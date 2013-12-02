/*
	Copyright 2012 bigbiff/Dees_Troy TeamWin
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

#include <vector>
#include <string>
#include "twrpDU.hpp"

#define MAX_FSTAB_LINE_LENGTH 2048

using namespace std;

struct PartitionList {
	std::string Display_Name;
	std::string Mount_Point;
	unsigned int selected;
};

// Partition class
class TWPartition
{
public:
	enum Backup_Method_enum {
		NONE = 0,
		FILES = 1,
		DD = 2,
		FLASH_UTILS = 3,
	};

public:
	TWPartition();
	virtual ~TWPartition();

public:
	bool Is_Mounted();                                                        // Checks mount to see if the partition is currently mounted
	bool Mount(bool Display_Error);                                           // Mounts the partition if it is not mounted
	bool UnMount(bool Display_Error);                                         // Unmounts the partition if it is mounted
	bool Wipe(string New_File_System);                                        // Wipes the partition
	bool Wipe();                                                              // Wipes the partition
	bool Wipe_AndSec();                                                       // Wipes android secure
	bool Backup(string backup_folder);                                        // Backs up the partition to the folder specified
	bool Check_MD5(string restore_folder);                                    // Checks MD5 of a backup
	bool Restore(string restore_folder);                                      // Restores the partition using the backup folder provided
	string Backup_Method_By_Name();                                           // Returns a string of the backup method for human readable output
	bool Decrypt(string Password);                                            // Decrypts the partition, return 0 for failure and -1 for success
	bool Wipe_Encryption();                                                   // Ignores wipe commands for /data/media devices and formats the original block device
	void Check_FS_Type();                                                     // Checks the fs type using blkid, does not do anything on MTD / yaffs2 because this crashes on some devices
	bool Update_Size(bool Display_Error);                                     // Updates size information
	void Recreate_Media_Folder();                                             // Recreates the /data/media folder

public:
	string Current_File_System;                                               // Current file system
	string Actual_Block_Device;                                               // Actual block device (one of primary, alternate, or decrypted)
	string MTD_Name;                                                          // Name of the partition for MTD devices

private:
	bool Process_Fstab_Line(string Line, bool Display_Error);                 // Processes a fstab line
	void Find_Actual_Block_Device();                                          // Determines the correct block device and stores it in Actual_Block_Device

	bool Process_Flags(string Flags, bool Display_Error);                     // Process custom fstab flags
	bool Process_FS_Flags(string& Options, int Flags);                        // Process standard fstab fs flags
	bool Is_File_System(string File_System);                                  // Checks to see if the file system given is considered a file system
	bool Is_Image(string File_System);                                        // Checks to see if the file system given is considered an image
	void Setup_File_System(bool Display_Error);                               // Sets defaults for a file system partition
	void Setup_Image(bool Display_Error);                                     // Sets defaults for an image partition
	void Setup_AndSec(void);                                                  // Sets up .android_secure settings
	void Find_Real_Block_Device(string& Block_Device, bool Display_Error);    // Checks the block device given and follows symlinks until it gets to the real block device
	bool Find_Partition_Size();                                               // Finds the partition size from /proc/partitions
	unsigned long long Get_Size_Via_du(string Path, bool Display_Error);      // Uses du to get sizes
	bool Wipe_EXT23(string File_System);                                      // Formats as ext3 or ext2
	bool Wipe_EXT4();                                                         // Formats using ext4, uses make_ext4fs when present
	bool Wipe_FAT();                                                          // Formats as FAT if mkdosfs exits otherwise rm -rf wipe
	bool Wipe_EXFAT();                                                        // Formats as EXFAT
	bool Wipe_MTD();                                                          // Formats as yaffs2 for MTD memory types
	bool Wipe_RMRF();                                                         // Uses rm -rf to wipe
	bool Wipe_F2FS();                                                         // Uses mkfs.f2fs to wipe
	bool Wipe_Data_Without_Wiping_Media();                                    // Uses rm -rf to wipe but does not wipe /data/media
	bool Backup_Tar(string backup_folder);                                    // Backs up using tar for file systems
	bool Backup_DD(string backup_folder);                                     // Backs up using dd for emmc memory types
	bool Backup_Dump_Image(string backup_folder);                             // Backs up using dump_image for MTD memory types
	bool Restore_Tar(string restore_folder, string Restore_File_System);      // Restore using tar for file systems
	bool Restore_DD(string restore_folder);                                   // Restore using dd for emmc memory types
	bool Restore_Flash_Image(string restore_folder);                          // Restore using flash_image for MTD memory types
	bool Get_Size_Via_statfs(bool Display_Error);                             // Get Partition size, used, and free space using statfs
	bool Get_Size_Via_df(bool Display_Error);                                 // Get Partition size, used, and free space using df command
	bool Make_Dir(string Path, bool Display_Error);                           // Creates a directory if it doesn't already exist
	bool Find_MTD_Block_Device(string MTD_Name);                              // Finds the mtd block device based on the name from the fstab
	void Recreate_AndSec_Folder(void);                                        // Recreates the .android_secure folder
	void Mount_Storage_Retry(void);                                           // Tries multiple times with a half second delay to mount a device in case storage is slow to mount

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
	string Primary_Block_Device;                                              // Block device (e.g. /dev/block/mmcblk1p1)
	string Alternate_Block_Device;                                            // Alternate block device (e.g. /dev/block/mmcblk1)
	string Decrypted_Block_Device;                                            // Decrypted block device available after decryption
	bool Removable;                                                           // Indicates if this partition is removable -- affects how often we check overall size, if present, etc.
	bool Is_Present;                                                          // Indicates if the partition is currently present as a block device
	int Length;                                                               // Used by make_ext4fs to leave free space at the end of the partition block for things like a crypto footer
	unsigned long long Size;                                                  // Overall size of the partition
	unsigned long long Used;                                                  // Overall used space
	unsigned long long Free;                                                  // Overall free space
	unsigned long long Backup_Size;                                           // Backup size -- may be different than used space especially when /data/media is present
	bool Can_Be_Encrypted;                                                    // This partition might be encrypted, affects error handling, can only be true if crypto support is compiled in
	bool Is_Encrypted;                                                        // This partition is thought to be encrypted -- it wouldn't mount for some reason, only avialble with crypto support
	bool Is_Decrypted;                                                        // This partition has successfully been decrypted
	string Display_Name;                                                      // Display name for the GUI
	string Backup_Name;                                                       // Backup name -- used for backup filenames
	string Backup_Display_Name;                                               // Name displayed in the partition list for backup selection
	string Storage_Name;                                                      // Name displayed in the partition list for storage selection
	string Backup_FileName;                                                   // Actual backup filename
	Backup_Method_enum Backup_Method;                                         // Method used for backup
	bool Can_Encrypt_Backup;                                                  // Indicates if this item can be encrypted during backup
	bool Use_Userdata_Encryption;                                             // Indicates if we will use userdata encryption splitting on an encrypted backup
	bool Has_Data_Media;                                                      // Indicates presence of /data/media, may affect wiping and backup methods
	bool Has_Android_Secure;                                                  // Indicates the presence of .android_secure on this partition
	bool Is_Storage;                                                          // Indicates if this partition is used for storage for backup, restore, and installing zips
	bool Is_Settings_Storage;                                                 // Indicates that this storage partition is the location of the .twrps settings file and the location that is used for custom themes
	string Storage_Path;                                                      // Indicates the path to the storage -- root indicates mount point, media/ indicates e.g. /data/media
	string Fstab_File_System;                                                 // File system from the recovery.fstab
	int Mount_Flags;                                                          // File system flags from recovery.fstab
	string Mount_Options;                                                     // File system options from recovery.fstab
	int Format_Block_Size;                                                    // Block size for formatting
	bool Ignore_Blkid;                                                        // Ignore blkid results due to superblocks lying to us on certain devices / partitions
	bool Retain_Layout_Version;                                               // Retains the .layout_version file during a wipe (needed on devices like Sony Xperia T where /data and /data/media are separate partitions)
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
	string EcryptFS_Password;                                                 // Have to store the encryption password to remount
#endif

friend class TWPartitionManager;
friend class DataManager;
friend class GUIPartitionList;
};

class TWPartitionManager
{
public:
	TWPartitionManager();													  // Constructor for TWRPartionManager
	~TWPartitionManager() {}

public:
	int Process_Fstab(string Fstab_Filename, bool Display_Error);             // Parses the fstab and populates the partitions
	int Write_Fstab();                                                        // Creates /etc/fstab file that's used by the command line for mount commands
	void Output_Partition_Logging();                                          // Outputs partition information to the log
	int Mount_By_Path(string Path, bool Display_Error);                       // Mounts partition based on path (e.g. /system)
	int Mount_By_Block(string Block, bool Display_Error);                     // Mounts partition based on block device (e.g. /dev/block/mmcblk1p1)
	int Mount_By_Name(string Name, bool Display_Error);                       // Mounts partition based on display name (e.g. System)
	int UnMount_By_Path(string Path, bool Display_Error);                     // Unmounts partition based on path
	int UnMount_By_Block(string Block, bool Display_Error);                   // Unmounts partition based on block device
	int UnMount_By_Name(string Name, bool Display_Error);                     // Unmounts partition based on display name
	int Is_Mounted_By_Path(string Path);                                      // Checks if partition is mounted based on path
	int Is_Mounted_By_Block(string Block);                                    // Checks if partition is mounted based on block device
	int Is_Mounted_By_Name(string Name);                                      // Checks if partition is mounted based on display name
	int Mount_Current_Storage(bool Display_Error);                            // Mounts the current storage location
	int Mount_Settings_Storage(bool Display_Error);                           // Mounts the settings file storage location (usually internal)
	TWPartition* Find_Partition_By_Path(string Path);                         // Returns a pointer to a partition based on path
	TWPartition* Find_Partition_By_Block(string Block);                       // Returns a pointer to a partition based on block device
	TWPartition* Find_Partition_By_Name(string Block);                        // Returns a pointer to a partition based on name
	int Check_Backup_Name(bool Display_Error);                                // Checks the current backup name to ensure that it is valid
	int Run_Backup();                                                         // Initiates a backup in the current storage
	int Run_Restore(string Restore_Name);                                     // Restores a backup
	void Set_Restore_Files(string Restore_Name);                              // Used to gather a list of available backup partitions for the user to select for a restore
	int Wipe_By_Path(string Path);                                            // Wipes a partition based on path
	int Wipe_By_Block(string Block);                                          // Wipes a partition based on block device
	int Wipe_By_Name(string Name);                                            // Wipes a partition based on display name
	int Factory_Reset();                                                      // Performs a factory reset
	int Wipe_Dalvik_Cache();                                                  // Wipes dalvik cache
	int Wipe_Rotate_Data();                                                   // Wipes rotation data --
	int Wipe_Battery_Stats();                                                 // Wipe battery stats -- /data/system/batterystats.bin
	int Wipe_Android_Secure();                                                // Wipes android secure
	int Format_Data();                                                        // Really formats data on /data/media devices -- also removes encryption
	int Wipe_Media_From_Data();                                               // Removes and recreates the media folder on /data/media devices
	void Refresh_Sizes();                                                     // Refreshes size data of partitions
	void Update_System_Details();                                             // Updates fstab, file systems, sizes, etc.
	int Decrypt_Device(string Password);                                      // Attempt to decrypt any encrypted partitions
	int usb_storage_enable(void);                                             // Enable USB storage mode
	int usb_storage_disable(void);                                            // Disable USB storage mode
	void Mount_All_Storage(void);                                             // Mounts all storage locations
	void UnMount_Main_Partitions(void);                                       // Unmounts system and data if not data/media and boot if boot is mountable
	int Partition_SDCard(void);                                               // Repartitions the sdcard

	int Fix_Permissions();
	void Get_Partition_List(string ListType, std::vector<PartitionList> *Partition_List);
	int Fstab_Processed();                                                    // Indicates if the fstab has been processed or not
	void Output_Storage_Fstab();                                              // Creates a /cache/recovery/storage.fstab file with a list of all potential storage locations for app use

private:
	bool Make_MD5(bool generate_md5, string Backup_Folder, string Backup_Filename); // Generates an MD5 after a backup is made
	bool Backup_Partition(TWPartition* Part, string Backup_Folder, bool generate_md5, unsigned long long* img_bytes_remaining, unsigned long long* file_bytes_remaining, unsigned long *img_time, unsigned long *file_time, unsigned long long *img_bytes, unsigned long long *file_bytes);
	bool Restore_Partition(TWPartition* Part, string Restore_Name, int partition_count);
	void Output_Partition(TWPartition* Part);
	int Open_Lun_File(string Partition_Path, string Lun_File);

private:
	std::vector<TWPartition*> Partitions;                                     // Vector list of all partitions
};

extern TWPartitionManager PartitionManager;

#endif // __TWRP_Partition_Manager
