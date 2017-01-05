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


#define LOGDECRYPT(...) { printf(__VA_ARGS__); if (fp_kmsg) { fprintf(fp_kmsg, "[VOLD_DECRYPT]" __VA_ARGS__); fflush(fp_kmsg); } }
#define LOGDECRYPT_KMSG(...) { if (fp_kmsg) { fprintf(fp_kmsg, "[VOLD_DECRYPT]" __VA_ARGS__); fflush(fp_kmsg); } }

FILE *fp_kmsg = NULL;


/* Convert a binary key of specified length into an ascii hex string equivalent,
 * without the leading 0x and with null termination
 */
static void convert_key_to_hex_ascii(const unsigned char *master_key,
                                     unsigned int keysize, char *master_key_ascii) {
    unsigned int i, a;
    unsigned char nibble;

    for (i=0, a=0; i<keysize; i++, a+=2) {
        /* For each byte, write out two ascii hex digits */
        nibble = (master_key[i] >> 4) & 0xf;
        master_key_ascii[a] = nibble + (nibble > 9 ? 0x37 : 0x30);

        nibble = master_key[i] & 0xf;
        master_key_ascii[a+1] = nibble + (nibble > 9 ? 0x37 : 0x30);
    }

    /* Add the null termination */
    master_key_ascii[a] = '\0';

}


string wait_for_property(string property_name, int timeout = 5, string expected_value = "not_empty") {
	char prop_value[PROPERTY_VALUE_MAX];

	if (expected_value == "not_empty") {
		while (timeout > 0) {
			property_get(property_name.c_str(), prop_value, "error");
			if (strcmp(prop_value, "error") != 0)
				break;
			timeout--;
			sleep(1);
		}
	}
	else {
		while (timeout > 0) {
			property_get(property_name.c_str(), prop_value, "error");
			if (strcmp(prop_value, expected_value.c_str()) == 0)
				break;
			timeout--;
			sleep(1);
		}
	}
	property_get(property_name.c_str(), prop_value, "error");

	return prop_value;
}

int start_service(string initrc_svc, int timeout = 5) {
	string res;
	string init_svc = "init.svc." + initrc_svc;

	property_set("ctl.start", initrc_svc.c_str());

	res = wait_for_property(init_svc, timeout, "running");

	LOGDECRYPT("Start service %s: %s.\n", initrc_svc.c_str(), res.c_str());

	if (res == "running")
		return 0;
	else
		return -1;
}

int stop_service(string initrc_svc, int timeout = 5) {
	string res;
	string init_svc = "init.svc." + initrc_svc;

	property_set("ctl.stop", initrc_svc.c_str());

	res = wait_for_property(init_svc, timeout, "stopped");

	LOGDECRYPT("Stop service %s: %s.\n", initrc_svc.c_str(), res.c_str());

	if (res == "stopped")
		return 0;
	else
		return -1;
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
	int vdc_r1, vdc_r2, vdc_r3;

	LOGDECRYPT("About to run vdc...\n");

	// Input password from GUI, or default password
	string vdc_res_a;
	res = TWFunc::Exec_Cmd("LD_LIBRARY_PATH=/system/lib64:/system/lib /system/bin/vdc cryptfs checkpw '" + Password + "'", vdc_res_a);
	std::replace(vdc_res_a.begin(), vdc_res_a.end(), '\n', ' '); // remove newline(s)
	LOGDECRYPT("vdc cryptfs result (passwd): %s (ret=%d)\n", vdc_res_a.c_str(), res);
	vdc_r1 = vdc_r2 = vdc_r3 = -1;
	sscanf(vdc_res_a.c_str(), "%d %d %d", &vdc_r1, &vdc_r2, &vdc_r3);

	if (vdc_r3 != 0) {
		// try falling back to Lollipop hex passwords
		int hex_pass_len = strlen(Password.c_str()) * 2 + 1;
		char *hex_passwd = (char *)malloc(hex_pass_len);
		if (hex_passwd) {
			convert_key_to_hex_ascii((unsigned char *)Password.c_str(),
								   strlen(Password.c_str()), hex_passwd);

			string vdc_res_b;
			string hexPassword = hex_passwd;
			res = TWFunc::Exec_Cmd("LD_LIBRARY_PATH=/system/lib64:/system/lib /system/bin/vdc cryptfs checkpw '" + hexPassword + "'", vdc_res_b);
			std::replace(vdc_res_b.begin(), vdc_res_b.end(), '\n', ' '); // remove newline(s)
			LOGDECRYPT("vdc cryptfs result (hex_pw): %s (ret=%d)\n", vdc_res_b.c_str(), res);
			vdc_r1 = vdc_r2 = vdc_r3 = -1;
			sscanf(vdc_res_a.c_str(), "%d %d %d", &vdc_r1, &vdc_r2, &vdc_r3);

			memset(hex_passwd, 0, hex_pass_len);
			free(hex_passwd);
		}
	}

	return res;
}

