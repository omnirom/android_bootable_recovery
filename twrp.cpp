/*
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
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include "cutils/properties.h"
extern "C" {
#include "minadbd/adb.h"
#include "bootloader.h"
}

#ifdef ANDROID_RB_RESTART
#include "cutils/android_reboot.h"
#else
#include <sys/reboot.h>
#endif

extern "C" {
#include "gui/gui.h"
#include "set_metadata.h"
}
#include "twcommon.h"
#include "twrp-functions.hpp"
#include "data.hpp"
#include "partitions.hpp"
#include "openrecoveryscript.hpp"
#include "variables.h"
#include "twrpDU.hpp"

#ifdef HAVE_SELINUX
#include "selinux/label.h"
struct selabel_handle *selinux_handle;
#endif

TWPartitionManager PartitionManager;
int Log_Offset;
bool datamedia;
twrpDU du;

static void Print_Prop(const char *key, const char *name, void *cookie) {
	printf("%s=%s\n", key, name);
}

int main(int argc, char **argv) {
	// Recovery needs to install world-readable files, so clear umask
	// set by init
	umask(0);

	Log_Offset = 0;

	// Set up temporary log file (/tmp/recovery.log)
	freopen(TMP_LOG_FILE, "a", stdout);
	setbuf(stdout, NULL);
	freopen(TMP_LOG_FILE, "a", stderr);
	setbuf(stderr, NULL);

	signal(SIGPIPE, SIG_IGN);

	// Handle ADB sideload
	if (argc == 3 && strcmp(argv[1], "--adbd") == 0) {
		property_set("ctl.stop", "adbd");
		adb_main(argv[2]);
		return 0;
	}

#ifdef RECOVERY_SDCARD_ON_DATA
	datamedia = true;
#endif

	char crash_prop_val[PROPERTY_VALUE_MAX];
	int crash_counter;
	property_get("twrp.crash_counter", crash_prop_val, "-1");
	crash_counter = atoi(crash_prop_val) + 1;
	snprintf(crash_prop_val, sizeof(crash_prop_val), "%d", crash_counter);
	property_set("twrp.crash_counter", crash_prop_val);
	property_set("ro.twrp.boot", "1");
	property_set("ro.twrp.version", TW_VERSION_STR);

	time_t StartupTime = time(NULL);
	printf("Starting TWRP %s on %s (pid %d)\n", TW_VERSION_STR, ctime(&StartupTime), getpid());

	// Load default values to set DataManager constants and handle ifdefs
	DataManager::SetDefaultValues();
	printf("Starting the UI...");
	gui_init();
	printf("=> Linking mtab\n");
	symlink("/proc/mounts", "/etc/mtab");
	if (TWFunc::Path_Exists("/etc/twrp.fstab")) {
		if (TWFunc::Path_Exists("/etc/recovery.fstab")) {
			printf("Renaming regular /etc/recovery.fstab -> /etc/recovery.fstab.bak\n");
			rename("/etc/recovery.fstab", "/etc/recovery.fstab.bak");
		}
		printf("Moving /etc/twrp.fstab -> /etc/recovery.fstab\n");
		rename("/etc/twrp.fstab", "/etc/recovery.fstab");
	}
	printf("=> Processing recovery.fstab\n");
	if (!PartitionManager.Process_Fstab("/etc/recovery.fstab", 1)) {
		LOGERR("Failing out of recovery due to problem with recovery.fstab.\n");
		return -1;
	}
	PartitionManager.Output_Partition_Logging();
	// Load up all the resources
	gui_loadResources();

#ifdef HAVE_SELINUX
	if (TWFunc::Path_Exists("/prebuilt_file_contexts")) {
		if (TWFunc::Path_Exists("/file_contexts")) {
			printf("Renaming regular /file_contexts -> /file_contexts.bak\n");
			rename("/file_contexts", "/file_contexts.bak");
		}
		printf("Moving /prebuilt_file_contexts -> /file_contexts\n");
		rename("/prebuilt_file_contexts", "/file_contexts");
	}
	struct selinux_opt selinux_options[] = {
		{ SELABEL_OPT_PATH, "/file_contexts" }
	};
	selinux_handle = selabel_open(SELABEL_CTX_FILE, selinux_options, 1);
	if (!selinux_handle)
		printf("No file contexts for SELinux\n");
	else
		printf("SELinux contexts loaded from /file_contexts\n");
	{ // Check to ensure SELinux can be supported by the kernel
		char *contexts = NULL;

		if (PartitionManager.Mount_By_Path("/cache", true) && TWFunc::Path_Exists("/cache/recovery")) {
			lgetfilecon("/cache/recovery", &contexts);
			if (!contexts) {
				lsetfilecon("/cache/recovery", "test");
				lgetfilecon("/cache/recovery", &contexts);
			}
		} else {
			LOGINFO("Could not check /cache/recovery SELinux contexts, using /sbin/teamwin instead which may be inaccurate.\n");
			lgetfilecon("/sbin/teamwin", &contexts);
		}
		if (!contexts) {
			gui_print_color("warning", "Kernel does not have support for reading SELinux contexts.\n");
		} else {
			free(contexts);
			gui_print("Full SELinux support is present.\n");
		}
	}
#else
	gui_print_color("warning", "No SELinux support (no libselinux).\n");
#endif

	PartitionManager.Mount_By_Path("/cache", true);

	string Zip_File, Reboot_Value;
	bool Cache_Wipe = false, Factory_Reset = false, Perform_Backup = false, Shutdown = false;

	{
		TWPartition* misc = PartitionManager.Find_Partition_By_Path("/misc");
		if (misc != NULL) {
			if (misc->Current_File_System == "emmc") {
				set_device_type('e');
				set_device_name(misc->Actual_Block_Device.c_str());
			} else if (misc->Current_File_System == "mtd") {
				set_device_type('m');
				set_device_name(misc->MTD_Name.c_str());
			} else {
				LOGERR("Unknown file system for /misc\n");
			}
		}
		get_args(&argc, &argv);

		int index, index2, len;
		char* argptr;
		char* ptr;
		printf("Startup Commands: ");
		for (index = 1; index < argc; index++) {
			argptr = argv[index];
			printf(" '%s'", argv[index]);
			len = strlen(argv[index]);
			if (*argptr == '-') {argptr++; len--;}
			if (*argptr == '-') {argptr++; len--;}
			if (*argptr == 'u') {
				ptr = argptr;
				index2 = 0;
				while (*ptr != '=' && *ptr != '\n')
					ptr++;
				// skip the = before grabbing Zip_File
				while (*ptr == '=')
					ptr++;
				if (*ptr) {
					Zip_File = ptr;
				} else
					LOGERR("argument error specifying zip file\n");
			} else if (*argptr == 'w') {
				if (len == 9)
					Factory_Reset = true;
				else if (len == 10)
					Cache_Wipe = true;
			} else if (*argptr == 'n') {
				Perform_Backup = true;
			} else if (*argptr == 'p') {
				Shutdown = true;
			} else if (*argptr == 's') {
				ptr = argptr;
				index2 = 0;
				while (*ptr != '=' && *ptr != '\n')
					ptr++;
				if (*ptr) {
					Reboot_Value = *ptr;
				}
			}
		}
		printf("\n");
	}

	if(crash_counter == 0) {
		property_list(Print_Prop, NULL);
		printf("\n");
	} else {
		printf("twrp.crash_counter=%d\n", crash_counter);
	}

	// Check for and run startup script if script exists
	TWFunc::check_and_run_script("/sbin/runatboot.sh", "boot");
	TWFunc::check_and_run_script("/sbin/postrecoveryboot.sh", "boot");

#ifdef TW_INCLUDE_INJECTTWRP
	// Back up TWRP Ramdisk if needed:
	TWPartition* Boot = PartitionManager.Find_Partition_By_Path("/boot");
	LOGINFO("Backing up TWRP ramdisk...\n");
	if (Boot == NULL || Boot->Current_File_System != "emmc")
		TWFunc::Exec_Cmd("injecttwrp --backup /tmp/backup_recovery_ramdisk.img");
	else {
		string injectcmd = "injecttwrp --backup /tmp/backup_recovery_ramdisk.img bd=" + Boot->Actual_Block_Device;
		TWFunc::Exec_Cmd(injectcmd);
	}
	LOGINFO("Backup of TWRP ramdisk done.\n");
#endif

	bool Keep_Going = true;
	if (Perform_Backup) {
		DataManager::SetValue(TW_BACKUP_NAME, "(Auto Generate)");
		if (!OpenRecoveryScript::Insert_ORS_Command("backup BSDCAE\n"))
			Keep_Going = false;
	}
	if (Keep_Going && !Zip_File.empty()) {
		string ORSCommand = "install " + Zip_File;

		if (!OpenRecoveryScript::Insert_ORS_Command(ORSCommand))
			Keep_Going = false;
	}
	if (Keep_Going) {
		if (Factory_Reset) {
			if (!OpenRecoveryScript::Insert_ORS_Command("wipe data\n"))
				Keep_Going = false;
		} else if (Cache_Wipe) {
			if (!OpenRecoveryScript::Insert_ORS_Command("wipe cache\n"))
				Keep_Going = false;
		}
	}

	TWFunc::Update_Log_File();
	// Offer to decrypt if the device is encrypted
	if (DataManager::GetIntValue(TW_IS_ENCRYPTED) != 0) {
		LOGINFO("Is encrypted, do decrypt page first\n");
		if (gui_startPage("decrypt", 1, 1) != 0) {
			LOGERR("Failed to start decrypt GUI page.\n");
		} else {
			// Check for and load custom theme if present
			gui_loadCustomResources();
		}
	} else if (datamedia) {
		if (tw_get_default_metadata(DataManager::GetSettingsStoragePath().c_str()) != 0) {
			LOGINFO("Failed to get default contexts and file mode for storage files.\n");
		} else {
			LOGINFO("Got default contexts and file mode for storage files.\n");
		}
	}

	// Read the settings file
#ifdef TW_HAS_MTP
	// We unmount partitions sometimes during early boot which may override
	// the default of MTP being enabled by auto toggling MTP off. This
	// will force it back to enabled then get overridden by the settings
	// file, assuming that an entry for tw_mtp_enabled is set.
	DataManager::SetValue("tw_mtp_enabled", 1);
#endif
	DataManager::ReadSettingsFile();

	// Fixup the RTC clock on devices which require it
	if(crash_counter == 0)
		TWFunc::Fixup_Time_On_Boot();

	// Run any outstanding OpenRecoveryScript
	if (DataManager::GetIntValue(TW_IS_ENCRYPTED) == 0 && (TWFunc::Path_Exists(SCRIPT_FILE_TMP) || TWFunc::Path_Exists(SCRIPT_FILE_CACHE))) {
		OpenRecoveryScript::Run_OpenRecoveryScript();
	}

#ifdef TW_HAS_MTP
	// Enable MTP?
	char mtp_crash_check[PROPERTY_VALUE_MAX];
	property_get("mtp.crash_check", mtp_crash_check, "0");
	if (strcmp(mtp_crash_check, "0") == 0) {
		property_set("mtp.crash_check", "1");
		if (DataManager::GetIntValue("tw_mtp_enabled") == 1 && ((DataManager::GetIntValue(TW_IS_ENCRYPTED) != 0 && DataManager::GetIntValue(TW_IS_DECRYPTED) != 0) || DataManager::GetIntValue(TW_IS_ENCRYPTED) == 0)) {
			LOGINFO("Enabling MTP during startup\n");
			if (!PartitionManager.Enable_MTP())
				PartitionManager.Disable_MTP();
			else
				gui_print("MTP Enabled\n");
		} else {
			PartitionManager.Disable_MTP();
		}
		property_set("mtp.crash_check", "0");
	} else {
		gui_print_color("warning", "MTP Crashed, not starting MTP on boot.\n");
		DataManager::SetValue("tw_mtp_enabled", 0);
		PartitionManager.Disable_MTP();
	}
#else
	PartitionManager.Disable_MTP();
#endif

	// Launch the main GUI
	gui_start();

	// Disable flashing of stock recovery
	TWFunc::Disable_Stock_Recovery_Replace();
	// Check for su to see if the device is rooted or not
	if (PartitionManager.Mount_By_Path("/system", false)) {
		if (TWFunc::Path_Exists("/supersu/su") && !TWFunc::Path_Exists("/system/bin/su") && !TWFunc::Path_Exists("/system/xbin/su") && !TWFunc::Path_Exists("/system/bin/.ext/.su")) {
			// Device doesn't have su installed
			DataManager::SetValue("tw_busy", 1);
			if (gui_startPage("installsu", 1, 1) != 0) {
				LOGERR("Failed to start SuperSU install page.\n");
			}
		}
		sync();
		PartitionManager.UnMount_By_Path("/system", false);
	}

	// Reboot
	TWFunc::Update_Intent_File(Reboot_Value);
	TWFunc::Update_Log_File();
	gui_print("Rebooting...\n");
	string Reboot_Arg;
	DataManager::GetValue("tw_reboot_arg", Reboot_Arg);
	if (Reboot_Arg == "recovery")
		TWFunc::tw_reboot(rb_recovery);
	else if (Reboot_Arg == "poweroff")
		TWFunc::tw_reboot(rb_poweroff);
	else if (Reboot_Arg == "bootloader")
		TWFunc::tw_reboot(rb_bootloader);
	else if (Reboot_Arg == "download")
		TWFunc::tw_reboot(rb_download);
	else
		TWFunc::tw_reboot(rb_system);

#ifdef ANDROID_RB_RESTART
	android_reboot(ANDROID_RB_RESTART, 0, 0);
#else
	reboot(RB_AUTOBOOT);
#endif
	return 0;
}
