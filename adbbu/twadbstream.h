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

#ifndef __TWADBSTREAM_H
#define __TWADBSTREAM_H

#define TW_ADB_BACKUP "/tmp/twadbbackup"		//FIFO for adb backup
#define TW_ADB_RESTORE "/tmp/twadbrestore"		//FIFO for adb restore
#define TW_ADB_BU_CONTROL "/tmp/twadbbucontrol"		//FIFO for sending control from TWRP to ADB Backup
#define TW_ADB_TWRP_CONTROL "/tmp/twadbtwrpcontrol"	//FIFO for sending control from ADB Backup to TWRP
#define TWRP "TWRP"					//Magic Value
#define ADB_BU_MAX_ERROR 10				//Max amount of errors for while loops

//ADB Backup Control Commands
#define TWSTREAMHDR "twstreamheader"			//TWRP Parititon Count Control
#define TWFN "twfilename"				//TWRP Filename Control
#define TWIMG "twimage"					//TWRP Image name Control
#define TWEOF "tweof"					//End of File for Image/File
#define MD5TRAILER "md5trailer"				//Image/File MD5 Trailer
#define TWDATA "twdatablock"				// twrp adb backup data block header
#define TWMD5 "twverifymd5"				//This command is compared to the md5trailer by ORS to verify transfer
#define TWENDADB "twendadb"				//End Protocol
#define TWERROR "twerror"				//Send error
#define ADB_BACKUP_VERSION 1				//Backup Version
#define DATA_MAX_CHUNK_SIZE 1048576			//Maximum size between each data header
#define MAX_ADB_READ 512				//align with default tar size for amount to read fom adb stream

/*
structs for adb backup need to align to 512 bytes for reading 512
bytes at a time
Each struct contains a crc field so that when we are checking for commands
and the crc doesn't match we still assume it's data matching the command
struct but not really a command
*/

/*  stream format:
  | TW ADB Backup Header   |
  | TW File Stream Header  |
  | File Data              |
  | File/Image MD5 Trailer |
  | TW File Stream Header  |
  | File Data              |
  | File/Image MD5 Trailer |
  | etc...                 |
*/

//determine whether struct is 512 bytes, if not fail compilation
#define ADBSTRUCT_STATIC_ASSERT(structure) typedef char adb_assertion[( !!(structure) )*2-1 ]

//generic cmd structure to align fields for sending commands to and from adb backup
struct AdbBackupControlType {
	char start_of_header[8];			//stores the magic value #define TWRP
	char type[16];					//stores the type of command, TWENDADB, TWCNT, TWEOF, TWMD5, TWDATA and TWERROR
	uint32_t crc;					//stores the zlib 32 bit crc of the AdbBackupControlType struct to allow for making sure we are processing metadata
	char space[484];				//stores space to align the struct to 512 bytes
};

//general info for file metadata stored in adb backup header
struct twfilehdr {
	char start_of_header[8];			//stores the magic value #define TWRP
	char type[16];					//stores the type of file header, TWFN or TWIMG
	uint64_t size;					//stores the size of the file contained after this header in the backup file
	uint64_t compressed;				//stores whether the file is compressed or not. 1 == compressed and 0 == uncompressed
	uint32_t crc;					//stores the zlib 32 bit crc of the twfilehdr struct to allow for making sure we are processing metadata
	char name[468];					//stores the filename of the file
};

//md5 for files stored as a trailer to files in the adb backup file to check
//that they are restored correctly
struct AdbBackupFileTrailer {
	char start_of_trailer[8];			//stores the magic value #define TWRP
	char type[16];					//stores the AdbBackupFileTrailer type MD5TRAILER
	uint32_t crc;					//stores the zlib 32 bit crc of the AdbBackupFileTrailer struct to allow for making sure we are processing metadata
	uint32_t ident;					//stores crc to determine if header is encapsulated in stream as data
	char md5[40];					//stores the md5 computation of the file
	char space[440];				//stores space to align the struct to 512 bytes
};

//info for version and number of partitions backed up
struct AdbBackupStreamHeader {
	char start_of_header[8];			//stores the magic value #define TWRP
	char type[16];					//stores the AdbBackupStreamHeader value TWCNT
	uint64_t partition_count;			//stores the number of partitions to restore in the stream
	uint64_t version;				//stores the version of adb backup. increment ADB_BACKUP_VERSION each time the metadata is updated
	uint32_t crc;					//stores the zlib 32 bit crc of the AdbBackupStreamHeader struct to allow for making sure we are processing metadata
	char space[468];				//stores space to align the struct to 512 bytes
};

#endif //__TWADBSTREAM_H
