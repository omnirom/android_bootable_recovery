/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _VARIABLES_HEADER_
#define _VARIABLES_HEADER_

#define TW_VERSION_STR              "3.0.2-0"

#define TW_USE_COMPRESSION_VAR      "tw_use_compression"
#define TW_FILENAME                 "tw_filename"
#define TW_ZIP_INDEX                "tw_zip_index"
#define TW_ZIP_QUEUE_COUNT       "tw_zip_queue_count"

#define MAX_BACKUP_NAME_LEN 64
#define TW_BACKUP_TEXT              "tw_backup_text"
#define TW_BACKUP_NAME		        "tw_backup_name"
#define TW_BACKUP_SYSTEM_VAR        "tw_backup_system"
#define TW_BACKUP_DATA_VAR          "tw_backup_data"
#define TW_BACKUP_BOOT_VAR          "tw_backup_boot"
#define TW_BACKUP_RECOVERY_VAR      "tw_backup_recovery"
#define TW_BACKUP_CACHE_VAR         "tw_backup_cache"
#define TW_BACKUP_ANDSEC_VAR        "tw_backup_andsec"
#define TW_BACKUP_SDEXT_VAR         "tw_backup_sdext"
#define TW_BACKUP_AVG_IMG_RATE      "tw_backup_avg_img_rate"
#define TW_BACKUP_AVG_FILE_RATE     "tw_backup_avg_file_rate"
#define TW_BACKUP_AVG_FILE_COMP_RATE    "tw_backup_avg_file_comp_rate"
#define TW_BACKUP_SYSTEM_SIZE       "tw_backup_system_size"
#define TW_BACKUP_DATA_SIZE         "tw_backup_data_size"
#define TW_BACKUP_BOOT_SIZE         "tw_backup_boot_size"
#define TW_BACKUP_RECOVERY_SIZE     "tw_backup_recovery_size"
#define TW_BACKUP_CACHE_SIZE        "tw_backup_cache_size"
#define TW_BACKUP_ANDSEC_SIZE       "tw_backup_andsec_size"
#define TW_BACKUP_SDEXT_SIZE        "tw_backup_sdext_size"
#define TW_STORAGE_FREE_SIZE        "tw_storage_free_size"
#define TW_GENERATE_MD5_TEXT        "tw_generate_md5_text"

#define TW_RESTORE_TEXT             "tw_restore_text"
#define TW_RESTORE_SYSTEM_VAR       "tw_restore_system"
#define TW_RESTORE_DATA_VAR         "tw_restore_data"
#define TW_RESTORE_BOOT_VAR         "tw_restore_boot"
#define TW_RESTORE_RECOVERY_VAR     "tw_restore_recovery"
#define TW_RESTORE_CACHE_VAR        "tw_restore_cache"
#define TW_RESTORE_ANDSEC_VAR       "tw_restore_andsec"
#define TW_RESTORE_SDEXT_VAR        "tw_restore_sdext"
#define TW_RESTORE_AVG_IMG_RATE     "tw_restore_avg_img_rate"
#define TW_RESTORE_AVG_FILE_RATE    "tw_restore_avg_file_rate"
#define TW_RESTORE_AVG_FILE_COMP_RATE    "tw_restore_avg_file_comp_rate"
#define TW_RESTORE_FILE_DATE        "tw_restore_file_date"
#define TW_VERIFY_MD5_TEXT          "tw_verify_md5_text"
#define TW_UPDATE_SYSTEM_DETAILS_TEXT "tw_update_system_details_text"

#define TW_VERSION_VAR              "tw_version"
#define TW_GUI_SORT_ORDER           "tw_gui_sort_order"
#define TW_ZIP_LOCATION_VAR         "tw_zip_location"
#define TW_ZIP_INTERNAL_VAR         "tw_zip_internal"
#define TW_ZIP_EXTERNAL_VAR         "tw_zip_external"
#define TW_FORCE_MD5_CHECK_VAR      "tw_force_md5_check"
#define TW_SKIP_MD5_CHECK_VAR       "tw_skip_md5_check"
#define TW_SKIP_MD5_GENERATE_VAR    "tw_skip_md5_generate"
#define TW_DISABLE_FREE_SPACE_VAR   "tw_disable_free_space"
#define TW_SIGNED_ZIP_VERIFY_VAR    "tw_signed_zip_verify"
#define TW_INSTALL_REBOOT_VAR       "tw_install_reboot"
#define TW_TIME_ZONE_VAR            "tw_time_zone"
#define TW_RM_RF_VAR                "tw_rm_rf"

#define TW_BACKUPS_FOLDER_VAR       "tw_backups_folder"

