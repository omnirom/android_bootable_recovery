/*
		Copyright TeamWin 2015
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
#include <poll.h>
#include <semaphore.h>
#include <string>
#include <fstream>
#include <sstream>

#include "twadbstream.h"
#include "twrpback.hpp"
#include "../variables.h"
#include "../twcommon.h"
#include "../twrpDigest.hpp"

twrpback::twrpback(void) {
	read_fd = 0;
	write_fd = 0;
	adb_control_twrp_fd = 0;
	adb_control_bu_fd = 0;
	adb_read_fd = 0;
	adb_write_fd = 0;
	ors_fd = 0;
	firstPart = true;
	adbloginit();
}

twrpback::~twrpback(void) {
	adblogfile.close();
}

void twrpback::adbloginit(void) {
	adblogfile.open("/tmp/adb.log", std::fstream::app);
}

void twrpback::adblogwrite(std::string writemsg) {
	adblogfile << writemsg << std::flush;
}

void twrpback::close_backup_fds() {
	close(ors_fd);
	close(write_fd);
	close(adb_read_fd);
	close(adb_control_bu_fd);
	unlink(TW_ADB_BACKUP);
}

void twrpback::close_restore_fds() {
	close(ors_fd);
	close(write_fd);
	close(adb_control_bu_fd);
	close(adb_control_twrp_fd);
	unlink(TW_ADB_RESTORE);
}

int twrpback::backup(std::string command) {
	twrpDigest adb_md5;
	bool breakloop = false;
	int bytes = 0;
	char result[512];
	uint64_t totalbytes = 0;
	uint64_t count = -1;			// Count of how many blocks set
	struct AdbBackupControlType endadb;

	ADBSTRUCT_STATIC_ASSERT(sizeof(endadb) == 512);

	bool writedata = true;
	bool compressed = false;

	if (mkfifo(TW_ADB_BACKUP, 0666) < 0) {
		adblogwrite("Unable to create TW_ADB_BACKUP fifo\n");
		return -1;
	}

	adblogwrite("opening ORS_INPUT_FILE\n");
	write_fd = open(ORS_INPUT_FILE, O_WRONLY);
	if (write_fd < 0) {
		while (write_fd < 0)
			write_fd = open(ORS_INPUT_FILE, O_WRONLY);
	}

	sprintf(operation, "adbbackup %s", command.c_str());
	if (write(write_fd, operation, sizeof(operation)) != sizeof(operation)) {
		adblogwrite("Unable to write to ORS_INPUT_FILE\n");
		close(write_fd);
		unlink(TW_ADB_BACKUP);
		return -1;
	}

	adblogwrite("opening ORS_OUTPUT_FILE\n");
	ors_fd = open(ORS_OUTPUT_FILE, O_RDONLY);
	if (ors_fd < 0) {
		adblogwrite("Unable to open ORS_OUTPUT_FILE\n");
		unlink(TW_ADB_BACKUP);
		return -1;
	}

	memset(&result, 0, sizeof(result));
	memset(&cmd, 0, sizeof(cmd));

	adblogwrite("opening TW_ADB_BU_CONTROL\n");
	adb_control_bu_fd = open(TW_ADB_BU_CONTROL, O_RDONLY | O_NONBLOCK);
	if (adb_control_bu_fd < 0) {
		adblogwrite("Unable to open TW_ADB_BU_CONTROL for reading.\n");
		unlink(TW_ADB_BACKUP);
		close(ors_fd);
		close(write_fd);
		return -1;
	}

	adblogwrite("opening TW_ADB_BACKUP\n");
	adb_read_fd = open(TW_ADB_BACKUP, O_RDONLY | O_NONBLOCK);
	if (adb_read_fd < 0) {
		unlink(TW_ADB_BACKUP);
		close(ors_fd);
		close(write_fd);
		close(adb_control_bu_fd);
		adblogwrite("Unable to open TW_ADB_BACKUP for reading.\n");
		return -1;
	}

	//loop until TWENDADB sent
	while (!breakloop) {
		if (read(adb_control_bu_fd, &cmd, sizeof(cmd)) > 0) {
			struct AdbBackupControlType structcmd;

			memcpy(&structcmd, cmd, sizeof(cmd));
			std::string cmdstr(structcmd.type);

			//we received an error, exit and unlink
			if (cmdstr.substr(0, sizeof(TWERROR)) == TWERROR) {
				writedata = false;
				adblogwrite("Error received. Quitting...\n");
				close_backup_fds();
				return -1;
			}
			//we received the end of adb backup stream so we should break the loop
			else if (cmdstr.substr(0, sizeof(TWENDADB)) == TWENDADB) {
				writedata = false;
				adblogwrite("Writing TWENDADB\n");
				memcpy(&endadb, cmd, sizeof(cmd));
				breakloop = true;
			}
			//we recieved the TWCNT structure metadata to write to adb
			else if (cmdstr.substr(0, sizeof(TWCNT) - 1) == TWCNT) {
				writedata = false;
				adblogwrite("Writing TWCNT\n");
				if (fwrite(cmd, 1, sizeof(cmd), stdout) != sizeof(cmd)) {
					adblogwrite("Error writing TWCNT to stdout\n");
					close_backup_fds();
					return -1;
				}
				fflush(stdout);
			}
			//we will be writing an image from TWRP
			else if (cmdstr.substr(0, sizeof(TWIMG) - 1) == TWIMG) {
				adblogwrite("Writing TWIMG\n");
				adb_md5.initMD5();
				if (fwrite(cmd, 1, sizeof(cmd), stdout)  != sizeof(cmd)) {
					adblogwrite("Error writing TWIMG to stdout\n");
					close_backup_fds();
					return -1;
				}
				fflush(stdout);
				writedata = true;
			}
			//we will be writing a tar from TWRP
			else if (cmdstr.substr(0, sizeof(TWFN) - 1) == TWFN) {
		                struct twfilehdr twfilehdr;

				adblogwrite("Writing TWFN\n");
				adb_md5.initMD5();

				ADBSTRUCT_STATIC_ASSERT(sizeof(twfilehdr) == 512);

				memset(&twfilehdr, 0, sizeof(twfilehdr));
				memcpy(&twfilehdr, cmd, sizeof(cmd));

				compressed = twfilehdr.compressed == 1 ? true: false;

				if (fwrite(cmd, 1, sizeof(cmd), stdout) != sizeof(cmd)) {
					adblogwrite("Error writing TWFN to stdout\n");
					close_backup_fds();
					return -1;
				}
				fflush(stdout);
				writedata = true;
			}
			/*
			We received the command that we are done with the file stream.
			We will flush the remaining data stream.
			Update md5 and write final results to adb stream.
			If we need padding because the total bytes are not a multiple
			of 512, we pad the end with 0s to we reach 512.
			We also write the final md5 to the adb stream.
			*/
			else if (cmdstr.substr(0, sizeof(TWEOF) - 1) == TWEOF) {
				adblogwrite("received TWEOF\n");
				count = totalbytes / 512 + 1;
				count = count * 512;

				while ((bytes = read(adb_read_fd, &result, sizeof(result))) > 0) {
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

				if ((totalbytes % 512) != 0) {
					char padding[count - totalbytes];
					memset(padding, 0, sizeof(padding));
					if (fwrite(padding, 1, sizeof(padding), stdout) != sizeof(padding)) {
						adblogwrite("Error writing padding to stdout\n");
						close_backup_fds();
						return -1;
					}
					adb_md5.updateMD5stream((unsigned char *) padding, sizeof(padding));
					fflush(stdout);
					totalbytes = 0;
				}

				AdbBackupFileTrailer md5trailer;

				memset(&md5trailer, 0, sizeof(md5trailer));
				adb_md5.finalizeMD5stream();

				std::string md5string = adb_md5.createMD5string();

				strncpy(md5trailer.start_of_trailer, TWRP, sizeof(md5trailer.start_of_trailer));
				strncpy(md5trailer.type, MD5TRAILER, sizeof(md5trailer.type));
				strncpy(md5trailer.md5, md5string.c_str(), sizeof(md5trailer.md5));

				md5trailer.crc = crc32(0L, Z_NULL, 0);
				md5trailer.crc = crc32(md5trailer.crc, (const unsigned char*) md5trailer.start_of_trailer, sizeof(md5trailer.start_of_trailer));
				md5trailer.crc = crc32(md5trailer.crc, (const unsigned char*) md5trailer.type, sizeof(md5trailer.type));
				md5trailer.crc = crc32(md5trailer.crc, (const unsigned char*) md5trailer.md5, sizeof(md5trailer.md5));
				md5trailer.crc = crc32(md5trailer.crc, (const unsigned char*) md5trailer.space, sizeof(md5trailer.space));

				if (fwrite(&md5trailer, 1, sizeof(md5trailer), stdout) != sizeof(md5trailer))  {
					adblogwrite("Error writing md5trailer to stdout\n");
					close_backup_fds();
					return -1;
				}
				fflush(stdout);
				writedata = false;
			}
			memset(&cmd, 0, sizeof(cmd));
		}
		//If we are to write data because of a new file stream, lets write all the data.
		//This will allow us to not write data after a command structure has been written
		//to the adb stream.
		//If the stream is compressed, we need to always write the data.
		if (writedata || compressed) {
			while ((bytes = read(adb_read_fd, &result, sizeof(result))) > 0) {
				totalbytes += bytes;
				char *writeresult = new char [bytes];
				memcpy(writeresult, result, bytes);
				if (fwrite(writeresult, 1, bytes, stdout) != bytes) {
					adblogwrite("Error writing backup data to stdout\n");
					close_backup_fds();
					return -1;
				}
				fflush(stdout);
				adb_md5.updateMD5stream((unsigned char *) writeresult, bytes);
				delete [] writeresult;
				memset(&result, 0, sizeof(result));
			}
			compressed = false;
		}
	}

	//Write the final end adb structure to the adb stream
	if (fwrite(&endadb, 1, sizeof(endadb), stdout) != sizeof(endadb)) {
		adblogwrite("Error writing endadb to stdout\n");
		close_backup_fds();
		return -1;
	}
	fflush(stdout);
	close_backup_fds();
	return 0;
}

