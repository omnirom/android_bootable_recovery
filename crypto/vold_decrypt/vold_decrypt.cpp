/*
    Copyright 2017 TeamWin
    This file is part of TWRP/TeamWin Recovery Project.

    TWRP is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    TWRP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>

#ifdef TW_CRYPTO_SYSTEM_VOLD_DEBUG
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include <string>
#include <vector>
#include <sstream>

#include "../../twcommon.h"
#include "../../partitions.hpp"
#include "../../twrp-functions.hpp"
#include "../../gui/gui.hpp"

using namespace std;

extern "C" {
	#include <cutils/properties.h>
}

/* Timeouts as defined by ServiceManager */

/* The maximum amount of time to wait for a service to start or stop,
 * in micro-seconds (really an approximation) */
#define  SLEEP_MAX_USEC     2000000  /* 2 seconds */
/* The minimal sleeping interval between checking for the service's state
 * when looping for SLEEP_MAX_USEC */
#define  SLEEP_MIN_USEC      200000  /* 200 msec */


#define LOGDECRYPT(...) do { printf(__VA_ARGS__); if (fp_kmsg) { fprintf(fp_kmsg, "[VOLD_DECRYPT]" __VA_ARGS__); fflush(fp_kmsg); } } while (0)
#define LOGDECRYPT_KMSG(...) do { if (fp_kmsg) { fprintf(fp_kmsg, "[VOLD_DECRYPT]" __VA_ARGS__); fflush(fp_kmsg); } } while (0)

#ifdef TW_CRYPTO_SYSTEM_VOLD_SERVICES
typedef struct {
	string service_name;
	string twrp_svc_name;
	bool is_running;
	bool resume;
} AdditionalService;
#endif

FILE *fp_kmsg = NULL;
bool has_timeout = false;

#ifdef TW_CRYPTO_SYSTEM_VOLD_DEBUG
bool has_strace = false;

pid_t strace_init(void) {
	if (!has_strace)
		return -1;

	pid_t pid;
	switch(pid = fork())
	{
		case -1:
			LOGDECRYPT_KMSG("forking strace_init failed: %d!\n", errno);
			return -1;
		case 0: // child
			execl("/sbin/strace", "strace", "-q", "-tt", "-ff", "-v", "-y", "-s", "1000", "-o", "/tmp/strace_init.log", "-p", "1" , NULL);
			LOGDECRYPT_KMSG("strace_init fork failed: %d!\n", errno);
			exit(-1);
		default:
			LOGDECRYPT_KMSG("Starting strace_init (pid=%d)\n", pid);
			return pid;
	}
}
#endif

/* Convert a binary key of specified length into an ascii hex string equivalent,
 * without the leading 0x and with null termination
 *
 * Original code from cryptfs.c
 */
string convert_key_to_hex_ascii(string master_key) {
	size_t i;
	unsigned char nibble;
	string master_key_ascii = "";

	for (i = 0; i < master_key.size(); ++i) {
		nibble = (master_key[i] >> 4) & 0xf;
		nibble += nibble > 9 ? 0x57 : 0x30;
		master_key_ascii += nibble;

		nibble = master_key[i] & 0xf;
		nibble += nibble > 9 ? 0x57 : 0x30;
		master_key_ascii += nibble;
	}

	return master_key_ascii;
}

string wait_for_property(string property_name, int utimeout = SLEEP_MAX_USEC, string expected_value = "not_empty") {
	char prop_value[PROPERTY_VALUE_MAX];

	if (expected_value == "not_empty") {
		while (utimeout > 0) {
			property_get(property_name.c_str(), prop_value, "error");
			if (strcmp(prop_value, "error") != 0)
				break;
			LOGDECRYPT_KMSG("waiting for %s to get set\n", property_name.c_str());
			utimeout -= SLEEP_MIN_USEC;
			usleep(SLEEP_MIN_USEC);;
		}
	}
	else {
		while (utimeout > 0) {
			property_get(property_name.c_str(), prop_value, "error");
			if (strcmp(prop_value, expected_value.c_str()) == 0)
				break;
			LOGDECRYPT_KMSG("waiting for %s to change from '%s' to '%s'\n", property_name.c_str(), prop_value, expected_value.c_str());
			utimeout -= SLEEP_MIN_USEC;
			usleep(SLEEP_MIN_USEC);;
		}
	}
	property_get(property_name.c_str(), prop_value, "error");

	return prop_value;
}