#define TW_SDEXT_SIZE               "tw_sdext_size"
#define TW_SWAP_SIZE                "tw_swap_size"
#define TW_SDPART_FILE_SYSTEM       "tw_sdpart_file_system"
#define TW_TIME_ZONE_GUISEL         "tw_time_zone_guisel"
#define TW_TIME_ZONE_GUIOFFSET      "tw_time_zone_guioffset"
#define TW_TIME_ZONE_GUIDST         "tw_time_zone_guidst"

#define TW_ACTION_BUSY              "tw_busy"

#define TW_ALLOW_PARTITION_SDCARD   "tw_allow_partition_sdcard"

#define TW_SCREEN_OFF               "tw_screen_off"

#define TW_REBOOT_SYSTEM            "tw_reboot_system"
#define TW_REBOOT_RECOVERY          "tw_reboot_recovery"
#define TW_REBOOT_POWEROFF          "tw_reboot_poweroff"
#define TW_REBOOT_BOOTLOADER        "tw_reboot_bootloader"

#define TW_USE_EXTERNAL_STORAGE     "tw_use_external_storage"
#define TW_HAS_INTERNAL             "tw_has_internal"
#define TW_INTERNAL_PATH            "tw_internal_path"         // /data/media or /internal
#define TW_INTERNAL_MOUNT           "tw_internal_mount"        // /data or /internal
#define TW_INTERNAL_LABEL           "tw_internal_label"        // data or internal
#define TW_HAS_EXTERNAL             "tw_has_external"
#define TW_EXTERNAL_PATH            "tw_external_path"         // /sdcard or /external/sdcard2
#define TW_EXTERNAL_MOUNT           "tw_external_mount"        // /sdcard or /external
#define TW_EXTERNAL_LABEL           "tw_external_label"        // sdcard or external

#define TW_HAS_DATA_MEDIA           "tw_has_data_media"

#define TW_HAS_BOOT_PARTITION       "tw_has_boot_partition"
#define TW_HAS_RECOVERY_PARTITION   "tw_has_recovery_partition"
#define TW_HAS_ANDROID_SECURE       "tw_has_android_secure"
#define TW_HAS_SDEXT_PARTITION      "tw_has_sdext_partition"
#define TW_HAS_USB_STORAGE          "tw_has_usb_storage"
#define TW_NO_BATTERY_PERCENT       "tw_no_battery_percent"
#define TW_POWER_BUTTON             "tw_power_button"
#define TW_SIMULATE_ACTIONS         "tw_simulate_actions"
#define TW_SIMULATE_FAIL            "tw_simulate_fail"
#define TW_DONT_UNMOUNT_SYSTEM      "tw_dont_unmount_system"
// #define TW_ALWAYS_RMRF              "tw_always_rmrf"

#define TW_SHOW_DUMLOCK             "tw_show_dumlock"
#define TW_HAS_INJECTTWRP           "tw_has_injecttwrp"
#define TW_INJECT_AFTER_ZIP         "tw_inject_after_zip"
#define TW_HAS_DATADATA             "tw_has_datadata"
#define TW_FLASH_ZIP_IN_PLACE       "tw_flash_zip_in_place"
#define TW_MIN_SYSTEM_SIZE          "50" // minimum system size to allow a reboot
#define TW_MIN_SYSTEM_VAR           "tw_min_system"
#define TW_DOWNLOAD_MODE            "tw_download_mode"
#define TW_IS_ENCRYPTED             "tw_is_encrypted"
#define TW_IS_DECRYPTED             "tw_is_decrypted"
#define TW_CRYPTO_PWTYPE            "tw_crypto_pwtype"
#define TW_HAS_CRYPTO               "tw_has_crypto"
#define TW_CRYPTO_PASSWORD          "tw_crypto_password"
#define TW_SDEXT_DISABLE_EXT4       "tw_sdext_disable_ext4"
#define TW_MILITARY_TIME            "tw_military_time"

// Also used:
//   tw_boot_is_mountable
//   tw_system_is_mountable
//   tw_data_is_mountable
//   tw_cache_is_mountable
//   tw_sdcext_is_mountable
//   tw_sdcint_is_mountable
//   tw_sd-ext_is_mountable
//   tw_sp1_is_mountable
//   tw_sp2_is_mountable
//   tw_sp3_is_mountable

// Max archive size for tar backups before we split (1.5GB)
#define MAX_ARCHIVE_SIZE 1610612736LLU
//#define MAX_ARCHIVE_SIZE 52428800LLU // 50MB split for testing

#ifndef CUSTOM_LUN_FILE
#define CUSTOM_LUN_FILE "/sys/class/android_usb/android0/f_mass_storage/lun%d/file"
#endif

// For OpenRecoveryScript
#define SCRIPT_FILE_CACHE "/cache/recovery/openrecoveryscript"
#define SCRIPT_FILE_TMP "/tmp/openrecoveryscript"
#define TMP_LOG_FILE "/tmp/recovery.log"

#endif  // _VARIABLES_HEADER_