int twrpback::restore(void) {
	twrpDigest adb_md5;
	char cmd[512];
	char result[512];
	struct AdbBackupControlType structcmd;
	int adb_control_twrp_fd;
	bool writedata, read_from_adb;

	bool breakloop = false;
	read_from_adb = true;

	signal(SIGPIPE, SIG_IGN);

	if(mkfifo(TW_ADB_RESTORE, 0666)) {
		adblogwrite("Unable to create TW_ADB_RESTORE fifo\n");
		return -1;
	}

	adblogwrite("opening ORS_INPUT_FILE\n");
	write_fd = open(ORS_INPUT_FILE, O_WRONLY);

	if (write_fd < 0) {
		while (write_fd < 0)
			write_fd = open(ORS_INPUT_FILE, O_WRONLY);
	}

	sprintf(operation, "adbrestore");
	if (write(write_fd, operation, sizeof(operation)) != sizeof(operation)) {
		adblogwrite("Unable to write to ORS_INPUT_FILE\n");
		close(write_fd);
		unlink(TW_ADB_RESTORE);
		return -1;
	}

	ors_fd = open(ORS_OUTPUT_FILE, O_RDONLY);
	if (ors_fd < 0) {
		adblogwrite("Unable to write to ORS_OUTPUT_FILE\n");
		close(ors_fd);
		unlink(TW_ADB_RESTORE);
		return -1;
	}

	memset(&result, 0, sizeof(result));
	memset(&cmd, 0, sizeof(cmd));

	adblogwrite("opening TW_ADB_BU_CONTROL\n");
	adb_control_bu_fd = open(TW_ADB_BU_CONTROL, O_RDONLY | O_NONBLOCK);
	if (adb_control_bu_fd < 0) {
		stringstream str;
		str << strerror(errno);
		adblogwrite("Unable to open TW_ADB_BU_CONTROL for writing. " + str.str() + "\n");
		close_restore_fds();
		return -1;
	}

	adblogwrite("opening TW_ADB_TWRP_CONTROL\n");
	adb_control_twrp_fd = open(TW_ADB_TWRP_CONTROL, O_WRONLY | O_NONBLOCK);
	if (adb_control_twrp_fd < 0) {
		stringstream str;
		str << strerror(errno);
		adblogwrite("Unable to open TW_ADB_TWRP_CONTROL for writing. " + str.str() + ". Retrying...\n");
		while (adb_control_twrp_fd < 0) {
			adb_control_twrp_fd = open(TW_ADB_TWRP_CONTROL, O_WRONLY | O_NONBLOCK);
		}
	}

	//Loop until we receive TWENDADB from TWRP
	while (breakloop == false) {
		memset(&cmd, 0, sizeof(cmd));
		if (read(adb_control_bu_fd, &cmd, sizeof(cmd)) > 0) {
			struct AdbBackupControlType structcmd;
			memcpy(&structcmd, cmd, sizeof(cmd));
			std::string cmdstr(structcmd.type);

			//If we receive TWEOF from TWRP close adb data fifo
			if (cmdstr.substr(0, sizeof(TWEOF) - 1) == TWEOF) {
				adblogwrite("Received TWEOF\n");
				struct AdbBackupControlType tweof;

				memset(&tweof, 0, sizeof(tweof));
				memcpy(&tweof, result, sizeof(result));
				adblogwrite("closing adb_write_fd\n");
				read_from_adb = true;
			}
			//Break when TWRP sends TWENDADB
			else if (cmdstr.substr(0, sizeof(TWENDADB) - 1) == TWENDADB) {
				adblogwrite("Received TWENDADB\n");
				breakloop = true;
			}
					//we received an error, exit and unlink
			else if (cmdstr.substr(0, sizeof(TWERROR)) == TWERROR) {
				adblogwrite("Error received. Quitting...\n");
				close_restore_fds();
				return -1;
			}
		}


		//If we should read from the adb stream, write commands anddata to TWRP
		if (read_from_adb) {
			if (fread(result, 1, sizeof(result), stdin) > 0) {
				memcpy(&structcmd, result, sizeof(result));
				std::string cmdstr(structcmd.type);

				//Tell TWRP we have read the entire adb stream
				if (cmdstr.substr(0, sizeof(TWENDADB) - 1) == TWENDADB) {
					struct AdbBackupControlType endadb;
					uint32_t crc;

					memset(&endadb, 0, sizeof(endadb));
					memcpy(&endadb, result, sizeof(result));
					crc = crc32(0L, Z_NULL, 0);
					crc = crc32(crc, (const unsigned char*) endadb.start_of_header, sizeof(endadb.start_of_header));
					crc = crc32(crc, (const unsigned char*) endadb.type, sizeof(endadb.type));
					crc = crc32(crc, (const unsigned char*) endadb.space, sizeof(endadb.space));

					if (crc == endadb.crc) {
						adblogwrite("Sending TWENDADB\n");
						if (write(adb_control_twrp_fd, &endadb, sizeof(endadb)) < 1) {
							stringstream str;
							str << strerror(errno);
							adblogwrite("Cannot write to ADB_CONTROL_READ_FD: " + str.str() + "\n");
							close_restore_fds();
							return -1;
						}
					}
					else {
						adblogwrite("ADB TWENDADB crc header doesn't match\n");
						close_restore_fds();
						return -1;
					}
				}
				//Tell TWRP we are sending a partition image
				else if (cmdstr.substr(0, sizeof(TWIMG) - 1) == TWIMG) {
					adblogwrite("Restoring TWIMG\n");
					struct twfilehdr twimghdr;
					uint32_t crc;

					adb_md5.initMD5();
					memset(&twimghdr, 0, sizeof(twimghdr));
					memcpy(&twimghdr, result, sizeof(result));
					crc = crc32(0L, Z_NULL, 0);
					crc = crc32(crc, (const unsigned char*) twimghdr.start_of_header, sizeof(twimghdr.start_of_header));
					crc = crc32(crc, (const unsigned char*) twimghdr.type, sizeof(twimghdr.type));
					crc = crc32(crc, (const unsigned char*) &twimghdr.size, sizeof(twimghdr.size));
					crc = crc32(crc, (const unsigned char*) &twimghdr.compressed, sizeof(twimghdr.compressed));
					crc = crc32(crc, (const unsigned char*) twimghdr.name, sizeof(twimghdr.name));

					if (crc == twimghdr.crc) {
						if (write(adb_control_twrp_fd, result, sizeof(result)) < 1) {
							stringstream str;
							str << strerror(errno);
							adblogwrite("Cannot write to ADB_CONTROL_READ_FD: " + str.str() + "\n");
							close_restore_fds();
							return -1;
						}
					}
					else {
						adblogwrite("ADB TWIMG crc header doesn't match\n");
						close_restore_fds();
						return -1;
					}
					adblogwrite("opening TW_ADB_RESTORE\n");
					adb_write_fd = open(TW_ADB_RESTORE, O_WRONLY);
				}
				//Send TWRP partition metadata
				else if (cmdstr.substr(0, sizeof(TWCNT) - 1) == TWCNT) {
					struct AdbBackupStreamHeader cnthdr;
					uint32_t crc;

					ADBSTRUCT_STATIC_ASSERT(sizeof(cnthdr) == 512);

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
						if (write(adb_control_twrp_fd, result, sizeof(result)) < 0) {
							stringstream str;
							str << strerror(errno);
							adblogwrite("Cannot write to adb_control_twrp_fd: " + str.str() + "\n");
							close_restore_fds();
							return -1;
						}
					}
					else {
						adblogwrite("ADB TWCNT crc header doesn't match\n");
						close_restore_fds();
						return -1;
					}
				}
				//Tell TWRP we are sending a tar stream
				else if (cmdstr.substr(0, sizeof(TWFN) - 1) == TWFN) {
					struct twfilehdr twfilehdr;
					uint32_t crc;

					adb_md5.initMD5();
					adblogwrite("Restoring TWFN\n");
					memset(&twfilehdr, 0, sizeof(twfilehdr));
					memcpy(&twfilehdr, result, sizeof(result));

					crc = crc32(0L, Z_NULL, 0);
					crc = crc32(crc, (const unsigned char*) twfilehdr.start_of_header, sizeof(twfilehdr.start_of_header));
					crc = crc32(crc, (const unsigned char*) twfilehdr.type, sizeof(twfilehdr.type));
					crc = crc32(crc, (const unsigned char*) &twfilehdr.size, sizeof(twfilehdr.size));
					crc = crc32(crc, (const unsigned char*) &twfilehdr.compressed, sizeof(twfilehdr.compressed));
					crc = crc32(crc, (const unsigned char*) twfilehdr.name, sizeof(twfilehdr.name));

					if (crc == twfilehdr.crc) {
						if (write(adb_control_twrp_fd, result, sizeof(result)) < 1) {
							stringstream str;
							str << strerror(errno);
							adblogwrite("Cannot write to adb_control_twrp_fd: " + str.str() + "\n");
							close_restore_fds();
							return -1;
						}
					}
					else {
						adblogwrite("ADB TWFN crc header doesn't match\n");
						close_restore_fds();
						return -1;
					}

					adblogwrite("opening TW_ADB_RESTORE\n");
					adb_write_fd = open(TW_ADB_RESTORE, O_WRONLY);
				}
				//Send the tar or partition image md5 to TWRP
				else if (cmdstr.substr(0, sizeof(MD5TRAILER) - 1) == MD5TRAILER) {
					struct AdbBackupFileTrailer md5tr;
					uint32_t crc;

					ADBSTRUCT_STATIC_ASSERT(sizeof(md5tr) == 512);

					close(adb_write_fd);
					adblogwrite("Restoring MD5TRAILER\n");
					memset(&md5tr, 0, sizeof(md5tr));
					memcpy(&md5tr, result, sizeof(result));
					crc = crc32(0L, Z_NULL, 0);
					crc = crc32(crc, (const unsigned char*) md5tr.start_of_trailer, sizeof(md5tr.start_of_trailer));
					crc = crc32(crc, (const unsigned char*) md5tr.type, sizeof(md5tr.type));
					crc = crc32(crc, (const unsigned char*) md5tr.md5, sizeof(md5tr.md5));
					crc = crc32(crc, (const unsigned char*) md5tr.space, sizeof(md5tr.space));

					if (crc == md5tr.crc) {
						if (write(adb_control_twrp_fd, result, sizeof(result)) < 1) {
							stringstream str;
							str << strerror(errno);
							adblogwrite("Cannot write to adb_control_twrp_fd: " + str.str() + "\n");
							close_restore_fds();
							return -1;
						}
					}
					else {
						adblogwrite("ADB MD5TRAILER crc header doesn't match\n");
						close_restore_fds();
						return -1;
					}
					adb_md5.finalizeMD5stream();

					AdbBackupFileTrailer md5;

					memset(&md5, 0, sizeof(md5));
					strncpy(md5.start_of_trailer, TWRP, sizeof(md5.start_of_trailer));
					strncpy(md5.type, TWMD5, sizeof(md5.type));
					std::string md5string = adb_md5.createMD5string();
					strncpy(md5.md5, md5string.c_str(), sizeof(md5.md5));
					adblogwrite(md5.md5);
					memset(md5.space, 0, sizeof(md5.space));

					adblogwrite("Sending MD5Check\n");
					if (write(adb_control_twrp_fd, &md5, sizeof(md5)) < 1) {
						stringstream str;
						str << strerror(errno);
						adblogwrite("Cannot write to adb_control_twrp_fd: " + str.str() + "\n");
						close_restore_fds();
						return -1;
					}
				}
				else {
					//data, no command to send
					if (write(adb_write_fd, result, sizeof(result)) < 0) {
						stringstream str;
						str << strerror(errno);
						adblogwrite("Cannot write to adb_write_fd\n" + str.str() + ". Retrying.");
						while(write(adb_write_fd, result, sizeof(result)) < 0)
							continue;
					}
					adb_md5.updateMD5stream((unsigned char*)result, sizeof(result));
					memset(&result, 0, sizeof(result));
				}
			}
		}
	}

	close_restore_fds();
	return 0;

}

