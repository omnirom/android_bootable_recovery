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
#include <sys/ioctl.h>
#include <string>

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
	fclose(twadblog);
}

void twrpback::adbloginit(void) {
	twadblog = fopen("/tmp/adb.log", "w");
}

void twrpback::adblogwrite(char* msg) {
	fwrite(msg, sizeof(msg), sizeof(msg), twadblog);
	fflush(twadblog);
}

int twrpback::backup(char* command) {
	twrpDigest adb_md5;
	breakloop = false;
	bool write_fn = false;
	bool write_img = false;
	int bytes = 0, totalbytes = 0;
	int count = -1;


	adb_md5.initMD5();

	if (mkfifo(TW_ADB_BACKUP, 0666) < 0)
		return -1;
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

	ors_fd = open(ORS_OUTPUT_FILE, O_RDONLY);
	if (ors_fd < 0) {
		return -1;
	}

	memset(&result, 0, sizeof(result));
	memset(&result, 0, sizeof(cmd));

	adb_read_fd = open(TW_ADB_BACKUP, O_RDONLY | O_NONBLOCK);
	if (adb_read_fd < 0)
		return -1;
	adb_control_fd = open(TW_ADB_CONTROL, O_RDONLY | O_NONBLOCK);
	if (adb_control_fd < 0)
		return -1;

	while (breakloop == false) {
		write_fn = false;
		write_img = false;
		if (read(adb_control_fd, &cmd, sizeof(cmd)) > 0) {
			std::string cmdstr(cmd);
			if (cmdstr.substr(0, sizeof(TWEADB)) == TWEADB) {
				breakloop = true;
			}
			else if (cmdstr.substr(0, sizeof(TWCNT) - 1) == TWCNT) {
				fwrite(cmd, 1, sizeof(cmd), stdout);
				fflush(stdout);
			}
			else if (cmdstr.substr(0, sizeof(TWIMG) - 1) == TWIMG) {
				write_img = true;
				fwrite(cmd, 1, sizeof(cmd), stdout);
				fflush(stdout);
			}
			else if (cmdstr.substr(0, sizeof(TWFN) - 1) == TWFN) {
				write_fn = true;
				fwrite(cmd, 1, sizeof(cmd), stdout);
				fflush(stdout);
			}
			memset(&cmd, 0, sizeof(cmd));
		}


		if ((bytes = read(adb_read_fd, &result, sizeof(result))) > 0) {
			totalbytes += bytes;
			char *writeresult = new char [bytes];
			memcpy(writeresult, result, bytes);
			fwrite(writeresult, 1, bytes, stdout);
			fflush(stdout);
			adb_md5.updateMD5stream((unsigned char *) writeresult, bytes);
			delete [] writeresult;
			memset(&result, 0, sizeof(result));
		}
	}

	count = totalbytes / 512 + 1;
	count = count * 512;
	char padding[count - totalbytes];
	memset(padding, 0, sizeof(padding));
	fwrite(padding, 1, sizeof(padding), stdout);
	fflush(stdout);

	adb_md5.updateMD5stream((unsigned char *) padding, sizeof(padding));
	adb_md5.finalizeMD5stream();
	adb_md5.createMD5string();

	md5trailer imgmd5;
	strncpy(imgmd5.type, MD5TRAILER, sizeof(imgmd5.type));
	strncpy(imgmd5.md5, adb_md5.md5string.c_str(), sizeof(imgmd5.md5));
	fwrite(&imgmd5, 1, sizeof(imgmd5), stdout);
	fflush(stdout);
	strncpy(cmd, TWEADB, sizeof(cmd));
	fwrite(cmd, 1, sizeof(cmd), stdout);
	fflush(stdout);
	close(write_fd);
	close(adb_read_fd);
	close(adb_control_fd);
	close(ors_fd);
	//fclose(twbkfile);
	return 0;
}

int twrpback::restore(void) {
	twrpDigest adb_md5;
	breakloop = false;

	//FILE* twbkfile = fopen("/tmp/adb.log", "w");

	adb_md5.initMD5();
	mkfifo(TW_ADB_RESTORE, 0666);
	mkfifo(TW_ADB_CONTROL, 0666);

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

	adb_control_fd = open(TW_ADB_CONTROL, O_WRONLY);
	//if (adb_control_fd < 0)
	//	  return -1;
	adb_write_fd = open(TW_ADB_RESTORE, O_WRONLY);

	if (adb_write_fd < 0) {
		while (adb_write_fd < 0)
			adb_write_fd = open(TW_ADB_RESTORE, O_WRONLY | O_NONBLOCK);
	}

	while (breakloop == false) {
		if (fread(result, 1, sizeof(result), stdin) > 0) {
			std::string cmdstr(result);
			if (cmdstr.substr(0, sizeof(TWEADB) - 1) == TWEADB) {
				adb_md5.finalizeMD5stream();
				adb_md5.createMD5string();
				md5trailer md5;
				strncpy(md5.type, TWMD5, sizeof(md5.type));
				strncpy(md5.md5, adb_md5.md5string.c_str(), adb_md5.md5string.size());
				int bytes = 0;
				if (bytes = write(adb_control_fd, &md5, sizeof(md5)) < 1) {
					return -1;
				}
				strncpy(cmd, TWEADB, sizeof(result));
				write(adb_control_fd, cmd, sizeof(cmd));
				break;
			}
			else if (cmdstr.substr(0, sizeof(TWIMG) - 1) == TWIMG) {
				adblogwrite("TWIMG\n");
				write(adb_control_fd, result, sizeof(result));
			}
			else if (cmdstr.substr(0, sizeof(TWCNT) - 1) == TWCNT) {
				adblogwrite("TWCNT\n");
				write(adb_control_fd, result, sizeof(result));
			}
			else if (cmdstr.substr(0, sizeof(TWFN) - 1) == TWFN) {
				adblogwrite("TWFN\n");
				write(adb_control_fd, result, sizeof(result));
			}
			else if (cmdstr.substr(0, sizeof(MD5TRAILER) - 1) == MD5TRAILER) {
				adblogwrite("MD5TRAILER\n");
				write(adb_control_fd, result, sizeof(result));
				adblogwrite("MD5TRAILER2\n");
			}
			else {
				write(adb_write_fd, result, sizeof(result));
					adb_md5.updateMD5stream((unsigned char*)result, sizeof(result));
				memset(&result, 0, sizeof(result));
			}
		}
	}


	int bytes = -1, err = 0;
	while (true) {
		ioctl(adb_write_fd, FIONREAD, &bytes);
		if (bytes == 0)
			break;
	}
	close(write_fd);
	close(adb_write_fd);
	close(adb_control_fd);
	close(ors_fd);
	//fclose(twbkfile);
	return 0;

}

int main(int argc, char **argv) {
	int index;
	int nbytes, ret;
	char command[1024], result[512], operation[1024], cmd[512];
	twrpback tw;

	if (mkfifo(TW_ADB_CONTROL, 0666) < 0)
		return -1;

	sprintf(command, "%s", argv[1]);
	for (index = 2; index < argc; index++) {
		sprintf(command, "%s %s", command, argv[index]);
	}

	if (strncmp(command, "backup", 6) == 0) {
		tw.backup(command);
	}
	else if (strcmp(command, "restore") == 0) {
		tw.restore();
	}
	if (unlink(TW_ADB_BACKUP) < 0)
		return -1;
	if (unlink(TW_ADB_CONTROL) < 0)
		return -1;
}
