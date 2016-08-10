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
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include "gui/twmsg.h"

#include "cutils/properties.h"
extern "C" {
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
#include "gui/gui.hpp"
#include "gui/pages.hpp"
#include "gui/objects.hpp"
#include "twcommon.h"
#include "twrp-functions.hpp"
#include "data.hpp"
#include "partitions.hpp"
#include "openrecoveryscript.hpp"
#include "variables.h"
#include "twrpDU.hpp"
#ifdef TW_USE_NEW_MINADBD
#include "adb.h"
#else
extern "C" {
#include "minadbd.old/adb.h"
}
#endif

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
#ifdef TW_USE_NEW_MINADBD
		adb_main(0, DEFAULT_ADB_PORT);
#else
		adb_main(argv[2]);
#endif
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
	printf("Starting TWRP %s-%s on %s (pid %d)\n", TW_VERSION_STR, TW_GIT_REVISION, ctime(&StartupTime), getpid());

	// Load default values to set DataManager constants and handle ifdefs
	DataManager::SetDefaultValues();
	printf("Starting the UI...\n");
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
			gui_warn("no_kernel_selinux=Kernel does not have support for reading SELinux contexts.");
		} else {
			free(contexts);
			gui_msg("full_selinux=Full SELinux support is present.");
		}
	}
#else
	gui_warn("no_selinux=No SELinux support (no libselinux).");
#endif

	PartitionManager.Mount_By_Path("/cache", true);

	string Reboot_Value;
	bool Shutdown = false;

	{
		TWPartition* misc = PartitionManager.Find_Partition_By_Path("/misc");
		if (misc != NULL) {
			if (misc->Current_File_System == "emmc") {
				set_misc_device("emmc", misc->Actual_Block_Device.c_str());
			} else if (misc->Current_File_System == "mtd") {
				set_misc_device("mtd", misc->MTD_Name.c_str());
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
					string ORSCommand = "install ";
					ORSCommand.append(ptr);

					if (!OpenRecoveryScript::Insert_ORS_Command(ORSCommand))
						break;
				} else
					LOGERR("argument error specifying zip file\n");
			} else if (*argptr == 'w') {
				if (len == 9) {
					if (!OpenRecoveryScript::Insert_ORS_Command("wipe data\n"))
						break;
				} else if (len == 10) {
					if (!OpenRecoveryScript::Insert_ORS_Command("wipe cache\n"))
						break;
				}
			} else if (*argptr == 'n') {
				DataManager::SetValue(TW_BACKUP_NAME, gui_parse_text("{@auto_generate}"));
				if (!OpenRecoveryScript::Insert_ORS_Command("backup BSDCAE\n"))
					break;
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
	PageManager::LoadLanguage(DataManager::GetStrValue("tw_language"));
	GUIConsole::Translate_Now();

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
				gui_msg("mtp_enabled=MTP Enabled");
		} else {
			PartitionManager.Disable_MTP();
		}
		property_set("mtp.crash_check", "0");
	} else {
		gui_warn("mtp_crash=MTP Crashed, not starting MTP on boot.");
		DataManager::SetValue("tw_mtp_enabled", 0);
		PartitionManager.Disable_MTP();
	}
#else
	PartitionManager.Disable_MTP();
#endif

#ifndef TW_OEM_BUILD
	// Check if system has never been changed
	TWPartition* sys = PartitionManager.Find_Partition_By_Path("/system");
	TWPartition* ven = PartitionManager.Find_Partition_By_Path("/vendor");
	if (sys) {
		if ((DataManager::GetIntValue("tw_mount_system_ro") == 0 && sys->Check_Lifetime_Writes() == 0) || DataManager::GetIntValue("tw_mount_system_ro") == 2) {
			if (DataManager::GetIntValue("tw_never_show_system_ro_page") == 0) {
				DataManager::SetValue("tw_back", "main");
				if (gui_startPage("system_readonly", 1, 1) != 0) {
					LOGERR("Failed to start system_readonly GUI page.\n");
				}
			} else if (DataManager::GetIntValue("tw_mount_system_ro") == 0) {
				sys->Change_Mount_Read_Only(false);
				if (ven)
					ven->Change_Mount_Read_Only(false);
			}
		} else if (DataManager::GetIntValue("tw_mount_system_ro") == 1) {
			// Do nothing, user selected to leave system read only
		} else {
			sys->Change_Mount_Read_Only(false);
			if (ven)
				ven->Change_Mount_Read_Only(false);
		}
	}
#endif
	// Launch the main GUI
	gui_start();

#ifndef TW_OEM_BUILD
	// Disable flashing of stock recovery
	TWFunc::Disable_Stock_Recovery_Replace();
	// Check for su to see if the device is rooted or not
	if (DataManager::GetIntValue("tw_mount_system_ro") == 0 && PartitionManager.Mount_By_Path("/system", false)) {
		// read /system/build.prop to get sdk version and do not offer to root if running M or higher (sdk version 23 == M)
		string sdkverstr = TWFunc::System_Property_Get("ro.build.version.sdk");
		int sdkver = 23;
		if (!sdkverstr.empty()) {
			sdkver = atoi(sdkverstr.c_str());
		}
		if (TWFunc::Path_Exists("/supersu/su") && TWFunc::Path_Exists("/system/bin") && !TWFunc::Path_Exists("/system/bin/su") && !TWFunc::Path_Exists("/system/xbin/su") && !TWFunc::Path_Exists("/system/bin/.ext/.su") && sdkver < 23) {
			// Device doesn't have su installed
			DataManager::SetValue("tw_busy", 1);
			if (gui_startPage("installsu", 1, 1) != 0) {
				LOGERR("Failed to start SuperSU install page.\n");
			}
		}
		sync();
		PartitionManager.UnMount_By_Path("/system", false);
	}
#endif

	// Reboot
	TWFunc::Update_Intent_File(Reboot_Value);
	TWFunc::Update_Log_File();
	gui_msg(Msg("rebooting=Rebooting..."));
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

	return 0;
}