int main(int argc, char **argv) {
	int index;
	int ret = 0;
	std::string command;
	twrpback tw;

	tw.adblogwrite("Starting adb backup and restore\n");
	if (mkfifo(TW_ADB_BU_CONTROL, 0666) < 0) {
		tw.adblogwrite("Unable to create TW_ADB_BU_CONTROL fifo\n");
		unlink(TW_ADB_BU_CONTROL);
		return -1;
	}
	if (mkfifo(TW_ADB_TWRP_CONTROL, 0666) < 0) {
		tw.adblogwrite("Unable to create TW_ADB_TWRP_CONTROL fifo\n");
		unlink(TW_ADB_TWRP_CONTROL);
		return -1;
	}

	command = argv[1];
	for (index = 2; index < argc; index++) {
		command = command + " " + argv[index];
	}
	tw.adblogwrite(command);
	if (command.substr(0, sizeof("backup") - 1) == "backup") {
		tw.adblogwrite("Starting adb backup\n");
		ret = tw.backup(command);
	}
	else if (command.substr(0, sizeof("restore") - 1) == "restore") {
		tw.adblogwrite("Starting adb restore\n");
		ret = tw.restore();
	}
	if (ret == 0)
		tw.adblogwrite("Adb backup/restore completed\n");
	else
		tw.adblogwrite("Adb backup/restore failed\n");

	if (unlink(TW_ADB_BU_CONTROL) < 0) {
		stringstream str;
		str << strerror(errno);
		tw.adblogwrite("Unable to remove TW_ADB_BU_CONTROL: " + str.str());
	}
	unlink(TW_ADB_TWRP_CONTROL);
	return ret;
}