bool Service_Exists(string initrc_svc) {
	char prop_value[PROPERTY_VALUE_MAX];
	string init_svc = "init.svc." + initrc_svc;
	property_get(init_svc.c_str(), prop_value, "error");
	return (strcmp(prop_value, "error") != 0);
}

bool Is_Service_Running(string initrc_svc) {
	char prop_value[PROPERTY_VALUE_MAX];
	string init_svc = "init.svc." + initrc_svc;
	property_get(init_svc.c_str(), prop_value, "error");
	return (strcmp(prop_value, "running") == 0);
}

bool Is_Service_Stopped(string initrc_svc) {
	char prop_value[PROPERTY_VALUE_MAX];
	string init_svc = "init.svc." + initrc_svc;
	property_get(init_svc.c_str(), prop_value, "error");
	return (strcmp(prop_value, "stopped") == 0);
}

bool Start_Service(string initrc_svc, int utimeout = SLEEP_MAX_USEC) {
	string res = "error";
	string init_svc = "init.svc." + initrc_svc;

	property_set("ctl.start", initrc_svc.c_str());

	res = wait_for_property(init_svc, utimeout, "running");

	LOGDECRYPT("Start service %s: %s.\n", initrc_svc.c_str(), res.c_str());

	return (res == "running");
}

bool Stop_Service(string initrc_svc, int utimeout = SLEEP_MAX_USEC) {
	string res = "error";

	if (Service_Exists(initrc_svc)) {
		string init_svc = "init.svc." + initrc_svc;
		property_set("ctl.stop", initrc_svc.c_str());
		res = wait_for_property(init_svc, utimeout, "stopped");
		LOGDECRYPT("Stop service %s: %s.\n", initrc_svc.c_str(), res.c_str());
	}

	return (res == "stopped");
}

void output_dmesg_to_recoverylog(void) {
	TWFunc::Exec_Cmd(
		"echo \"---- DMESG LOG FOLLOWS ----\";"
		"dmesg | grep 'DECRYPT\\|vold\\|qseecom\\|QSEECOM\\|keymaste\\|keystore\\|cmnlib';"
		"echo \"---- DMESG LOG ENDS ----\""
	);
}

void set_needed_props(void) {
	// vold won't start without ro.storage_structure on Kitkat
	string sdkverstr = TWFunc::System_Property_Get("ro.build.version.sdk");
	int sdkver = 20;
	if (!sdkverstr.empty()) {
		sdkver = atoi(sdkverstr.c_str());
	}
	if (sdkver <= 19) {
		string ro_storage_structure = TWFunc::System_Property_Get("ro.storage_structure");
		if (!ro_storage_structure.empty())
			property_set("ro.storage_structure", ro_storage_structure.c_str());
	}
}

string vdc_cryptfs_cmd(string log_name) {
	string cmd = "LD_LIBRARY_PATH=/system/lib64:/system/lib /system/bin/vdc cryptfs";

#ifndef TW_CRYPTO_SYSTEM_VOLD_DEBUG
	(void)log_name; // do nothing, but get rid of compiler warning in non debug builds
#else
	if (has_timeout && has_strace)
		cmd = "/sbin/strace -q -tt -ff -v -y -s 1000 -o /tmp/strace_vdc_" + log_name + " /sbin/timeout -t 30 -s KILL env " + cmd;
	else if (has_strace)
		cmd = "/sbin/strace -q -tt -ff -v -y -s 1000 -o /tmp/strace_vdc_" + log_name + " -E " + cmd;
	else
#endif
	if (has_timeout)
		cmd = "/sbin/timeout -t 30 -s KILL env " + cmd;

	return cmd;
}

