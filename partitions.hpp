/*
	Copyright 2014 to 2017 TeamWin
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

#ifndef __TWRP_Partition_Manager
#define __TWRP_Partition_Manager

#include <map>
#include <vector>
#include <string>
#include <sys/poll.h>
#include "exclude.hpp"
#include "tw_atomic.hpp"
#include "progresstracking.hpp"
#include "ext4crypt_tar.h"

#define MAX_FSTAB_LINE_LENGTH 2048

#define REPACK_ORIG_DIR "/tmp/repackorig/"
#define REPACK_NEW_DIR "/tmp/repacknew/"

using namespace std;

// BasePartition is used for overriding so we can run custom, device
// specific code.
class BasePartition {
	public:
		explicit BasePartition() {}
		virtual ~BasePartition() {}

		virtual bool PreWipeEncryption() {
			return true;
		}

		virtual bool PostWipeEncryption() {
			return true;
		}
};
BasePartition* make_partition();

struct PartitionList {
	std::string Display_Name;
	std::string Mount_Point;
	unsigned int selected;
};

struct Uevent_Block_Data {
	std::string action;
	std::string subsystem;
	std::string block_device;
	std::string type;
	std::string sysfs_path;
	int major;
	int minor;
};

struct Flags_Map {
	std::string Primary_Block_Device;
	std::string Alternate_Block_Device;
	std::string File_System;
	std::string Flags;
	char* fstab_line;
};

enum Repack_Type {
	REPLACE_NONE = 0,
	REPLACE_RAMDISK = 1,
	REPLACE_KERNEL = 2,
};

struct Repack_Options_struct {
	Repack_Type Type;
	bool Backup_First;
	bool Disable_Verity;
	bool Disable_Force_Encrypt;
};

enum PartitionManager_Op {                                                    // PartitionManager Restore Mode for Raw_Read_Write()
	PM_BACKUP = 0,
	PM_RESTORE = 1,
};

class TWPartition;

struct PartitionSettings {                                                    // Settings for backup session
	TWPartition* Part;                                                        // Partition to pass to the partition backup loop
	std::string Backup_Folder;                                                // Path to restore folder
	bool adbbackup;                                                           // tell the system we are backing up over adb
	bool adb_compression;                                                     // 0 == uncompressed, 1 == compressed
	bool generate_digest;                                                      // tell system to create digest for partitions
	bool generate_md5;                                                        // tell system to create md5 for partitions
	uint64_t total_restore_size;                                              // Total size of restored backup
	uint64_t img_bytes_remaining;                                             // remaining img/emmc bytes to backup for progress indicator
	uint64_t file_bytes_remaining;                                            // remaining file bytes to backup for progress indicator
	uint64_t img_time;                                                        // used to calculate how fast we backup images
	uint64_t file_time;                                                       // used to calculate how fast we backup files
	uint64_t img_bytes;                                                       // total image bytes of all emmc partitions
	uint64_t file_bytes;                                                      // total file bytes of all file based partitions
	int partition_count;                                                      // Number of partitions to restore
	ProgressTracking *progress;                                               // Keep track of progress in GUI
	enum PartitionManager_Op PM_Method;                                       // Current operation of backup or restore
};

enum Backup_Method_enum {
	BM_NONE = 0,
	BM_FILES = 1,
	BM_DD = 2,
	BM_FLASH_UTILS = 3,
};

// Partition class
class TWPartition
{
public:
	TWPartition();
	virtual ~TWPartition();

public:
	bool Is_Mounted();                                                        // Checks mount to see if the partition is currently mounted
	bool Is_File_System_Writable();                                           // Checks if the root directory of the file system can be written to
	bool Mount(bool Display_Error);                                           // Mounts the partition if it is not mounted
	bool UnMount(bool Display_Error);                                         // Unmounts the partition if it is mounted
	bool ReMount(bool Display_Error);                                         // Remounts the partition
	bool ReMount_RW(bool Display_Error);                                      // Remounts the partition with read/write access
	bool Wipe(string New_File_System);                                        // Wipes the partition
	bool Wipe();                                                              // Wipes the partition
	bool Wipe_AndSec();                                                       // Wipes android secure
	bool Can_Repair();                                                        // Checks to see if we have everything needed to be able to repair the current file system
	uint64_t Get_Max_FileSize();                                              // get partition maxFileSie
	bool Repair();                                                            // Repairs the current file system
	bool Can_Resize();                                                        // Checks to see if we have everything needed to be able to resize the current file system
	bool Resize();                                                            // Resizes the current file system
	bool Backup(PartitionSettings *part_settings, pid_t *tar_fork_pid);       // Backs up the partition to the folder specified
	bool Restore(PartitionSettings *part_settings);                           // Restores the partition using the backup folder provided
	unsigned long long Get_Restore_Size(PartitionSettings *part_settings);    // Returns the overall restore size of the backup
	string Backup_Method_By_Name();                                           // Returns a string of the backup method for human readable output
	bool Decrypt(string Password);                                            // Decrypts the partition, return 0 for failure and -1 for success
	bool Wipe_Encryption();                                                   // Ignores wipe commands for /data/media devices and formats the original block device
	void Check_FS_Type();                                                     // Checks the fs type using blkid, does not do anything on MTD / yaffs2 because this crashes on some devices
	bool Update_Size(bool Display_Error);                                     // Updates size information
	void Recreate_Media_Folder();                                             // Recreates the /data/media folder
	bool Flash_Image(PartitionSettings *part_settings);                                        // Flashes an image to the partition
	void Change_Mount_Read_Only(bool new_value);                              // Changes Mount_Read_Only to new_value
	bool Is_Read_Only();                                                      // Check if system is read-only in TWRP
	int Check_Lifetime_Writes();
	int Decrypt_Adopted();
	void Revert_Adopted();
	void Partition_Post_Processing(bool Display_Error);                       // Apply partition specific settings after fstab processed
	void Set_Backup_FileName(string fname);                                   // Set Backup_FileName for partition
	string Get_Backup_Name();                                                 // Get Backup_Name for partition
	bool Decrypt_FBE_DE();                                                    // If FBE is present, backup exclusions are set up and DE decrypt is attempted

public:
	string Current_File_System;                                               // Current file system
	string Actual_Block_Device;                                               // Actual block device (one of primary, alternate, or decrypted)
	string Backup_Display_Name;                                               // Name displayed in the partition list for backup selection
	string MTD_Name;                                                          // Name of the partition for MTD devices
	bool Is_Present;                                                          // Indicates if the partition is currently present as a block device
	string Crypto_Key_Location;                                               // Location of the crypto key used for decrypting encrypted data partitions
	unsigned int MTP_Storage_ID;
	string Adopted_GUID;

protected:
	bool Has_Data_Media;                                                      // Indicates presence of /data/media, may affect wiping and backup methods
	void Setup_Data_Media();                                                  // Sets up a partition as a /data/media emulated storage partition

private:
	bool Process_Fstab_Line(const char *fstab_line, bool Display_Error, std::map<string, Flags_Map> *twrp_flags, bool Sar_Detect); // Processes a fstab line
	void Setup_Data_Partition(bool Display_Error);                            // Setup data partition after fstab processed
	void Setup_Cache_Partition(bool Display_Error);                           // Setup cache partition after fstab processed
	bool Find_Wildcard_Block_Devices(const string& Device);                   // Searches for and finds wildcard block devices
	void Find_Actual_Block_Device();                                          // Determines the correct block device and stores it in Actual_Block_Device

	void Apply_TW_Flag(const unsigned flag, const char* str, const bool val); // Apply custom twrp fstab flags
	void Process_TW_Flags(char *flags, bool Display_Error, int fstab_ver);    // Process custom twrp fstab flags
	void Process_FS_Flags(const char *str);                                   // Process standard fstab fs flags
	void Save_FS_Flags(const string& local_File_System, int local_Mount_Flags, const string& local_Mount_Options); // Saves fs flags to a vector in case there are multiple lines in a v2 fstab with different mount flags for different file systems
	bool Is_File_System(string File_System);                                  // Checks to see if the file system given is considered a file system
	bool Is_Image(string File_System);                                        // Checks to see if the file system given is considered an image
	void Setup_File_System(bool Display_Error);                               // Sets defaults for a file system partition
	void Setup_Image();                                                       // Sets defaults for an image partition
	void Setup_AndSec(void);                                                  // Sets up .android_secure settings
	void Find_Real_Block_Device(string& Block_Device, bool Display_Error);    // Checks the block device given and follows symlinks until it gets to the real block device
	unsigned long long IOCTL_Get_Block_Size();                                // Finds the partition size using ioctl
	bool Find_Partition_Size();                                               // Finds the partition size from /proc/partitions
	unsigned long long Get_Size_Via_du(string Path, bool Display_Error);      // Uses du to get sizes
	bool Wipe_EXTFS(string File_System);                                      // Create an ext2/ext3/ext4 filesystem
	bool Wipe_EXT4();                                                         // Formats using ext4, uses make_ext4fs when present
	bool Wipe_FAT();                                                          // Formats as FAT if mkfs.fat exits otherwise rm -rf wipe
	bool Wipe_EXFAT();                                                        // Formats as EXFAT
	bool Wipe_MTD();                                                          // Formats as yaffs2 for MTD memory types
	bool Wipe_RMRF();                                                         // Uses rm -rf to wipe
	bool Wipe_F2FS();                                                         // Uses mkfs.f2fs to wipe
	bool Wipe_NTFS();                                                         // Uses mkntfs to wipe
	bool Wipe_Data_Without_Wiping_Media();                                    // Uses rm -rf to wipe but does not wipe /data/media
	bool Wipe_Data_Without_Wiping_Media_Func(const string& parent);           // Uses rm -rf to wipe but does not wipe /data/media
	bool Recreate_Logs_Dir();                                                 // Recreate TWRP_AB_LOGS_DIR after wipe
	void Wipe_Crypto_Key();                                                   // Wipe crypto key from either footer or block device
	bool Backup_Tar(PartitionSettings *part_settings, pid_t *tar_fork_pid);   // Backs up using tar for file systems
	bool Backup_Image(PartitionSettings *part_settings);                      // Backs up using raw read/write for emmc memory types
	bool Raw_Read_Write(PartitionSettings *part_settings);
	bool Backup_Dump_Image(PartitionSettings *part_settings);                 // Backs up using dump_image for MTD memory types
	string Get_Restore_File_System(PartitionSettings *part_settings);         // Returns the file system that was in place at the time of the backup
	bool Restore_Tar(PartitionSettings *part_settings);                       // Restore using tar for file systems
	bool Restore_Image(PartitionSettings *part_settings);                     // Restore using dd for images
	bool Check_Restore_File_MD5(const string& Filename);                      // Verifies MD5 matches for a file before restoration
	bool Get_Size_Via_statfs(bool Display_Error);                             // Get Partition size, used, and free space using statfs
	bool Get_Size_Via_df(bool Display_Error);                                 // Get Partition size, used, and free space using df command
	bool Make_Dir(string Path, bool Display_Error);                           // Creates a directory if it doesn't already exist
	bool Find_MTD_Block_Device(string MTD_Name);                              // Finds the mtd block device based on the name from the fstab
	void Recreate_AndSec_Folder(void);                                        // Recreates the .android_secure folder
	bool Mount_Storage_Retry(bool Display_Error);                             // Tries multiple times with a half second delay to mount a device in case storage is slow to mount
	bool Is_Sparse_Image(const string& Filename);                             // Determines if a file is in sparse image format
	bool Flash_Sparse_Image(const string& Filename);                          // Flashes a sparse image using simg2img
	bool Flash_Image_FI(const string& Filename, ProgressTracking *progress);  // Flashes an image to the partition using flash_image for mtd nand
	void ExcludeAll(const string& path);                                      // Adds an exclusion for path to both the backup and wipe exclusion lists

private:
	bool Can_Be_Mounted;                                                      // Indicates that the partition can be mounted
	bool Can_Be_Wiped;                                                        // Indicates that the partition can be wiped
	bool Can_Be_Backed_Up;                                                    // Indicates that the partition will show up in the backup list
	bool Use_Rm_Rf;                                                           // Indicates that the partition will always be formatted w/ "rm -rf *"
	bool Wipe_During_Factory_Reset;                                           // Indicates that this partition is wiped during a factory reset
	bool Wipe_Available_in_GUI;                                               // Inidcates that the wipe can be user initiated in the GUI system
	bool Is_SubPartition;                                                     // Indicates that this partition is a sub-partition of another partition (e.g. datadata is a sub-partition of data)
	bool Has_SubPartition;                                                    // Indicates that this partition has a sub-partition
	string SubPartition_Of;                                                   // Indicates which partition is the parent partition of this partition (e.g. /data is the parent partition of /datadata)
	string Symlink_Path;                                                      // Symlink path (e.g. /data/media)
	string Symlink_Mount_Point;                                               // /sdcard could be the symlink mount point for /data/media
	string Mount_Point;                                                       // Mount point for this partition (e.g. /system or /data)
	string Backup_Path;                                                       // Path for backup
	bool Wildcard_Block_Device;                                               // If the block device contains an asterisk, we set this flag
	string Sysfs_Entry;                                                       // For v2 fstab, if the "block device" starts with /devices then it is a sysfs entry that is handled by uevents
	string Primary_Block_Device;                                              // Block device (e.g. /dev/block/mmcblk1p1)
	string Alternate_Block_Device;                                            // Alternate block device (e.g. /dev/block/mmcblk1)
	string Decrypted_Block_Device;                                            // Decrypted block device available after decryption
	bool Removable;                                                           // Indicates if this partition is removable -- affects how often we check overall size, if present, etc.
	int Length;                                                               // Used by make_ext4fs to leave free space at the end of the partition block for things like a crypto footer
	unsigned long long Size;                                                  // Overall size of the partition
	unsigned long long Used;                                                  // Overall used space
	unsigned long long Free;                                                  // Overall free space
	unsigned long long Backup_Size;                                           // Backup size -- may be different than used space especially when /data/media is present
	unsigned long long Restore_Size;                                          // Restore size of the current restore operation
	bool Can_Be_Encrypted;                                                    // This partition might be encrypted, affects error handling, can only be true if crypto support is compiled in
	bool Is_Encrypted;                                                        // This partition is thought to be encrypted -- it wouldn't mount for some reason, only avialble with crypto support
	bool Is_Decrypted;                                                        // This partition has successfully been decrypted
	bool Is_FBE;                                                              // File Based Encryption is present
	bool Mount_To_Decrypt;                                                    // Mount this partition during decrypt (/vendor, /firmware, etc in case we need proprietary libs or firmware files)
	string Display_Name;                                                      // Display name for the GUI
	string Backup_Name;                                                       // Backup name -- used for backup filenames
	string Storage_Name;                                                      // Name displayed in the partition list for storage selection
	string Backup_FileName;                                                   // Actual backup filename
	Backup_Method_enum Backup_Method;                                         // Method used for backup
	bool Can_Encrypt_Backup;                                                  // Indicates if this item can be encrypted during backup
	bool Use_Userdata_Encryption;                                             // Indicates if we will use userdata encryption splitting on an encrypted backup
	bool Has_Android_Secure;                                                  // Indicates the presence of .android_secure on this partition
	bool Is_Storage;                                                          // Indicates if this partition is used for storage for backup, restore, and installing zips
	bool Is_Settings_Storage;                                                 // Indicates that this storage partition is the location of the .twrps settings file and the location that is used for custom themes
	string Storage_Path;                                                      // Indicates the path to the storage -- root indicates mount point, media/ indicates e.g. /data/media
	string Fstab_File_System;                                                 // File system from the recovery.fstab
	int Mount_Flags;                                                          // File system flags from recovery.fstab
	string Mount_Options;                                                     // File system options from recovery.fstab
	unsigned long Format_Block_Size;                                          // Block size for formatting
	bool Ignore_Blkid;                                                        // Ignore blkid results due to superblocks lying to us on certain devices / partitions
	bool Retain_Layout_Version;                                               // Retains the .layout_version file during a wipe (needed on devices like Sony Xperia T where /data and /data/media are separate partitions)
	bool Can_Flash_Img;                                                       // Indicates if this partition can have images flashed to it via the GUI
	bool Mount_Read_Only;                                                     // Only mount this partition as read-only
	bool Is_Adopted_Storage;                                                  // Indicates that this partition is for adopted storage (android_expand)
	bool SlotSelect;                                                          // Partition has A/B slots
	TWExclude backup_exclusions;                                              // Exclusions for file based backups
	TWExclude wipe_exclusions;                                                // Exclusions for file based wipes (data/media devices only)
	string Key_Directory;                                                     // Metadata key directory needed for mounting FBE encrypted data partitions using metadata encryption

	struct partition_fs_flags_struct {                                        // This struct is used to store mount flags and options for different file systems for the same partition
		string File_System;
		int Mount_Flags;
		string Mount_Options;
	};

	std::vector<partition_fs_flags_struct> fs_flags;                          // This vector stores mount flags and options for different file systems for the same partition

friend class TWPartitionManager;
friend class DataManager;
friend class GUIPartitionList;
friend class GUIAction;
friend class PageManager;
};

struct users_struct {
	std::string userId;
	std::string userName;
	int type;
	bool isDecrypted;
};

class TWPartitionManager
{
public:
	TWPartitionManager();                                                     // Constructor for TWRPartionManager
	~TWPartitionManager() {}

public:
	int Process_Fstab(string Fstab_Filename, bool Display_Error, bool Sar_Detect);             // Parses the fstab and populates the partitions
	int Write_Fstab();                                                        // Creates /etc/fstab file that's used by the command line for mount commands
	void Output_Partition_Logging();                                          // Outputs partition information to the log
	void Output_Partition(TWPartition* Part);                                 // Outputs partition details to the log
	int Mount_By_Path(string Path, bool Display_Error);                       // Mounts partition based on path (e.g. /system)
	int UnMount_By_Path(string Path, bool Display_Error);                     // Unmounts partition based on path
	int Is_Mounted_By_Path(string Path);                                      // Checks if partition is mounted based on path
	int Mount_Current_Storage(bool Display_Error);                            // Mounts the current storage location
	int Mount_Settings_Storage(bool Display_Error);                           // Mounts the settings file storage location (usually internal)
	TWPartition* Find_Partition_By_Path(const string& Path);                  // Returns a pointer to a partition based on path
	TWPartition* Find_Partition_By_Block_Device(const string& Block_Device);  // Returns a pointer to a partition based on block device
	int Check_Backup_Name(const std::string& Backup_Name, bool Display_Error, bool Must_Be_Unique); // Checks the current backup name to ensure that it is valid and optionally that a backup with that name doesn't already exist
	int Run_Backup(bool adbbackup);                                           // Initiates a backup in the current storage
	int Run_Restore(const string& Restore_Name);                              // Restores a backup
	bool Write_ADB_Stream_Header(uint64_t partition_count);                   // Write ADB header over twrpbu FIFO
	bool Write_ADB_Stream_Trailer();                                          // Write ADB trailer over twrpbu FIFO
	void Set_Restore_Files(string Restore_Name);                              // Used to gather a list of available backup partitions for the user to select for a restore
	int Wipe_By_Path(string Path);                                            // Wipes a partition based on path
	int Wipe_By_Path(string Path, string New_File_System);                    // Wipes a partition based on path
	int Factory_Reset();                                                      // Performs a factory reset
	int Wipe_Dalvik_Cache();                                                  // Wipes dalvik cache
	int Wipe_Rotate_Data();                                                   // Wipes rotation data --
	int Wipe_Battery_Stats();                                                 // Wipe battery stats -- /data/system/batterystats.bin
	int Wipe_Android_Secure();                                                // Wipes android secure
	int Format_Data();                                                        // Really formats data on /data/media devices -- also removes encryption
	int Wipe_Media_From_Data();                                               // Removes and recreates the media folder on /data/media devices
	int Repair_By_Path(string Path, bool Display_Error);                      // Repairs a partition based on path
	int Resize_By_Path(string Path, bool Display_Error);                      // Resizes a partition based on path
	void Update_System_Details();                                             // Updates fstab, file systems, sizes, etc.
	int Decrypt_Device(string Password, int user_id = 0);                     // Attempt to decrypt any encrypted partitions
	void Parse_Users();                                                       // Parse FBE users
	int usb_storage_enable(void);                                             // Enable USB storage mode
	int usb_storage_disable(void);                                            // Disable USB storage mode
	void Mount_All_Storage(void);                                             // Mounts all storage locations
	void UnMount_Main_Partitions(void);                                       // Unmounts system and data if not data/media and boot if boot is mountable
	int Partition_SDCard(void);                                               // Repartitions the sdcard
	TWPartition *Get_Default_Storage_Partition();                             // Returns a pointer to a default storage partition
	int Check_Backup_Cancel();                                                // Returns the value of stop_backup
	int Cancel_Backup();                                                      // Signals partition backup to cancel
	void Clean_Backup_Folder(string Backup_Folder);                           // Clean Backup Folder on Error
	int Fix_Contexts();
	void Get_Partition_List(string ListType, std::vector<PartitionList> *Partition_List);
	int Fstab_Processed();                                                    // Indicates if the fstab has been processed or not
	void Output_Storage_Fstab();                                              // Creates a /cache/recovery/storage.fstab file with a list of all potential storage locations for app use
	bool Enable_MTP();                                                        // Enables MTP
	void Add_All_MTP_Storage();                                               // Adds all storage objects for MTP
	bool Disable_MTP();                                                       // Disables MTP
	bool Add_MTP_Storage(string Mount_Point);                                 // Adds or removes an MTP Storage partition
	bool Add_MTP_Storage(unsigned int Storage_ID);                            // Adds or removes an MTP Storage partition
	bool Remove_MTP_Storage(string Mount_Point);                              // Adds or removes an MTP Storage partition
	bool Remove_MTP_Storage(unsigned int Storage_ID);                         // Adds or removes an MTP Storage partition
	void Translate_Partition(const char* path, const char* resource_name, const char* default_value);
	void Translate_Partition(const char* path, const char* resource_name, const char* default_value, const char* storage_resource_name, const char* storage_default_value);
	void Translate_Partition(const char* path, const char* resource_name, const char* default_value, const char* storage_resource_name, const char* storage_default_value, const char* backup_name, const char* backup_default);
	void Translate_Partition_Display_Names();                                 // Updates display names based on translations
	bool Decrypt_Adopted();                                                   // Attempt to identy and decrypt any adopted storage partitions
	void Remove_Partition_By_Path(string Path);                               // Removes / erases a partition entry from the partition list

	bool Flash_Image(string& path, string& filename);                         // Flashes an image to a selected partition from the partition list
	bool Restore_Partition(struct PartitionSettings *part_settings);          // Restore the partitions based on type
	TWAtomicInt stop_backup;
	void Set_Active_Slot(const string& Slot);                                 // Sets the active slot to A or B
	string Get_Active_Slot_Suffix();                                          // Returns active slot _a or _b
	string Get_Active_Slot_Display();                                         // Returns active slot A or B for display purposes
	string Get_Android_Root_Path();                                           // Returns path of ANDROID_ROOT environment variable
	struct pollfd uevent_pfd;                                                 // Used for uevent code
	void Remove_Uevent_Devices(const string& sysfs_path);                     // Removes subpartitions from the Partitions vector for a matched uevent device
	void Handle_Uevent(const Uevent_Block_Data& uevent_data);                 // Handle uevent data
	void setup_uevent();                                                      // Opens the uevent netlink socket
	Uevent_Block_Data get_event_block_values(char *buf, int len);             // Scans the buffer from uevent data and loads the appropriate data into a Uevent_Block_Data struct for processing
	void read_uevent();                                                       // Reads uevent data into a buffer
	void close_uevent();                                                      // Closes the uevent netlink socket
	void Add_Partition(TWPartition* Part);                                    // Adds a new partition to the Partitions vector
	bool Prepare_Repack(TWPartition* Part, const std::string& Temp_Folder_Destination, const bool Create_Backup, const std::string& Backup_Name); // Prepares an image for repacking by unpacking it to the temp folder destination
	bool Prepare_Repack(const std::string& Source_Path, const std::string& Temp_Folder_Destination, const bool Copy_Source, const bool Create_Destination = true); // Prepares an image for repacking by unpacking it to the temp folder destination
	bool Repack_Images(const std::string& Target_Image, const struct Repack_Options_struct& Repack_Options); // Repacks the boot image with a new kernel or a new ramdisk
	std::vector<users_struct>* Get_Users_List();                              // Returns pointer to list of users

private:
	void Setup_Settings_Storage_Partition(TWPartition* Part);                 // Sets up settings storage
	void Setup_Android_Secure_Location(TWPartition* Part);                    // Sets up .android_secure if needed
	bool Backup_Partition(struct PartitionSettings *part_settings);           // Backup the partitions based on type
	TWPartition* Find_Partition_By_MTP_Storage_ID(unsigned int Storage_ID);   // Returns a pointer to a partition based on MTP Storage ID
	bool Add_Remove_MTP_Storage(TWPartition* Part, int message_type);         // Adds or removes an MTP Storage partition
	TWPartition* Find_Next_Storage(string Path, bool Exclude_Data_Media);
	int Open_Lun_File(string Partition_Path, string Lun_File);
	void Post_Decrypt(const string& Block_Device);                            // Completes various post-decrypt tasks
	void Coldboot_Scan(std::vector<string> *sysfs_entries, const string& Path, int depth); // Scans subfolders to find matches to the paths stored in sysfs_entries so we can trigger the uevent system to "re-add" devices
	void Coldboot();                                                          // Starts the scan of the /sys/block folder
	bool Prepare_Empty_Folder(const std::string& Folder);                     // Creates an empty folder at Folder. If the folder already exists, the folder is deleted, then created
	pid_t mtppid;
	bool mtp_was_enabled;
	int mtp_write_fd;
	pid_t tar_fork_pid;                                                       // PID of twrpTar fork
	Backup_Method_enum Backup_Method;                                         // Method used for backup
	void Mark_User_Decrypted(int userID);                                     // Marks given user ID in Users_List as decrypted
	void Check_Users_Decryption_Status();                                      // Checks to see if all users are decrypted

private:
	std::vector<TWPartition*> Partitions;                                     // Vector list of all partitions
	string Active_Slot_Display;                                               // Current Active Slot (A or B) for display purposes
	std::vector<users_struct> Users_List;                                     // List of FBE users
};

extern TWPartitionManager PartitionManager;

#endif // __TWRP_Partition_Manager
