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

#include <string>
#include <vector>
#include <sstream>

#include "../../twcommon.h"
#include "../../partitions.hpp"
#include "../../twrp-functions.hpp"

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


#define LOGDECRYPT(...) { printf(__VA_ARGS__); if (fp_kmsg) { fprintf(fp_kmsg, "[VOLD_DECRYPT]" __VA_ARGS__); fflush(fp_kmsg); } }
#define LOGDECRYPT_KMSG(...) { if (fp_kmsg) { fprintf(fp_kmsg, "[VOLD_DECRYPT]" __VA_ARGS__); fflush(fp_kmsg); } }

FILE *fp_kmsg = NULL;


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

bool Is_Service_Running(string initrc_svc) {
	char prop_value[PROPERTY_VALUE_MAX];
	string init_svc = "init.svc." + initrc_svc;
	property_get(init_svc.c_str(), prop_value, "error");
	return (strcmp(prop_value, "running") == 0);
}

bool Start_Service(string initrc_svc, int utimeout = SLEEP_MAX_USEC) {
	string res;
	string init_svc = "init.svc." + initrc_svc;

	property_set("ctl.start", initrc_svc.c_str());

	res = wait_for_property(init_svc, utimeout, "running");

	LOGDECRYPT("Start service %s: %s.\n", initrc_svc.c_str(), res.c_str());

	return (res == "running");
}

bool Stop_Service(string initrc_svc, int utimeout = SLEEP_MAX_USEC) {
	string res;
	string init_svc = "init.svc." + initrc_svc;

	property_set("ctl.stop", initrc_svc.c_str());

	res = wait_for_property(init_svc, utimeout, "stopped");

	LOGDECRYPT("Stop service %s: %s.\n", initrc_svc.c_str(), res.c_str());

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

int run_vdc(string Password) {
	int res;
	int utimeout;
	string vdc_res;
	int vdc_r1, vdc_r2, vdc_r3;

	LOGDECRYPT("About to run vdc...\n");

	// Wait for vold connection
	utimeout = SLEEP_MAX_USEC;
	while (utimeout > 0) {
		vdc_res.clear();
		// cryptfs getpwtype returns: R1=213(PasswordTypeResult)   R2=?   R3="password", "pattern", "pin", "default"
		res = TWFunc::Exec_Cmd("LD_LIBRARY_PATH=/system/lib64:/system/lib /system/bin/vdc cryptfs getpwtype", vdc_res);
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
		utimeout -= SLEEP_MIN_USEC;
		usleep(SLEEP_MIN_USEC); // vdc usually usleep(10000), but that causes too many unnecessary attempts
	}

	if (res != 0)
		return res;

	LOGDECRYPT("Connected to vold (%s)\n", vdc_res.c_str());

	// Input password from GUI, or default password
	vdc_res.clear();
	res = TWFunc::Exec_Cmd("LD_LIBRARY_PATH=/system/lib64:/system/lib /system/bin/vdc cryptfs checkpw '" + Password + "'", vdc_res);
	std::replace(vdc_res.begin(), vdc_res.end(), '\n', ' '); // remove newline(s)
	LOGDECRYPT("vdc cryptfs result (passwd): %s (ret=%d)\n", vdc_res.c_str(), res);
	vdc_r1 = vdc_r2 = vdc_r3 = -1;
	sscanf(vdc_res.c_str(), "%d %d %d", &vdc_r1, &vdc_r2, &vdc_r3);

	if (vdc_r3 != 0) {
		// try falling back to Lollipop hex passwords
		string hexPassword = convert_key_to_hex_ascii(Password);
		vdc_res.clear();
		res = TWFunc::Exec_Cmd("LD_LIBRARY_PATH=/system/lib64:/system/lib /system/bin/vdc cryptfs checkpw '" + hexPassword + "'", vdc_res);
		std::replace(vdc_res.begin(), vdc_res.end(), '\n', ' '); // remove newline(s)
		LOGDECRYPT("vdc cryptfs result (hex_pw): %s (ret=%d)\n", vdc_res.c_str(), res);
		vdc_r1 = vdc_r2 = vdc_r3 = -1;
		sscanf(vdc_res.c_str(), "%d %d %d", &vdc_r1, &vdc_r2, &vdc_r3);
	}

	if (vdc_r3 == 0) {
		// Decryption successful wait for crypto blk dev
		wait_for_property("ro.crypto.fs_crypto_blkdev");
	}

	return res;
}

bool Stop_sbinqseecomd(void) {
	char prop_value[PROPERTY_VALUE_MAX];
	property_get("init.svc.sbinqseecomd", prop_value, "error");
	if (strcmp(prop_value, "error") != 0 && strcmp(prop_value, "stopped") != 0) {
		LOGDECRYPT("sbinqseecomd is running, stopping it...\n");
		Stop_Service("sbinqseecomd");
		return true;
	}
	return false;
}

void Start_sbinqseecomd(void) {
	LOGDECRYPT("Restarting sbinqseecomd\n");
	Start_Service("sbinqseecomd");
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
		}
	}
	return is_vendor_symlinked;
}