int run_vdc(string Password) {
	int res = -1;
	struct timeval t1, t2;
	string vdc_res;
	int vdc_r1, vdc_r2, vdc_r3;

	LOGDECRYPT("About to run vdc...\n");

	// Wait for vold connection
	gettimeofday(&t1, NULL);
	t2 = t1;
	while ((t2.tv_sec - t1.tv_sec) < 5) {
		vdc_res.clear();
		// cryptfs getpwtype returns: R1=213(PasswordTypeResult)   R2=?   R3="password", "pattern", "pin", "default"
		res = TWFunc::Exec_Cmd(vdc_cryptfs_cmd("connect") + " getpwtype", vdc_res);
		std::replace(vdc_res.begin(), vdc_res.end(), '\n', ' '); // remove newline(s)
		vdc_r1 = vdc_r2 = vdc_r3 = -1;
		sscanf(vdc_res.c_str(), "%d", &vdc_r1);
		if (vdc_r1 == 213) {
			char str_res[sizeof(int) + 1];
			snprintf(str_res, sizeof(str_res), "%d", res);
			vdc_res += "ret=";
			vdc_res += str_res;
			res = 0;
			break;
		}
		LOGDECRYPT("Retrying connection to vold\n");
		usleep(SLEEP_MIN_USEC); // vdc usually usleep(10000), but that causes too many unnecessary attempts
		gettimeofday(&t2, NULL);
	}

	if (res != 0)
		return res;

	LOGDECRYPT("Connected to vold (%s)\n", vdc_res.c_str());

	// Input password from GUI, or default password
	vdc_res.clear();
	res = TWFunc::Exec_Cmd(vdc_cryptfs_cmd("passwd") + " checkpw '" + Password + "'", vdc_res);
	std::replace(vdc_res.begin(), vdc_res.end(), '\n', ' '); // remove newline(s)
	LOGDECRYPT("vdc cryptfs result (passwd): %s (ret=%d)\n", vdc_res.c_str(), res);
	vdc_r1 = vdc_r2 = vdc_r3 = -1;
	sscanf(vdc_res.c_str(), "%d %d %d", &vdc_r1, &vdc_r2, &vdc_r3);

	if (vdc_r3 != 0) {
		// try falling back to Lollipop hex passwords
		string hexPassword = convert_key_to_hex_ascii(Password);
		vdc_res.clear();
		res = TWFunc::Exec_Cmd(vdc_cryptfs_cmd("hex_pw") + " checkpw '" + hexPassword + "'", vdc_res);
		std::replace(vdc_res.begin(), vdc_res.end(), '\n', ' '); // remove newline(s)
		LOGDECRYPT("vdc cryptfs result (hex_pw): %s (ret=%d)\n", vdc_res.c_str(), res);
		vdc_r1 = vdc_r2 = vdc_r3 = -1;
		sscanf(vdc_res.c_str(), "%d %d %d", &vdc_r1, &vdc_r2, &vdc_r3);
	}

	// vdc's return value is dependant upon source origin, it will either
	// return 0 or vdc_r1, so disregard and focus on decryption instead
	if (vdc_r3 == 0) {
		// Decryption successful wait for crypto blk dev
		wait_for_property("ro.crypto.fs_crypto_blkdev");
		res = 0;
	} else {
		res = -1;
	}

	return res;
}

