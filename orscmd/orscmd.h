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
#define TW_ADB_BACKUP "/tmp/twadbbkup"
#define TW_ADB_RESTORE "/tmp/twadbrestore"
#define TW_ADB_CONTROL "/tmp/twadbcontrol"
#define TWEADB "twendadb"
#define TWCNT "twcnt"
#define TWFN "twfn"
#define TWIMG "twimg"
#define MD5TRAILER "md5tr"
#define TWMD5 "twmd5"
#define ADB_BACKUP_VERSION 0x01

//structs for adb backup need to align to 512 bytes

//general info for files
struct twfilehdr {
	char type[8];
	char name[488];
	uint64_t size;
	uint64_t compressed;
};

//md5 for files
struct md5trailer {
	char type[8];
	char md5[504];
};

//info for version and number of partitions backed up
struct twheader {
	char twptcnt[8];
	uint64_t count;
	uint64_t version;
	char space[488];
};

#endif //__ORSCMD_H
