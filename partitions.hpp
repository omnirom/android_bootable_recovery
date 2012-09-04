/* Partition Management classes for TWRP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * The code was written from scratch by Dees_Troy dees_troy at
 * yahoo
 *
 * Copyright (c) 2012
 */

#ifndef __TWRP_Partition_Manager
#define __TWRP_Partition_Manager

#include "data.hpp"
#include <vector>
#include <string>
#include <map>

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
	virtual bool Is_Mounted();                                                // Checks mount to see if the partition is currently mounted
	virtual bool Mount(bool Display_Error);                                   // Mounts the partition if it is not mounted
	virtual bool UnMount(bool Display_Error);                                 // Unmounts the partition if it is mounted
	virtual bool Wipe();                                                      // Wipes the partition
	virtual bool Backup(string backup_folder);                                // Backs up the partition to the folder specified
	virtual bool Restore(string restore_folder);                              // Restores the partition using the backup folder provided
	static string Backup_Method_By_Name();                                    // Returns a string of the backup method for human readable output
	virtual bool Decrypt(string Password);                                    // Decrypts the partition, return 0 for failure and -1 for success
	virtual bool Wipe_Encryption();                                           // Ignores wipe commands for /data/media devices and formats the original block device
	void Check_FS_Type();                                                     // Checks the fs type using blkid, does not do anything on MTD / yaffs2 because this crashes on some devices

protected:
	bool Process_Fstab_Line(string Line);                                     // Processes a fstab line

protected:
	bool Can_Be_Mounted;                                                      // Indicates that the partition can be mounted
	bool Can_Be_Wiped;                                                        // Indicates that the partition can be wiped
	bool Wipe_Available_in_GUI;                                               // Inidcates that the wipe can be user initiated in the GUI system
	bool Is_SubPartition;                                                     // Indicates that this partition is a sub-partition of another partition (e.g. datadata is a sub-partition of data)
	string SubPartition_Of;                                                   // Indicates which partition is the parent partition of this partition (e.g. data is the parent partition of datadata)
	string Symlink_Path;                                                      // Symlink path (e.g. /data/media)
	string Symlink_Mount_Point;                                               // /sdcard could be the symlink mount point for /data/media
	string Mount_Point;                                                       // Mount point for this partition (e.g. /system or /data)
	string Block_Device;                                                      // Block device (e.g. /dev/block/mmcblk1p1)
	string Alternate_Block_Device;                                            // Alternate block device (e.g. /dev/block/mmcblk1)
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
	string Decrypted_Block_Device;                                            // Decrypted block device available after decryption
	string Display_Name;                                                      // Display name for the GUI
	string Backup_Name;                                                       // Backup name -- used for backup filenames
	Backup_Method_enum Backup_Method;                                         // Method used for backup
	bool Has_Data_Media                                                       // Indicates presence of /data/media, may affect wiping and backup methods
	bool Is_Storage                                                           // Indicates if this partition is used for storage for backup, restore, and installing zips
	string Storage_Path                                                       // Indicates the path to the storage -- root indicates mount point, media/ indicates e.g. /data/media
	string Current_File_System                                                // Current file system
	string Fstab_File_System                                                  // File system from the recovery.fstab
	int Format_Block_Size                                                     // Block size for formatting

private:
	bool Wipe_EXT23();                                                        // Formats as ext3 or ext2
	bool Wipe_EXT4();                                                         // Formats using ext4, uses make_ext4fs when present
	bool Wipe_FAT();                                                          // Formats as FAT except that mkdosfs from busybox usually fails so oftentimes this is actually a rm -rf wipe
	bool Wipe_YAFFS2();                                                       // Formats as yaffs2 for MTD memory types
	bool Wipe_RMRF();                                                         // Uses rm -rf to wipe
	bool Wipe_Data_Without_Wiping_Media();                                    // Uses rm -rf to wipe but does not wipe /data/media
	bool Backup_Tar(string backup_folder);                                    // Backs up using tar for file systems
	bool Backup_DD(string backup_folder);                                     // Backs up using dd for emmc memory types
	bool Backup_Dump_Image(string backup_folder);                             // Backs up using dump_image for MTD memory types
	bool Restore_Tar(string restore_folder);                                  // Restore using tar for file systems
	bool Restore_DD(string restore_folder);                                   // Restore using dd for emmc memory types
	bool Restore_Flash_Image(string restore_folder);                          // Restore using flash_image for MTD memory types

friend class TWPartitionManager;
}

class TWPartitionManager
{
public:
	TWPartitionManager();
	virtual ~TWPartitionManager();

public:
	virtual int Process_Fstab(string Fstab_Filename, bool Display_Error);     // Parses the fstab and populates the partitions
	virtual int Mount_By_Path(string Path, bool Display_Error);               // Mounts partition based on path (e.g. /system)
	virtual int Mount_By_Block(string Block, bool Display_Error);             // Mounts partition based on block device (e.g. /dev/block/mmcblk1p1)
	virtual int Mount_By_Name(string Name, bool Display_Error);               // Mounts partition based on display name (e.g. System)
	virtual int UnMount_By_Path(string Path, bool Display_Error);             // Unmounts partition based on path
	virtual int UnMount_By_Block(string Block, bool Display_Error);           // Unmounts partition based on block device
	virtual int UnMount_By_Name(string Name, bool Display_Error);             // Unmounts partition based on display name
	virtual int Is_Mounted_By_Path(string Path);                              // Checks if partition is mounted based on path
	virtual int Is_Mounted_By_Block(string Block);                            // Checks if partition is mounted based on block device
	virtual int Is_Mounted_By_Name(string Name);                              // Checks if partition is mounted based on display name
	static *Partition Find_Partition_By_Path(string Path);                    // Returns a pointer to a partition based on path
	static *Partition Find_Partition_By_Block(string Block);                  // Returns a pointer to a partition based on block device
	virtual int Run_Backup(string Backup_Name);                               // Initiates a backup in the current storage
	virtual int Run_Restore(string Restore_Name);                             // Restores a backup
	void Set_Restore_Files(string Restore_Name);                              // Used to gather a list of available backup partitions for the user to select for a restore
	virtual int Wipe_By_Path(string Path);                                    // Wipes a partition based on path
	virtual int Wipe_By_Block(string Block);                                  // Wipes a partition based on block device
	virtual int Wipe_By_Name(string Name);                                    // Wipes a partition based on display name
	void Refresh_Sizes();                                                     // Refreshes size data of partitions

private:
	std::vector<TWPartition*> Partitions;
}

#endif // __TWRP_Partition_Manager