bool Stop_sbinqseecomd(void) {
	char prop_value[PROPERTY_VALUE_MAX];
	property_get("init.svc.sbinqseecomd", prop_value, "error");
	if (strcmp(prop_value, "error") != 0 && strcmp(prop_value, "stopped") != 0) {
		LOGDECRYPT("sbinqseecomd is running, stopping it...\n");
		stop_service("sbinqseecomd");
		return true;
	}
	return false;
}

void Start_sbinqseecomd(void) {
	LOGDECRYPT("Restarting sbinqseecomd\n");
	start_service("sbinqseecomd");
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
	int rc;
	int res;
	bool output_dmesg_to_log = false;
	bool resume_sbinqseecomd = false;
	bool is_vendor_symlinked = false;
	bool is_firmware_symlinked = false;

	fp_kmsg = fopen("/dev/kmsg", "a");

	LOGDECRYPT("TW_CRYPTO_USE_SYSTEM_VOLD := true\n");
	LOGDECRYPT("Attempting to use system's vold for decryption...\n");

	// Mount system and check for vold and vdc
	if (!PartitionManager.Mount_By_Path("/system", true)) {
		goto out;
	} else if (!TWFunc::Path_Exists("/system/bin/vold")) {
		LOGDECRYPT("ERROR: /system/bin/vold not found, aborting.\n");
		goto out;
	} else if (!TWFunc::Path_Exists("/system/bin/vdc")) {
		LOGDECRYPT("ERROR: /system/bin/vdc not found, aborting.\n");
		goto out;
	}

	// Check if TWRPs sbinqseecomd is running
	resume_sbinqseecomd = Stop_sbinqseecomd();

	LOGDECRYPT("Setting up folders and permissions...\n");
	is_vendor_symlinked = Symlink_Vendor_Folder();
	is_firmware_symlinked = Symlink_Firmware_Folder();
	Symlink_Firmware_Files(is_vendor_symlinked, is_firmware_symlinked);

	set_needed_props();

	// Start services needed for vold decrypt
	LOGDECRYPT("Starting services...\n");
	start_service("qseecomd");
	rc = start_service("vold");

	if (rc == 0) {
		// This is needed, even if vold is running, the socket may not be ready
		// and result in "Error connecting to cryptd: Connection refused"
		sleep(1);
		// Alternatives:
		//    * add a function to check for the socket
		//    * use timeout with vdc --wait (but not all vdc's have --wait)
		//         eg: timeout -t 30 vdc --wait cryptfs checkpw "$1"
		//         - you have to use timeout (or some kind of kill loop), otherwise if the socket never becomes
		//           available we'll be stuck here! (TWRP splash)

		res = run_vdc(Password);

		if (res != 0) {
			// this is a vdc <--> vold error, not whether decryption was successful or not
			LOGDECRYPT("vdc returned an error: %d\n", res);
			output_dmesg_to_log = true;
		}

		sleep(1); // doesn't seem needed, but maybe we should keep it to make sure vold has finished before stopping it
	} else {
		LOGDECRYPT("Failed to start vold\n");
		TWFunc::Exec_Cmd("echo \"$(getprop | grep init.svc)\" >> /dev/kmsg");
		output_dmesg_to_log = true;
	}

	// Stop services needed for vold decrypt so /system can be unmounted
	LOGDECRYPT("Stopping services...\n");
	stop_service("vold");
	stop_service("qseecomd");

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

out:
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