bool Symlink_Vendor_Folder(void) {
	bool is_vendor_symlinked = false;

	if (PartitionManager.Is_Mounted_By_Path("/vendor")) {
		LOGDECRYPT("vendor partition mounted, skipping /vendor substitution\n");
	}
	else if (TWFunc::Path_Exists("/system/vendor")) {
		LOGDECRYPT("Symlinking vendor folder...\n");
		if (TWFunc::Path_Exists("/vendor") && rename("/vendor", "/vendor-orig") != 0) {
			LOGDECRYPT("Failed to rename original /vendor folder: %s\n", strerror(errno));
		} else {
			TWFunc::Recursive_Mkdir("/vendor/firmware/keymaster");
			LOGDECRYPT_KMSG("Symlinking /system/vendor/lib64 to /vendor/lib64 (res=%d)\n",
				symlink("/system/vendor/lib64", "/vendor/lib64")
			);
			LOGDECRYPT_KMSG("Symlinking /system/vendor/lib to /vendor/lib (res=%d)\n",
				symlink("/system/vendor/lib", "/vendor/lib")
			);
			is_vendor_symlinked = true;
			property_set("vold_decrypt.symlinked_vendor", "1");
		}
	}
	return is_vendor_symlinked;
}

void Restore_Vendor_Folder(void) {
	property_set("vold_decrypt.symlinked_vendor", "0");
	TWFunc::removeDir("/vendor", false);
	rename("/vendor-orig", "/vendor");
}

bool Symlink_Firmware_Folder(void) {
	bool is_firmware_symlinked = false;

	if (PartitionManager.Is_Mounted_By_Path("/firmware")) {
		LOGDECRYPT("firmware partition mounted, skipping /firmware substitution\n");
	} else {
		LOGDECRYPT("Symlinking firmware folder...\n");
		if (TWFunc::Path_Exists("/firmware") && rename("/firmware", "/firmware-orig") != 0) {
			LOGDECRYPT("Failed to rename original /firmware folder: %s\n", strerror(errno));
		} else {
			TWFunc::Recursive_Mkdir("/firmware/image");
			is_firmware_symlinked = true;
			property_set("vold_decrypt.symlinked_firmware", "1");
		}
	}
	return is_firmware_symlinked;
}

void Restore_Firmware_Folder(void) {
	property_set("vold_decrypt.symlinked_firmware", "0");
	TWFunc::removeDir("/firmware", false);
	rename("/firmware-orig", "/firmware");
}

void Symlink_Firmware_Files(bool is_vendor_symlinked, bool is_firmware_symlinked) {
	if (!is_vendor_symlinked && !is_firmware_symlinked)
		return;

	LOGDECRYPT("Symlinking firmware files...\n");
	string result_of_find;
	TWFunc::Exec_Cmd("find /system -name keymaste*.* -type f -o -name cmnlib.* -type f 2>/dev/null", result_of_find);

	stringstream ss(result_of_find);
	string line;
	int count = 0;

	while(getline(ss, line)) {
		const char *fwfile = line.c_str();
		string base_name = TWFunc::Get_Filename(line);
		count++;

		if (is_firmware_symlinked) {
			LOGDECRYPT_KMSG("Symlinking %s to /firmware/image/ (res=%d)\n", fwfile,
				symlink(fwfile, ("/firmware/image/" + base_name).c_str())
			);
		}

		if (is_vendor_symlinked) {
			LOGDECRYPT_KMSG("Symlinking %s to /vendor/firmware/ (res=%d)\n", fwfile,
				symlink(fwfile, ("/vendor/firmware/" + base_name).c_str())
			);

			LOGDECRYPT_KMSG("Symlinking %s to /vendor/firmware/keymaster/ (res=%d)\n", fwfile,
				symlink(fwfile, ("/vendor/firmware/keymaster/" + base_name).c_str())
			);
		}
	}
	LOGDECRYPT("%d file(s) symlinked.\n", count);
}

#ifdef TW_CRYPTO_SYSTEM_VOLD_SERVICES
vector<AdditionalService> Get_List_Of_Additional_Services (void) {
	vector<AdditionalService> services;

	vector<string> service_names = TWFunc::Split_String(TW_CRYPTO_SYSTEM_VOLD_SERVICES, " ");

	for (size_t i = 0; i < service_names.size(); ++i) {
		AdditionalService svc;
		svc.service_name = service_names[i];
		services.push_back(svc);
	}

	return services;
}
#endif