void Restore_Vendor_Folder(void) {
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
		}
	}
	return is_firmware_symlinked;
}

void Restore_Firmware_Folder(void) {
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

	while(getline(ss, line)) {
		const char *fwfile = line.c_str();
		string base_name = TWFunc::Get_Filename(line);

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
}

int vold_decrypt(string Password)
{
	int res;
	bool output_dmesg_to_log = false;
	bool resume_sbinqseecomd = false;
	bool is_vendor_symlinked = false;
	bool is_firmware_symlinked = false;
	bool is_vold_running = false;
	bool is_qseecomd_running = false;

	if (Password.empty()) {
		LOGDECRYPT("vold_decrypt: password is empty!\n");
		return -1;
	}

	// Mount system and check for vold and vdc
	if (!PartitionManager.Mount_By_Path("/system", true)) {
		return -1;
	} else if (!TWFunc::Path_Exists("/system/bin/vold")) {
		LOGDECRYPT("ERROR: /system/bin/vold not found, aborting.\n");
		return -1;
	} else if (!TWFunc::Path_Exists("/system/bin/vdc")) {
		LOGDECRYPT("ERROR: /system/bin/vdc not found, aborting.\n");
		return -1;
	}

	fp_kmsg = fopen("/dev/kmsg", "a");

	LOGDECRYPT("TW_CRYPTO_USE_SYSTEM_VOLD := true\n");
	LOGDECRYPT("Attempting to use system's vold for decryption...\n");

	// Check if TWRPs sbinqseecomd is running
	resume_sbinqseecomd = Stop_sbinqseecomd();

	LOGDECRYPT("Setting up folders and permissions...\n");
	is_vendor_symlinked = Symlink_Vendor_Folder();
	is_firmware_symlinked = Symlink_Firmware_Folder();
	Symlink_Firmware_Files(is_vendor_symlinked, is_firmware_symlinked);

	set_needed_props();

	// Start services needed for vold decrypt
	LOGDECRYPT("Starting services...\n");
	is_qseecomd_running = Start_Service("qseecomd");
	is_vold_running = Start_Service("vold");

	if (is_vold_running) {

		if (!Is_Service_Running("qseecomd") && resume_sbinqseecomd) {
			// if qseecomd crashed restart sbinqseecomd
			LOGDECRYPT("qseecomd is not running, resuming sbinqseecomd!\n");
			Start_sbinqseecomd();
		}

		res = run_vdc(Password);

		if (res != 0) {
			// this is a vdc <--> vold error, not whether decryption was successful or not
			LOGDECRYPT("vdc returned an error: %d\n", res);
			output_dmesg_to_log = true;
		}
	} else {
		LOGDECRYPT("Failed to start vold\n");
		TWFunc::Exec_Cmd("echo \"$(getprop | grep init.svc)\" >> /dev/kmsg");
		output_dmesg_to_log = true;
	}

	// Stop services needed for vold decrypt so /system can be unmounted
	LOGDECRYPT("Stopping services...\n");
	Stop_Service("vold");
	if (!Is_Service_Running("qseecomd") && resume_sbinqseecomd)
		Stop_sbinqseecomd();
	Stop_Service("qseecomd");

	if (is_firmware_symlinked)
		Restore_Firmware_Folder();
	if (is_vendor_symlinked)
		Restore_Vendor_Folder();

	if (!PartitionManager.UnMount_By_Path("/system", true)) {
		// PartitionManager failed to unmount /system, this should not happen,
		// but in case it does, do a lazy unmount
		LOGDECRYPT("WARNING: system could not be unmounted normally!\n")
		TWFunc::Exec_Cmd("umount -l /system");
	}

	LOGDECRYPT("Finished.\n");

	// Start sbinqseecomd if it was previously running
	if (resume_sbinqseecomd)
		Start_sbinqseecomd();

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
