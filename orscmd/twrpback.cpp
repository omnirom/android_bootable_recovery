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
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <zlib.h>
#include <string>
#include <fstream>
#include <sstream>

#include "orscmd.h"
#include "twrpback.hpp"
#include "../variables.h"
#include "../twcommon.h"
#include "../twrpDigest.hpp"

twrpback::twrpback(void) {
	read_fd = 0;
	write_fd = 0;
	adb_control_fd = 0;
	adb_read_fd = 0;
	adb_write_fd = 0;
	ors_fd = 0;
	adbloginit();
}

twrpback::~twrpback(void) {
	adblogfile.close();
}

void twrpback::adbloginit(void) {
	adblogfile.open("/tmp/adb.log");
}

void twrpback::adblogwrite(std::string writemsg) {
	adblogfile << writemsg << std::flush;
}

int twrpback::backup(char* command) {
	twrpDigest adb_md5;
	breakloop = false;
	int bytes = 0;
	uint64_t totalbytes = 0;
	int count = -1;
	struct twcmd endadb;

	adb_md5.initMD5();

	if (mkfifo(TW_ADB_BACKUP, 0666) < 0)
		return -1;
	
	adblogwrite("opening ORS_INPUT_FILE\n");
	write_fd = open(ORS_INPUT_FILE, O_WRONLY);
	if (write_fd < 0) {
		while (write_fd < 0)
			write_fd = open(ORS_INPUT_FILE, O_WRONLY);
	}
	
	sprintf(operation, "adbbackup %s", command);
	if (write(write_fd, operation, sizeof(operation)) != sizeof(operation)) {
		close(write_fd);
		return -1;
	}

	adblogwrite("opening ORS_OUTPUT_FILE\n");
	ors_fd = open(ORS_OUTPUT_FILE, O_RDONLY);
	if (ors_fd < 0) {
		return -1;
	}

	memset(&result, 0, sizeof(result));
	memset(&cmd, 0, sizeof(cmd));

	adblogwrite("opening TW_ADB_CONTROL\n");
	adb_control_fd = open(TW_ADB_CONTROL, O_RDONLY | O_NONBLOCK);
	if (adb_control_fd < 0) {
		adblogwrite("Unable to open TW_ADB_CONTROL for reading.\n");
		return -1;
	}

	adblogwrite("opening TW_ADB_BACKUP\n");
	adb_read_fd = open(TW_ADB_BACKUP, O_RDONLY | O_NONBLOCK);
	if (adb_read_fd < 0) {
		adblogwrite("Unable to open TW_ADB_BACKUP for reading.\n");
		return -1;
	}

	while (breakloop == false) {
		/*if (read(ors_fd, &cmd, sizeof(cmd)) > 0) {
			std::string checkstr(cmd);
			if (checkstr.substr(0, sizeof(TWERROR)) == TWERROR) {
				adblogwrite("Error received. Quitting...\n");
				return -1;
			}
		}
		*/
		if (read(adb_control_fd, &cmd, sizeof(cmd)) > 0) {
			struct twcmd structcmd;
			memcpy(&structcmd, cmd, sizeof(cmd));
			std::string cmdstr(structcmd.type);
			if (cmdstr.substr(0, sizeof(TWENDADB)) == TWENDADB) {
				adblogwrite("Writing TWENDADB\n");
				memset(&endadb, 0, sizeof(endadb));
				memcpy(&endadb, cmd, sizeof(cmd));
				breakloop = true;
			}
			else if (cmdstr.substr(0, sizeof(TWCNT) - 1) == TWCNT) {
				adblogwrite("Writing TWCNT\n");
				if (fwrite(cmd, 1, sizeof(cmd), stdout) != sizeof(cmd)) {
					adblogwrite("Error writing TWCNT to stdout\n");
					return -1;
				}
				fflush(stdout);
			}
			else if (cmdstr.substr(0, sizeof(TWIMG) - 1) == TWIMG) {
				adblogwrite("Writing TWIMG\n");
				if (fwrite(cmd, 1, sizeof(cmd), stdout)  != sizeof(cmd)) {
					adblogwrite("Error writing TWIMG to stdout\n");
					return -1;
				}
				fflush(stdout);
			}
			else if (cmdstr.substr(0, sizeof(TWFN) - 1) == TWFN) {
				adblogwrite("Writing TWFN\n");
				if (fwrite(cmd, 1, sizeof(cmd), stdout) != sizeof(cmd)) {
					adblogwrite("Error writing TWFN to stdout\n");
					return -1;
				}
				fflush(stdout);
			}
			memset(&cmd, 0, sizeof(cmd));
		}
		if ((bytes = read(adb_read_fd, &result, sizeof(result))) > 0) {
			totalbytes += bytes;
			char *writeresult = new char [bytes];
			memcpy(writeresult, result, bytes);
			if (fwrite(writeresult, 1, bytes, stdout) != bytes) {
				adblogwrite("Error writing backup data to stdout\n");
				return -1;
			}
			fflush(stdout);
			adb_md5.updateMD5stream((unsigned char *) writeresult, bytes);
			delete [] writeresult;
			memset(&result, 0, sizeof(result));
		}
	}

	count = totalbytes / 512 + 1;
	count = count * 512;

	if ((count % 512) == 0) {
		char padding[count - totalbytes];
		memset(padding, 0, sizeof(padding));
		if (fwrite(padding, 1, sizeof(padding), stdout) != sizeof(padding)) {
			adblogwrite("Error writing padding to stdout\n");
			return -1;
		}
		fflush(stdout);
		adb_md5.updateMD5stream((unsigned char *) padding, sizeof(padding));
		adb_md5.finalizeMD5stream();
		adb_md5.createMD5string();
	}

	md5trailer imgmd5;
	strncpy(imgmd5.start_of_trailer, TWRP, sizeof(imgmd5.start_of_trailer));
	strncpy(imgmd5.type, MD5TRAILER, sizeof(imgmd5.type));
	strncpy(imgmd5.md5, adb_md5.md5string.c_str(), sizeof(imgmd5.md5));
	memset(imgmd5.space, 0, sizeof(imgmd5.space));

	imgmd5.crc = crc32(0L, Z_NULL, 0);
	imgmd5.crc = crc32(imgmd5.crc, (const unsigned char*) imgmd5.start_of_trailer, sizeof(imgmd5.start_of_trailer));
	imgmd5.crc = crc32(imgmd5.crc, (const unsigned char*) imgmd5.type, sizeof(imgmd5.type));
	imgmd5.crc = crc32(imgmd5.crc, (const unsigned char*) imgmd5.md5, sizeof(imgmd5.md5));
	imgmd5.crc = crc32(imgmd5.crc, (const unsigned char*) imgmd5.space, sizeof(imgmd5.space));

	if (fwrite(&imgmd5, 1, sizeof(imgmd5), stdout) != sizeof(imgmd5))  {
		adblogwrite("Error writing imgmd5 to stdout\n");
		return -1;
	}
	fflush(stdout);
	if (fwrite(&endadb, 1, sizeof(endadb), stdout) != sizeof(endadb)) {
		adblogwrite("Error writing endadb to stdout\n");
		return -1;
	}
	fflush(stdout);
	close(write_fd);
	close(adb_read_fd);
	close(adb_control_fd);
	//close(ors_fd);
	unlink(TW_ADB_BACKUP);
	return 0;
}

