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

#ifndef __ORSCMD_H
#define __ORSCMD_H

#define ORS_INPUT_FILE "/sbin/orsin"
#define ORS_OUTPUT_FILE "/sbin/orsout"
#define TW_ADB_BACKUP "/tmp/twadbbackup"
#define TW_ADB_RESTORE "/tmp/twadbrestore"
#define TW_ADB_BU_CONTROL "/tmp/twadbbucontrol"
#define TW_ADB_TWRP_CONTROL "/tmp/twadbtwrpcontrol"
#define TWRP "TWRP"
#define TWENDADB "twendadb"
#define TWCNT "twcnt"
#define TWFN "twfn"
#define TWIMG "twimg"
#define TWEOF "tweof"
#define MD5TRAILER "md5tr"
#define TWMD5 "twmd5"
#define TWERROR "twerror"
#define ADB_BACKUP_VERSION 0x01

//structs for adb backup need to align to 512 bytes for reading 512
//bytes at a time
//Each struct contains a crc field so that when we are checking for commands
//and the crc doesn't match we still assume it's data matching the command
//struct but not really a command

//generic cmd structure to align fields for sending commands to and from adb backup
struct twcmd {
	char start_of_header[6];			//stores the magic value #define TWRP
	char type[10];					//stores the type of command, TWENDADB, TWCNT, TWEOF, TWMD5 and TWERROR
	uint32_t crc;					//stores the zlib 32 bit crc of the twcmd struct to allow for making sure we are processing metadata
	char space[492];				//stores space to align the struct to 512 bytes
};

//general info for file metadata stored in adb backup header
struct twfilehdr {
	char start_of_header[6];			//stores the magic value #define TWRP
	char type[10];					//stores the type of file header, TWFN or TWIMG
	uint32_t crc;					//stores the zlib 32 bit crc of the twfilehdr struct to allow for making sure we are processing metadata
	uint64_t size;					//stores the size of the file contained after this header in the backup file
	uint64_t compressed;				//stores whether the file is compressed or not. 1 == compressed and 0 == uncompressed
	char name[476];					//stores the filename of the file
};

//md5 for files stored as a trailer to files in the adb backup file to check
//that they are restored correctly
struct md5trailer {
	char start_of_trailer[6];			//stores the magic value #define TWRP
	char type[10];					//stores the md5trailer type MD5TRAILER
	uint32_t crc;					//stores the zlib 32 bit crc of the md5trailer struct to allow for making sure we are processing metadata
	char md5[33];					//stores the md5 computation of the file
	char space[457];				//stores space to align the struct to 512 bytes
};

//info for version and number of partitions backed up
struct twheader {
	char start_of_header[6];			//stores the magic value #define TWRP
	char type[10];					//stores the twheader value TWCNT
	uint32_t crc;					//stores the zlib 32 bit crc of the twheader struct to allow for making sure we are processing metadata
	uint64_t partition_count;			//stores the number of partitions to restore in the stream
	uint64_t version;				//stores the version of adb backup. increment ADB_BACKUP_VERSION each time the metadata is updated
	char space[476];				//stores space to align the struct to 512 bytes
};

#endif //__ORSCMD_H