int vold_decrypt(string Password)
{
	int res;
	bool output_dmesg_to_log = false;
	bool is_vendor_symlinked = false;
	bool is_firmware_symlinked = false;
	bool is_vold_running = false;

	if (Password.empty()) {
		LOGDECRYPT("vold_decrypt: password is empty!\n");
		return -1;
	}

	// Mount system and check for vold and vdc
	if (!PartitionManager.Mount_By_Path("/system", true)) {
		return -1;
	} else if (!TWFunc::Path_Exists("/system/bin/vold")) {
		LOGDECRYPT("ERROR: /system/bin/vold not found, aborting.\n");
		gui_msg(Msg(msg::kError, "decrypt_data_vold_os_missing=Missing files needed for vold decrypt: {1}")("/system/bin/vold"));
		return -1;
	} else if (!TWFunc::Path_Exists("/system/bin/vdc")) {
		LOGDECRYPT("ERROR: /system/bin/vdc not found, aborting.\n");
		gui_msg(Msg(msg::kError, "decrypt_data_vold_os_missing=Missing files needed for vold decrypt: {1}")("/system/bin/vdc"));
		return -1;
	}

	fp_kmsg = fopen("/dev/kmsg", "a");

	LOGDECRYPT("TW_CRYPTO_USE_SYSTEM_VOLD := true\n");
	LOGDECRYPT("Attempting to use system's vold for decryption...\n");

#ifndef TW_CRYPTO_SYSTEM_VOLD_DISABLE_TIMEOUT
	has_timeout = TWFunc::Path_Exists("/sbin/timeout");
	if (!has_timeout)
		LOGDECRYPT("timeout binary not found, disabling timeout in vold_decrypt!\n");
#endif

#ifdef TW_CRYPTO_SYSTEM_VOLD_DEBUG
	has_strace = TWFunc::Path_Exists("/sbin/strace");
	if (!has_strace)
		LOGDECRYPT("strace binary not found, disabling strace in vold_decrypt!\n");
	pid_t pid_strace = strace_init();
#endif

#ifdef TW_CRYPTO_SYSTEM_VOLD_SERVICES
	vector<AdditionalService> Services = Get_List_Of_Additional_Services();

	// Check if TWRP is running any of the services
	for (size_t i = 0; i < Services.size(); ++i) {
		if (Service_Exists(Services[i].service_name))
			Services[i].twrp_svc_name = Services[i].service_name;
		else if (Service_Exists("sbin" + Services[i].service_name))
			Services[i].twrp_svc_name = "sbin" + Services[i].service_name;
		else
			Services[i].twrp_svc_name.clear();

		if (!Services[i].twrp_svc_name.empty() && !Is_Service_Stopped(Services[i].twrp_svc_name)) {
			Services[i].resume = true;
			Stop_Service(Services[i].twrp_svc_name);
		} else
			Services[i].resume = false;

		// vold_decrypt system services have to be named sys_{service} in the .rc files
		Services[i].service_name = "sys_" + Services[i].service_name;
	}
#endif

	LOGDECRYPT("Setting up folders and permissions...\n");
	is_vendor_symlinked = Symlink_Vendor_Folder();
	is_firmware_symlinked = Symlink_Firmware_Folder();
	Symlink_Firmware_Files(is_vendor_symlinked, is_firmware_symlinked);

	set_needed_props();

	// Start services needed for vold decrypt
	LOGDECRYPT("Starting services...\n");
#ifdef TW_CRYPTO_SYSTEM_VOLD_SERVICES
	for (size_t i = 0; i < Services.size(); ++i) {
		Services[i].is_running = Start_Service(Services[i].service_name);
	}
#endif
	is_vold_running = Start_Service("sys_vold");

	if (is_vold_running) {

#ifdef TW_CRYPTO_SYSTEM_VOLD_SERVICES
		for (size_t i = 0; i < Services.size(); ++i) {
			if (!Is_Service_Running(Services[i].service_name) && Services[i].resume) {
				// if system_service has died restart the twrp_service
				LOGDECRYPT("%s is not running, resuming %s!\n", Services[i].service_name.c_str(), Services[i].twrp_svc_name.c_str());
				Start_Service(Services[i].twrp_svc_name);
			}
		}
#endif

		res = run_vdc(Password);

		if (res != 0) {
			// Decryption was unsuccessful
			LOGDECRYPT("Decryption failed\n");
			output_dmesg_to_log = true;
		}
	} else {
		LOGDECRYPT("Failed to start vold\n");
		TWFunc::Exec_Cmd("echo \"$(getprop | grep init.svc)\" >> /dev/kmsg");
		output_dmesg_to_log = true;
	}

	// Stop services needed for vold decrypt so /system can be unmounted
	LOGDECRYPT("Stopping services...\n");
	Stop_Service("sys_vold");
#ifdef TW_CRYPTO_SYSTEM_VOLD_SERVICES
	for (size_t i = 0; i < Services.size(); ++i) {
		if (!Is_Service_Running(Services[i].service_name) && Services[i].resume)
			Stop_Service(Services[i].twrp_svc_name);
		else
			Stop_Service(Services[i].service_name);
	}
#endif

	if (is_firmware_symlinked)
		Restore_Firmware_Folder();
	if (is_vendor_symlinked)
		Restore_Vendor_Folder();

	if (!PartitionManager.UnMount_By_Path("/system", true)) {
		// PartitionManager failed to unmount /system, this should not happen,
		// but in case it does, do a lazy unmount
		LOGDECRYPT("WARNING: system could not be unmounted normally!\n");
		TWFunc::Exec_Cmd("umount -l /system");
	}

	LOGDECRYPT("Finished.\n");

#ifdef TW_CRYPTO_SYSTEM_VOLD_SERVICES
	// Restart previously running services
	for (size_t i = 0; i < Services.size(); ++i) {
		if (Services[i].resume)
			Start_Service(Services[i].twrp_svc_name);
	}
#endif

#ifdef TW_CRYPTO_SYSTEM_VOLD_DEBUG
	if (pid_strace > 0) {
		LOGDECRYPT_KMSG("Stopping strace_init (pid=%d)\n", pid_strace);
		int timeout;
		int status;
		pid_t retpid = waitpid(pid_strace, &status, WNOHANG);

		kill(pid_strace, SIGTERM);
		for (timeout = 5; retpid == 0 && timeout; --timeout) {
			sleep(1);
			retpid = waitpid(pid_strace, &status, WNOHANG);
		}
		if (retpid)
			LOGDECRYPT_KMSG("strace_init terminated successfully\n");
		else {
			// SIGTERM didn't work, kill it instead
			kill(pid_strace, SIGKILL);
			for (timeout = 5; retpid == 0 && timeout; --timeout) {
				sleep(1);
				retpid = waitpid(pid_strace, &status, WNOHANG);
			}
			if (retpid)
				LOGDECRYPT_KMSG("strace_init killed successfully\n");
			else
				LOGDECRYPT_KMSG("strace_init took too long to kill, may be a zombie process\n");
		}
	}
	output_dmesg_to_log = true;
#endif

	// Finish up and exit
	if (fp_kmsg) {
		fflush(fp_kmsg);
		fclose(fp_kmsg);
	}

	if (output_dmesg_to_log)
		output_dmesg_to_recoverylog();

	// Finally check if crypto device is up
	if (wait_for_property("ro.crypto.fs_crypto_blkdev", 0) != "error")
		res = 0;
	else
		res = -1;

	return res;
}