int twrpback::restore(void) {
	twrpDigest adb_md5;
	breakloop = false;

	adb_md5.initMD5();

	if(mkfifo(TW_ADB_RESTORE, 0666))
		return -1;

	adblogwrite("opening ORS_INPUT_FILE\n");
	write_fd = open(ORS_INPUT_FILE, O_WRONLY);

	if (write_fd < 0) {
		while (write_fd < 0)
			write_fd = open(ORS_INPUT_FILE, O_WRONLY);
	}

	sprintf(operation, "adbrestore");
	if (write(write_fd, operation, sizeof(operation)) != sizeof(operation)) {
		close(write_fd);
		return -1;
	}

	ors_fd = open(ORS_OUTPUT_FILE, O_RDONLY);
	if (ors_fd < 0) {
		return -1;
	}

	memset(&result, 0, sizeof(result));
	memset(&result, 0, sizeof(cmd));

	adblogwrite("opening TW_ADB_CONTROL\n");
	adb_control_fd = open(TW_ADB_CONTROL, O_WRONLY);

	while (breakloop == false) {
		if (fread(result, 1, sizeof(result), stdin) > 0) {
			struct twcmd structcmd;
			memcpy(&structcmd, result, sizeof(result));
			std::string cmdstr(structcmd.type);
			if (cmdstr.substr(0, sizeof(TWENDADB) - 1) == TWENDADB) {
				struct twcmd endadb;
				uint32_t crc;

				memset(&endadb, 0, sizeof(endadb));
				memcpy(&endadb, result, sizeof(result));
				crc = crc32(0L, Z_NULL, 0);
				crc = crc32(crc, (const unsigned char*) endadb.start_of_header, sizeof(endadb.start_of_header));
				crc = crc32(crc, (const unsigned char*) endadb.type, sizeof(endadb.type));
				crc = crc32(crc, (const unsigned char*) endadb.space, sizeof(endadb.space));

				if (crc == endadb.crc) {
					adb_md5.finalizeMD5stream();
					md5trailer md5;
					strncpy(md5.start_of_trailer, TWRP, sizeof(md5.start_of_trailer));
					strncpy(md5.type, TWMD5, sizeof(md5.type));
					std::string md5string = adb_md5.createMD5string();
					strncpy(md5.md5, md5string.c_str(), sizeof(md5.md5));
					memset(md5.space, 0, sizeof(md5.space));

					adblogwrite("Sending MD5Check\n");
					if (write(adb_control_fd, &md5, sizeof(md5)) < 1) {
						adblogwrite("Cannot write to ADB CONTROL FD\n");
						return -1;
					}

					adblogwrite("Sending TWENDADB\n");
					if (write(adb_control_fd, &endadb, sizeof(endadb)) < 1) {
						adblogwrite("Cannot write to ADB CONTROL FD\n");
						return -1;
					}
					break;
				}
			}
			else if (cmdstr.substr(0, sizeof(TWIMG) - 1) == TWIMG) {
				adblogwrite("Restoring TWIMG\n");
				struct twfilehdr twimghdr;
				uint32_t crc;

				memset(&twimghdr, 0, sizeof(twimghdr));
				memcpy(&twimghdr, result, sizeof(result));
				crc = crc32(0L, Z_NULL, 0);
				crc = crc32(crc, (const unsigned char*) twimghdr.start_of_header, sizeof(twimghdr.start_of_header));
				crc = crc32(crc, (const unsigned char*) twimghdr.type, sizeof(twimghdr.type));
				crc = crc32(crc, (const unsigned char*) &twimghdr.size, sizeof(twimghdr.size));
				crc = crc32(crc, (const unsigned char*) &twimghdr.compressed, sizeof(twimghdr.compressed));
				crc = crc32(crc, (const unsigned char*) twimghdr.name, sizeof(twimghdr.name));

				if (crc == twimghdr.crc) {
					if (write(adb_control_fd, result, sizeof(result)) < 1) {
						adblogwrite("Cannot write to ADB CONTROL FD\n");
						return -1;
					}
				}
			}
			else if (cmdstr.substr(0, sizeof(TWCNT) - 1) == TWCNT) {
				struct twheader cnthdr;
				uint32_t crc;

				memset(&cnthdr, 0, sizeof(cnthdr));
				memcpy(&cnthdr, result, sizeof(result));
				crc = crc32(0L, Z_NULL, 0);
				crc = crc32(crc, (const unsigned char*) cnthdr.start_of_header, sizeof(cnthdr.start_of_header));
				crc = crc32(crc, (const unsigned char*) cnthdr.type, sizeof(cnthdr.type));
				crc = crc32(crc, (const unsigned char*) &cnthdr.partition_count, sizeof(cnthdr.partition_count));
				crc = crc32(crc, (const unsigned char*) &cnthdr.version, sizeof(cnthdr.version));
				crc = crc32(crc, (const unsigned char*) cnthdr.space, sizeof(cnthdr.space));

				if (crc == cnthdr.crc) {
					adblogwrite("Restoring TWCNT\n");
					if (write(adb_control_fd, result, sizeof(result)) < 0) {
						adblogwrite("Cannot write to ADB CONTROL FD\n");
						return -1;
					}
				}
			}
			else if (cmdstr.substr(0, sizeof(TWFN) - 1) == TWFN) {
				adblogwrite("Restoring TWFN\n");

				struct twfilehdr twfilehdr;
				uint32_t crc;
				memset(&twfilehdr, 0, sizeof(twfilehdr));
				memcpy(&twfilehdr, result, sizeof(result));

				crc = crc32(0L, Z_NULL, 0);
				crc = crc32(crc, (const unsigned char*) twfilehdr.start_of_header, sizeof(twfilehdr.start_of_header));
				crc = crc32(crc, (const unsigned char*) twfilehdr.type, sizeof(twfilehdr.type));
				crc = crc32(crc, (const unsigned char*) &twfilehdr.size, sizeof(twfilehdr.size));
				crc = crc32(crc, (const unsigned char*) &twfilehdr.compressed, sizeof(twfilehdr.compressed));
				crc = crc32(crc, (const unsigned char*) twfilehdr.name, sizeof(twfilehdr.name));

				if (crc == twfilehdr.crc) {
					if (write(adb_control_fd, result, sizeof(result)) < 1) {
						adblogwrite("Cannot write to ADB CONTROL FD\n");
						return -1;
					}
				}
			}
			else if (cmdstr.substr(0, sizeof(MD5TRAILER) - 1) == MD5TRAILER) {
				adblogwrite("Restoring MD5TRAILER\n");
				struct md5trailer md5tr;
				uint32_t crc;

				memset(&md5tr, 0, sizeof(md5tr));
				memcpy(&md5tr, result, sizeof(result));
				crc = crc32(0L, Z_NULL, 0);
				crc = crc32(crc, (const unsigned char*) md5tr.start_of_trailer, sizeof(md5tr.start_of_trailer));
				crc = crc32(crc, (const unsigned char*) md5tr.type, sizeof(md5tr.type));
				crc = crc32(crc, (const unsigned char*) md5tr.md5, sizeof(md5tr.md5));
				crc = crc32(crc, (const unsigned char*) md5tr.space, sizeof(md5tr.space));

				if (crc == md5tr.crc) {
					if (write(adb_control_fd, result, sizeof(result)) < 1) {
						adblogwrite("Cannot write to ADB CONTROL FD\n");
						return -1;
					}
				}
			}
			else {
				//data, no command to send
				adb_write_fd = open(TW_ADB_RESTORE, O_WRONLY);
				if (write(adb_write_fd, result, sizeof(result)) < 0) {
					adblogwrite("Cannot write to ADB data FD\n");
					return -1;
				}
				adb_md5.updateMD5stream((unsigned char*)result, sizeof(result));
				memset(&result, 0, sizeof(result));
				close(write_fd);
			}
		}
	}
	close(adb_control_fd);
	//close(ors_fd);
	close(adb_write_fd);
	unlink(TW_ADB_RESTORE);
	return 0;

}

int main(int argc, char **argv) {
	int index;
	int nbytes, ret;
	char command[1024], result[512], operation[1024], cmd[512];
	twrpback tw;

	tw.adblogwrite("Starting adb backup and restore\n");
	if (mkfifo(TW_ADB_CONTROL, 0666) < 0) {
		tw.adblogwrite("Unable to create TW_ADB_CONTROL fifo\n");
		return -1;
	}

	sprintf(command, "%s", argv[1]);
	for (index = 2; index < argc; index++) {
		sprintf(command, "%s %s", command, argv[index]);
	}

	if (strncmp(command, "backup", 6) == 0) {
		tw.adblogwrite("Starting adb backup\n");
		ret = tw.backup(command);
	}
	else if (strcmp(command, "restore") == 0) {
		tw.adblogwrite("Starting adb restore\n");
		ret = tw.restore();
	}
	unlink(TW_ADB_CONTROL);
	return ret;
}
