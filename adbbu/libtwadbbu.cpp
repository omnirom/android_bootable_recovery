/*
		Copyright 2013 to 2017 TeamWin
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
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <zlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <assert.h>

#include "twadbstream.h"
#include "libtwadbbu.hpp"
#include "twrpback.hpp"

bool twadbbu::Check_ADB_Backup_File(std::string fname) {
	struct AdbBackupStreamHeader adbbuhdr;
	uint32_t crc, adbbuhdrcrc;
	unsigned char buf[MAX_ADB_READ];
	int bytes;

	int fd = open(fname.c_str(), O_RDONLY);
	if (fd < 0) {
		printf("Unable to open %s for reading: %s.\n", fname.c_str(), strerror(errno));
		close(fd);
		return false;
	}
	bytes = read(fd, &buf, sizeof(buf));
	close(fd);

	if (memcpy(&adbbuhdr, buf, sizeof(adbbuhdr)) == NULL) {
		printf("Unable to memcpy: %s (%s).\n", fname.c_str(), strerror(errno));
		return false;
	}
	adbbuhdrcrc = adbbuhdr.crc;
	memset(&adbbuhdr.crc, 0, sizeof(adbbuhdr.crc));
	crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, (const unsigned char*) &adbbuhdr, sizeof(adbbuhdr));

	return (crc == adbbuhdrcrc);
}

std::vector<std::string> twadbbu::Get_ADB_Backup_Files(std::string fname) {
	unsigned char buf[MAX_ADB_READ];
	struct AdbBackupControlType structcmd;
	std::vector<std::string> adb_partitions;

	int fd = open(fname.c_str(), O_RDONLY);
	if (fd < 0) {
		printf("Unable to open %s for reading: %s\n", fname.c_str(), strerror(errno));
		close(fd);
		return std::vector<std::string>();
	}

	while (true) {
		std::string cmdstr;
		int readbytes;
		if ((readbytes = read(fd, &buf, sizeof(buf))) > 0) {
			memcpy(&structcmd, buf, sizeof(structcmd));
			assert(structcmd.type == TWENDADB || structcmd.type == TWIMG || structcmd.type == TWFN);
			cmdstr = structcmd.type;
			std::string cmdtype = cmdstr.substr(0, sizeof(structcmd.type) - 1);
			if (cmdtype == TWENDADB) {
				struct AdbBackupControlType endadb;
				uint32_t crc, endadbcrc;

				memcpy(&endadb, buf, sizeof(endadb));
				endadbcrc = endadb.crc;
				memset(&endadb.crc, 0, sizeof(endadb.crc));
				crc = crc32(0L, Z_NULL, 0);
				crc = crc32(crc, (const unsigned char*) &endadb, sizeof(endadb));

				if (crc == endadbcrc) {
					break;
				}
				else {
					printf("ADB TWENDADB crc header doesn't match\n");
					close(fd);
					return std::vector<std::string>();
				}
			}
			else if (cmdtype == TWIMG || cmdtype == TWFN) {
				struct twfilehdr twfilehdr;
				uint32_t crc, twfilehdrcrc;

				memcpy(&twfilehdr, buf, sizeof(twfilehdr));
				twfilehdrcrc = twfilehdr.crc;
				memset(&twfilehdr.crc, 0, sizeof(twfilehdr.crc));

				crc = crc32(0L, Z_NULL, 0);
				crc = crc32(crc, (const unsigned char*) &twfilehdr, sizeof(twfilehdr));
				if (crc == twfilehdrcrc) {
					std::string adbfile = twfilehdr.name;
					int pos = adbfile.find_last_of("/") + 1;
					adbfile = adbfile.substr(pos, adbfile.size());
					adb_partitions.push_back(adbfile);
				}
				else {
					printf("ADB crc header doesn't match\n");
					close(fd);
					return std::vector<std::string>();
				}
			}
		}
	}
	close(fd);
	return adb_partitions;
}

bool twadbbu::Write_ADB_Stream_Header(uint64_t partition_count) {
	struct AdbBackupStreamHeader twhdr;
	int adb_control_bu_fd;

	memset(&twhdr, 0, sizeof(twhdr));
	adb_control_bu_fd = open(TW_ADB_BU_CONTROL, O_WRONLY | O_NONBLOCK);
	if (adb_control_bu_fd < 0) {
		printf("Cannot write to TW_ADB_BU_CONTROL: %s\n", strerror(errno));
		return false;
	}

	strncpy(twhdr.start_of_header, TWRP, sizeof(twhdr.start_of_header));
	strncpy(twhdr.type, TWSTREAMHDR, sizeof(twhdr.type));
	twhdr.partition_count = partition_count;
	twhdr.version = ADB_BACKUP_VERSION;
	memset(twhdr.space, 0, sizeof(twhdr.space));
	twhdr.crc = crc32(0L, Z_NULL, 0);
	twhdr.crc = crc32(twhdr.crc, (const unsigned char*) &twhdr, sizeof(twhdr));
	if (write(adb_control_bu_fd, &twhdr, sizeof(twhdr)) < 0) {
		printf("Cannot write to adb control channel: %s\n", strerror(errno));
		close(adb_control_bu_fd);
		return false;
	}
	return true;
}

bool twadbbu::Write_ADB_Stream_Trailer() {
	int adb_control_bu_fd;
	struct AdbBackupControlType endadb;

	memset(&endadb, 0, sizeof(endadb));

	adb_control_bu_fd = open(TW_ADB_BU_CONTROL, O_WRONLY);
	if (adb_control_bu_fd < 0) {
		printf("Error opening adb_control_bu_fd: %s\n", strerror(errno));
		return false;
	}
	strncpy(endadb.start_of_header, TWRP, sizeof(endadb.start_of_header));
	strncpy(endadb.type, TWENDADB, sizeof(endadb.type));
	endadb.crc = crc32(0L, Z_NULL, 0);
	endadb.crc = crc32(endadb.crc, (const unsigned char*) &endadb, sizeof(endadb));
	if (write(adb_control_bu_fd, &endadb, sizeof(endadb)) < 0) {
		printf("Cannot write to ADB control: %s\n", strerror(errno));
		close(adb_control_bu_fd);
		return false;
	}
	close(adb_control_bu_fd);
	return true;
}

bool twadbbu::Write_TWFN(std::string Backup_FileName, uint64_t file_size, bool use_compression) {
	int adb_control_bu_fd;
	adb_control_bu_fd = open(TW_ADB_BU_CONTROL, O_WRONLY | O_NONBLOCK);
	struct twfilehdr twfilehdr;
	strncpy(twfilehdr.start_of_header, TWRP, sizeof(twfilehdr.start_of_header));
	strncpy(twfilehdr.type, TWFN, sizeof(twfilehdr.type));
	strncpy(twfilehdr.name, Backup_FileName.c_str(), sizeof(twfilehdr.name));
	twfilehdr.size = (file_size == 0 ? 1024 : file_size);
	twfilehdr.compressed = use_compression;
	twfilehdr.crc = crc32(0L, Z_NULL, 0);
	twfilehdr.crc = crc32(twfilehdr.crc, (const unsigned char*) &twfilehdr, sizeof(twfilehdr));

	printf("Sending TWFN to adb\n");
	if (write(adb_control_bu_fd, &twfilehdr, sizeof(twfilehdr)) < 1) {
		printf("Cannot that write to adb_control_bu_fd: %s\n", strerror(errno));
		close(adb_control_bu_fd);
		return false;
	}
	fsync(adb_control_bu_fd);
	close(adb_control_bu_fd);
	return true;
}

bool twadbbu::Write_TWIMG(std::string Backup_FileName, uint64_t file_size) {
	int adb_control_bu_fd;
	struct twfilehdr twimghdr;

	adb_control_bu_fd = open(TW_ADB_BU_CONTROL, O_WRONLY | O_NONBLOCK);
	strncpy(twimghdr.start_of_header, TWRP, sizeof(twimghdr.start_of_header));
	strncpy(twimghdr.type, TWIMG, sizeof(twimghdr.type));
	twimghdr.size = file_size;
	strncpy(twimghdr.name, Backup_FileName.c_str(), sizeof(twimghdr.name));
	twimghdr.crc = crc32(0L, Z_NULL, 0);
	twimghdr.crc = crc32(twimghdr.crc, (const unsigned char*) &twimghdr, sizeof(twimghdr));
	printf("Sending TWIMG to adb\n");
	if (write(adb_control_bu_fd, &twimghdr, sizeof(twimghdr)) < 1) {
		printf("Cannot write to adb control channel: %s\n", strerror(errno));
		return false;
	}

	return true;
}

bool twadbbu::Write_TWEOF() {
	struct AdbBackupControlType tweof;
	int adb_control_bu_fd;
	int errctr = 0;

	printf("opening TW_ADB_BU_CONTROL\n");
	adb_control_bu_fd = open(TW_ADB_BU_CONTROL, O_WRONLY | O_NONBLOCK);
	while (adb_control_bu_fd < 0) {
		printf("failed to open TW_ADB_BU_CONTROL. Retrying: %s\n", strerror(errno));
		adb_control_bu_fd = open(TW_ADB_BU_CONTROL, O_WRONLY | O_NONBLOCK);
		usleep(10000);
		errctr++;
		if (errctr > ADB_BU_MAX_ERROR) {
			printf("Cannot write to adb_control_bu_fd: %s.\n", strerror(errno));
			close(adb_control_bu_fd);
			return false;
		}
	}
	memset(&tweof, 0, sizeof(tweof));
	strncpy(tweof.start_of_header, TWRP, sizeof(tweof.start_of_header));
	strncpy(tweof.type, TWEOF, sizeof(tweof.type));
	tweof.crc = crc32(0L, Z_NULL, 0);
	tweof.crc = crc32(tweof.crc, (const unsigned char*) &tweof, sizeof(tweof));
	printf("Sending TWEOF to adb backup\n");
	if (write(adb_control_bu_fd, &tweof, sizeof(tweof)) < 0) {
		printf("Cannot write to adb_control_bu_fd: %s.\n", strerror(errno));
		close(adb_control_bu_fd);
		return false;
	}
	close(adb_control_bu_fd);
	return true;
}

bool twadbbu::Write_TWERROR() {
	struct AdbBackupControlType twerror;
	int adb_control_bu_fd = open(TW_ADB_BU_CONTROL, O_WRONLY | O_NONBLOCK);

	strncpy(twerror.start_of_header, TWRP, sizeof(twerror.start_of_header));
	strncpy(twerror.type, TWERROR, sizeof(twerror.type));
	memset(twerror.space, 0, sizeof(twerror.space));
	twerror.crc = crc32(0L, Z_NULL, 0);
	twerror.crc = crc32(twerror.crc, (const unsigned char*) &twerror, sizeof(twerror));
	if (write(adb_control_bu_fd, &twerror, sizeof(twerror)) < 0) {
		printf("Cannot write to adb control channel: %s\n", strerror(errno));
		return false;
	}
	close(adb_control_bu_fd);
	return true;
}

bool twadbbu::Write_TWENDADB() {
	struct AdbBackupControlType endadb;
	int adb_control_bu_fd = open(TW_ADB_BU_CONTROL, O_WRONLY | O_NONBLOCK);

	memset(&endadb, 0, sizeof(endadb));
	strncpy(endadb.start_of_header, TWRP, sizeof(endadb.start_of_header));
	strncpy(endadb.type, TWENDADB, sizeof(endadb.type));
	endadb.crc = crc32(0L, Z_NULL, 0);
	endadb.crc = crc32(endadb.crc, (const unsigned char*) &endadb, sizeof(endadb));

	printf("Sending TWENDADB to ADB Backup\n");
	if (write(adb_control_bu_fd, &endadb, sizeof(endadb)) < 1) {
		printf("Cannot write to ADB_CONTROL_BU_FD: %s\n", strerror(errno));
		return false;
	}

	close(adb_control_bu_fd);
	return true;
}

bool twadbbu::Write_TWDATA(FILE* adbd_fp) {
	struct AdbBackupControlType data_block;
	memset(&data_block, 0, sizeof(data_block));
	strncpy(data_block.start_of_header, TWRP, sizeof(data_block.start_of_header));
	strncpy(data_block.type, TWDATA, sizeof(data_block.type));
	data_block.crc = crc32(0L, Z_NULL, 0);
	data_block.crc = crc32(data_block.crc, (const unsigned char*) &data_block, sizeof(data_block));
	if (fwrite(&data_block, 1, sizeof(data_block), adbd_fp) != sizeof(data_block))  {
		return false;
	}
	return true;
}
