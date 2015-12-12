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
#define TW_ADB_CONTROL "/tmp/twadbcontrol"
#define TWRP "TWRP"
#define TWENDADB "twendadb"
#define TWCNT "twcnt"
#define TWFN "twfn"
#define TWIMG "twimg"
#define MD5TRAILER "md5tr"
#define TWMD5 "twmd5"
#define ADB_BACKUP_VERSION 0x01
#define TWERROR "twerror"

//structs for adb backup need to align to 512 bytes for reading 512
//bytes at a time

//generic cmd structure to align fields
struct twcmd {
	char start_of_header[6];
	char type[10];
	uint32_t crc;
	char space[492];
};

//general info for files
struct twfilehdr {
	char start_of_header[6];
	char type[10];
	uint32_t crc;
	uint64_t size;
	uint64_t compressed;
	char name[476];
};

//md5 for files
struct md5trailer {
	char start_of_trailer[6];
	char type[10];
	uint32_t crc;
	char md5[33];
	char space[457];
};

//info for version and number of partitions backed up
struct twheader {
	char start_of_header[6];
	char type[10];
	uint32_t crc;
	uint64_t partition_count;
	uint64_t version;
	char space[476];
};

#endif //__ORSCMD_H
