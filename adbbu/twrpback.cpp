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
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <zlib.h>
#include <ctype.h>
#include <semaphore.h>
#include <string>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <utils/threads.h>
#include <pthread.h>

#include "twadbstream.h"
#include "twrpback.hpp"
#include "libtwadbbu.hpp"
#include "../twrpDigest/twrpDigest.hpp"
#include "../twrpDigest/twrpMD5.hpp"
#include "../twrpAdbBuFifo.hpp"

twrpback::twrpback(void) {
	read_fd = 0;
	write_fd = 0;
	adb_control_twrp_fd = 0;
	adb_control_bu_fd = 0;
	adb_read_fd = 0;
	adb_write_fd = 0;
	adb_write_fd = 0;
	ors_fd = 0;
	firstPart = true;
	createFifos();
	adbloginit();
}

twrpback::~twrpback(void) {
	twrpback::adblogfile.close();
	closeFifos();
}

void twrpback::createFifos(void) {
        if (mkfifo(TW_ADB_BU_CONTROL, 0666) < 0) {
                std::stringstream str;
                str << strerror(errno);
                adblogwrite("Unable to create TW_ADB_BU_CONTROL fifo: " + str.str() + "\n");
        }
        if (mkfifo(TW_ADB_TWRP_CONTROL, 0666) < 0) {
                std::stringstream str;
                str << strerror(errno);
                adblogwrite("Unable to create TW_ADB_TWRP_CONTROL fifo: " + str.str() + "\n");
                unlink(TW_ADB_BU_CONTROL);
        }
}

void twrpback::closeFifos(void) {
        if (unlink(TW_ADB_BU_CONTROL) < 0) {
                std::stringstream str;
                str << strerror(errno);
                adblogwrite("Unable to remove TW_ADB_BU_CONTROL: " + str.str());
        }
        if (unlink(TW_ADB_TWRP_CONTROL) < 0) {
                std::stringstream str;
                str << strerror(errno);
                adblogwrite("Unable to remove TW_ADB_TWRP_CONTROL: " + str.str());
	}
}

void twrpback::adbloginit(void) {
	twrpback::adblogfile.open("/tmp/adb.log", std::fstream::app);
}

void twrpback::adblogwrite(std::string writemsg) {
	twrpback::adblogfile << writemsg << std::flush;
}

void twrpback::close_backup_fds() {
	if (ors_fd > 0)
		close(ors_fd);
	if (write_fd > 0)
		close(write_fd);
	if (adb_read_fd > 0)
		close(adb_read_fd);
	if (adb_control_bu_fd > 0)
		close(adb_control_bu_fd);
	if (adbd_fp != NULL)
		fclose(adbd_fp);
	if (access(TW_ADB_BACKUP, F_OK) == 0)
		unlink(TW_ADB_BACKUP);
}

void twrpback::close_restore_fds() {
	if (ors_fd > 0)
		close(ors_fd);
	if (write_fd > 0)
		close(write_fd);
	if (adb_control_bu_fd > 0)
		close(adb_control_bu_fd);
	if (adb_control_twrp_fd > 0)
		close(adb_control_twrp_fd);
	if (adbd_fp != NULL)
		fclose(adbd_fp);
	if (access(TW_ADB_RESTORE, F_OK) == 0)
		unlink(TW_ADB_RESTORE);
}

int twrpback::backup(std::string command) {
	twrpMD5 digest;
	bool breakloop = false;
	int bytes = 0, errctr = 0;
	char adbReadStream[MAX_ADB_READ];
	uint64_t totalbytes = 0, dataChunkBytes = 0;
	int64_t count = -1;			// Count of how many blocks set
	uint64_t md5fnsize = 0;
	struct AdbBackupControlType endadb;
	//bool gzipDataHeader = false;
	FILE* fptest;

	fptest = fopen("/tmp/test.tgz", "w+");

	ADBSTRUCT_STATIC_ASSERT(sizeof(endadb) == MAX_ADB_READ);

	bool writedata = true;
	bool compressed = false;
	bool firstDataPacket = true;

	adbd_fp = fdopen(adbd_fd, "w");
	if (adbd_fp == NULL) {
		adblogwrite("Unable to open adb_fp\n");
		return -1;
	}

	if (mkfifo(TW_ADB_BACKUP, 0666) < 0) {
		adblogwrite("Unable to create TW_ADB_BACKUP fifo\n");
		return -1;
	}

	adblogwrite("opening TW_ADB_FIFO\n");
	write_fd = open(TW_ADB_FIFO, O_WRONLY);
	while (write_fd < 0) {
		write_fd = open(TW_ADB_FIFO, O_WRONLY);
		usleep(10000);
		errctr++;
		if (errctr > ADB_BU_MAX_ERROR) {
			adblogwrite("Unable to open TW_ADB_FIFO\n");
			close_backup_fds();
			return -1;
		}
	}

	memset(operation, 0, sizeof(operation));
	if (snprintf(operation, sizeof(operation), "adbbackup %s", command.c_str()) >= sizeof(operation)) {
		adblogwrite("Operation too big to write to ORS_INPUT_FILE\n");
		close_backup_fds();
		return -1;
	}
	if (write(write_fd, operation, sizeof(operation)) != sizeof(operation)) {
		adblogwrite("Unable to write to ORS_INPUT_FILE\n");
		close_backup_fds();
		return -1;
	}

	memset(&adbReadStream, 0, sizeof(adbReadStream));
	memset(&cmd, 0, sizeof(cmd));

	adblogwrite("opening TW_ADB_BU_CONTROL\n");
	adb_control_bu_fd = open(TW_ADB_BU_CONTROL, O_RDONLY | O_NONBLOCK);
	if (adb_control_bu_fd < 0) {
		adblogwrite("Unable to open TW_ADB_BU_CONTROL for reading.\n");
		close_backup_fds();
		return -1;
	}

	adblogwrite("opening TW_ADB_BACKUP\n");
	adb_read_fd = open(TW_ADB_BACKUP, O_RDONLY | O_NONBLOCK);
	if (adb_read_fd < 0) {
		adblogwrite("Unable to open TW_ADB_BACKUP for reading.\n");
		close_backup_fds();
		return -1;
	}

	//loop until TWENDADB sent
	while (!breakloop) {
		if (read(adb_control_bu_fd, &cmd, sizeof(cmd)) > 0) {
			struct AdbBackupControlType structcmd;

			memcpy(&structcmd, cmd, sizeof(cmd));
			std::string cmdtype = structcmd.get_type();

			//we received an error, exit and unlink
			if (cmdtype == TWERROR) {
				writedata = false;
				adblogwrite("Error received. Quitting...\n");
				close_backup_fds();
				return -1;
			}
			//we received the end of adb backup stream so we should break the loop
			else if (cmdtype == TWENDADB) {
				writedata = false;
				adblogwrite("Recieved TWENDADB\n");
				memcpy(&endadb, cmd, sizeof(cmd));
				std::stringstream str;
				str << totalbytes;
				adblogwrite(str.str() + " total bytes written\n");
				breakloop = true;
			}
			//we recieved the TWSTREAMHDR structure metadata to write to adb
			else if (cmdtype == TWSTREAMHDR) {
				writedata = false;
				adblogwrite("Writing TWSTREAMHDR\n");
				if (fwrite(cmd, 1, sizeof(cmd), adbd_fp) != sizeof(cmd)) {
					std::stringstream str;
					str << strerror(errno);
					adblogwrite("Error writing TWSTREAMHDR to adbd" + str.str() + "\n");
					close_backup_fds();
					return -1;
				}
				fflush(adbd_fp);
			}
			//we will be writing an image from TWRP
			else if (cmdtype == TWIMG) {
				struct twfilehdr twimghdr;

				adblogwrite("Writing TWIMG\n");
				digest.init();
				memset(&twimghdr, 0, sizeof(twimghdr));
				memcpy(&twimghdr, cmd, sizeof(cmd));
				md5fnsize = twimghdr.size;

				if (fwrite(cmd, 1, sizeof(cmd), adbd_fp)  != sizeof(cmd)) {
					adblogwrite("Error writing TWIMG to adbd\n");
					close_backup_fds();
					return -1;
				}
				fflush(adbd_fp);
				writedata = true;
			}
			//we will be writing a tar from TWRP
			else if (cmdtype == TWFN) {
				struct twfilehdr twfilehdr;

				adblogwrite("Writing TWFN\n");
				digest.init();

				ADBSTRUCT_STATIC_ASSERT(sizeof(twfilehdr) == MAX_ADB_READ);

				memset(&twfilehdr, 0, sizeof(twfilehdr));
				memcpy(&twfilehdr, cmd, sizeof(cmd));
				md5fnsize = twfilehdr.size;

				compressed = twfilehdr.compressed == 1 ? true: false;
				//if (compressed) {
					//gzipDataHeader = true;
					//dataChunkBytes = 2;
				//}

				if (fwrite(cmd, 1, sizeof(cmd), adbd_fp) != sizeof(cmd)) {
					adblogwrite("Error writing TWFN to adbd\n");
					close_backup_fds();
					return -1;
				}
				fflush(adbd_fp);
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
			else if (cmdtype == TWEOF) {
				adblogwrite("received TWEOF\n");
				count = totalbytes / MAX_ADB_READ + 1;
				count = count * MAX_ADB_READ;

				while ((bytes = read(adb_read_fd, &adbReadStream, sizeof(adbReadStream))) > 0) {
					totalbytes += bytes;
					char *writeadbReadStream = new char [bytes];
					memcpy(writeadbReadStream, adbReadStream, bytes);
					digest.update((unsigned char *) writeadbReadStream, bytes);
					fwrite(writeadbReadStream, 1, sizeof(writeadbReadStream), fptest);
					if (fwrite(writeadbReadStream, 1, bytes, adbd_fp) != bytes) {
						adblogwrite("Error writing backup data to adbd\n");
						close_backup_fds();
						return -1;
					}
					fflush(adbd_fp);
					delete [] writeadbReadStream;
					memset(&adbReadStream, 0, sizeof(adbReadStream));
				}

				if ((totalbytes % MAX_ADB_READ) != 0) {
					adblogwrite("writing padding to stream\n");
					char padding[count - totalbytes];
					memset(padding, 0, sizeof(padding));
					if (fwrite(padding, 1, sizeof(padding), adbd_fp) != sizeof(padding)) {
						adblogwrite("Error writing padding to adbd\n");
						close_backup_fds();
						return -1;
					}
					digest.update((unsigned char *) padding, sizeof(padding));
					fflush(adbd_fp);
				}

				AdbBackupFileTrailer md5trailer;

				memset(&md5trailer, 0, sizeof(md5trailer));

				std::string md5string = digest.return_digest_string();

				strncpy(md5trailer.start_of_trailer, TWRP, sizeof(md5trailer.start_of_trailer));
				strncpy(md5trailer.type, MD5TRAILER, sizeof(md5trailer.type));
				strncpy(md5trailer.md5, md5string.c_str(), sizeof(md5trailer.md5));

				md5trailer.crc = crc32(0L, Z_NULL, 0);
				md5trailer.crc = crc32(md5trailer.crc, (const unsigned char*) &md5trailer, sizeof(md5trailer));

				md5trailer.ident = crc32(0L, Z_NULL, 0);
				md5trailer.ident = crc32(md5trailer.ident, (const unsigned char*) &md5trailer, sizeof(md5trailer));
				md5trailer.ident = crc32(md5trailer.ident, (const unsigned char*) &md5fnsize, sizeof(md5fnsize));

				if (fwrite(&md5trailer, 1, sizeof(md5trailer), adbd_fp) != sizeof(md5trailer))  {
					adblogwrite("Error writing md5trailer to adbd\n");
					close_backup_fds();
					return -1;
				}
				fflush(adbd_fp);
				writedata = false;
				firstDataPacket = true;
			}
			memset(&cmd, 0, sizeof(cmd));
		}
		//If we are to write data because of a new file stream, lets write all the data.
		//This will allow us to not write data after a command structure has been written
		//to the adb stream.
		//If the stream is compressed, we need to always write the data.
		if (writedata || compressed) {
			while ((bytes = read(adb_read_fd, &adbReadStream, sizeof(adbReadStream))) > 0) {
				if (firstDataPacket) {
					adblogwrite("write twdata\n");
					if (!twadbbu::Write_TWDATA(adbd_fp)) {
						close_backup_fds();
						return -1;
					}
					fflush(adbd_fp);
					firstDataPacket = false;
				}
				char *writeadbReadStream = new char [bytes];
				memcpy(writeadbReadStream, adbReadStream, bytes);

				digest.update((unsigned char *) writeadbReadStream, bytes);

				totalbytes += bytes;
				dataChunkBytes += bytes;

				fwrite(writeadbReadStream, 1, sizeof(writeadbReadStream), fptest);
				adblogwrite("write data\n");
				if (fwrite(writeadbReadStream, 1, bytes, adbd_fp) != bytes) {
					adblogwrite("Error writing backup data to adbd\n");
					close_backup_fds();
					return -1;
				}
				fflush(adbd_fp);

				delete [] writeadbReadStream;
				memset(&adbReadStream, 0, sizeof(adbReadStream));

				if (dataChunkBytes == (DATA_MAX_CHUNK_SIZE - sizeof(adbReadStream))) {
					adblogwrite("stopped at DATA_MAX_CHUNK_SIZE\n");
					dataChunkBytes = 0;
					firstDataPacket = true;
				}
				else if (dataChunkBytes > (DATA_MAX_CHUNK_SIZE - sizeof(adbReadStream))) {
					//1048576
					int bytesLeft = DATA_MAX_CHUNK_SIZE - dataChunkBytes;
					//if (gzipDataHeader) {
					//	bytesLeft = bytesLeft + 2;
					//}

					char extraData[bytesLeft];
					memset(&extraData, 0, sizeof(extraData));
					std::stringstream str;
					str << sizeof(extraData);
					adblogwrite("extraData: " + str.str() + "\n");
					if ((bytes = read(adb_read_fd, &extraData, sizeof(extraData))) > 0) {
						std::stringstream str1;
						std::stringstream str2;
						str1 << bytes;
						adblogwrite("bytes: " + str1.str() + "\n");
						str2 << bytesLeft;
						adblogwrite("byteLefts: " + str2.str() + "\n");
						totalbytes += bytes;
						dataChunkBytes += bytes;
					}

					digest.update((unsigned char *) extraData, bytesLeft);
	
					fwrite(extraData, 1, sizeof(extraData), fptest);
					adblogwrite("write pad data\n");
					if (fwrite(extraData, 1, sizeof(extraData), adbd_fp) < 0) {
						std::stringstream str;
						str << strerror(errno);
						adblogwrite("Cannot write to adbd stream: " + str.str() + "\n");
						close_restore_fds();
						return -1;
					}
					fflush(adbd_fp);
					dataChunkBytes = 0;
					//gzipDataHeader = false;
					firstDataPacket = true;
				}
			}
			//compressed = false;
		}
	}

	//Write the final end adb structure to the adb stream
	if (fwrite(&endadb, 1, sizeof(endadb), adbd_fp) != sizeof(endadb)) {
		adblogwrite("Error writing endadb to adbd\n");
		close_backup_fds();
		return -1;
	}
	fflush(adbd_fp);
	fclose(fptest);
	close_backup_fds();
	return 0;
}

int twrpback::restore(void) {
	twrpMD5 digest;
	char cmd[MAX_ADB_READ];
	char result[MAX_ADB_READ];
	struct AdbBackupControlType structcmd;
	int adb_control_twrp_fd, errctr = 0;
	uint64_t totalbytes = 0, dataChunkBytes = 0;
	uint64_t md5fnsize = 0;
	bool writedata, read_from_adb;
	bool breakloop, eofsent, md5trsent;
	FILE* fptest;

	fptest = fopen("/tmp/test.tgz", "w+");

	breakloop = false;
	read_from_adb = true;

	signal(SIGPIPE, SIG_IGN);

	adbd_fp = fdopen(adbd_fd, "r");
	if (adbd_fp == NULL) {
		adblogwrite("Unable to open adb_fp\n");
		close_restore_fds();
		return -1;
	}

	if(mkfifo(TW_ADB_RESTORE, 0666)) {
		adblogwrite("Unable to create TW_ADB_RESTORE fifo\n");
		close_restore_fds();
		return -1;
	}

	adblogwrite("opening TW_ADB_FIFO\n");
	write_fd = open(TW_ADB_FIFO, O_WRONLY);

	while (write_fd < 0) {
		write_fd = open(TW_ADB_FIFO, O_WRONLY);
		errctr++;
		if (errctr > ADB_BU_MAX_ERROR) {
			adblogwrite("Unable to open TW_ADB_FIFO\n");
			close_restore_fds();
			return -1;
		}
	}

	memset(operation, 0, sizeof(operation));
	sprintf(operation, "adbrestore");
	if (write(write_fd, operation, sizeof(operation)) != sizeof(operation)) {
		adblogwrite("Unable to write to TW_ADB_FIFO\n");
		close_restore_fds();
		return -1;
	}

	memset(&result, 0, sizeof(result));
	memset(&cmd, 0, sizeof(cmd));

	adblogwrite("opening TW_ADB_BU_CONTROL\n");
	adb_control_bu_fd = open(TW_ADB_BU_CONTROL, O_RDONLY | O_NONBLOCK);
	if (adb_control_bu_fd < 0) {
		std::stringstream str;
		str << strerror(errno);
		adblogwrite("Unable to open TW_ADB_BU_CONTROL for writing. " + str.str() + "\n");
		close_restore_fds();
		return -1;
	}

	adblogwrite("opening TW_ADB_TWRP_CONTROL\n");
	adb_control_twrp_fd = open(TW_ADB_TWRP_CONTROL, O_WRONLY | O_NONBLOCK);
	if (adb_control_twrp_fd < 0) {
		std::stringstream str;
		str << strerror(errno);
		adblogwrite("Unable to open TW_ADB_TWRP_CONTROL for writing. " + str.str() + ". Retrying...\n");
		while (adb_control_twrp_fd < 0) {
			adb_control_twrp_fd = open(TW_ADB_TWRP_CONTROL, O_WRONLY | O_NONBLOCK);
			usleep(10000);
			errctr++;
			if (errctr > ADB_BU_MAX_ERROR) {
				adblogwrite("Unable to open TW_ADB_TWRP_CONTROL\n");
				close_backup_fds();
				return -1;
			}
		}
	}

	//Loop until we receive TWENDADB from TWRP
	while (!breakloop) {
		memset(&cmd, 0, sizeof(cmd));
		if (read(adb_control_bu_fd, &cmd, sizeof(cmd)) > 0) {
			struct AdbBackupControlType structcmd;
			memcpy(&structcmd, cmd, sizeof(cmd));
			std::string cmdtype = structcmd.get_type();

			//If we receive TWEOF from TWRP close adb data fifo
			if (cmdtype == TWEOF) {
				adblogwrite("Received TWEOF\n");
				struct AdbBackupControlType tweof;

				memset(&tweof, 0, sizeof(tweof));
				memcpy(&tweof, result, sizeof(result));
				read_from_adb = true;
			}
			//Break when TWRP sends TWENDADB
			else if (cmdtype == TWENDADB) {
				adblogwrite("Received TWENDADB\n");
				breakloop = true;
				close_restore_fds();
			}
			//we received an error, exit and unlink
			else if (cmdtype == TWERROR) {
				adblogwrite("Error received. Quitting...\n");
				close_restore_fds();
				return -1;
			}
		}
		//If we should read from the adb stream, write commands and data to TWRP
		if (read_from_adb) {
			int readbytes;
			if ((readbytes = fread(result, 1, sizeof(result), adbd_fp)) == sizeof(result)) {
				totalbytes += readbytes;
				memcpy(&structcmd, result, sizeof(result));
				std::string cmdtype = structcmd.get_type();

				//Tell TWRP we have read the entire adb stream
				if (cmdtype == TWENDADB) {
					struct AdbBackupControlType endadb;
					uint32_t crc, endadbcrc;

					totalbytes -= sizeof(result);
					memset(&endadb, 0, sizeof(endadb));
					memcpy(&endadb, result, sizeof(result));
					endadbcrc = endadb.crc;
					memset(&endadb.crc, 0, sizeof(endadb.crc));
					crc = crc32(0L, Z_NULL, 0);
					crc = crc32(crc, (const unsigned char*) &endadb, sizeof(endadb));

					if (crc == endadbcrc) {
						adblogwrite("Sending TWENDADB\n");
						if (write(adb_control_twrp_fd, &endadb, sizeof(endadb)) < 1) {
							std::stringstream str;
							str << strerror(errno);
							adblogwrite("Cannot write to ADB_CONTROL_READ_FD: " + str.str() + "\n");
							close_restore_fds();
							return -1;
						}
						read_from_adb = false;
					}
					else {
						adblogwrite("ADB TWENDADB crc header doesn't match\n");
						close_restore_fds();
						return -1;
					}
				}
				//Send TWRP partition metadata
				else if (cmdtype == TWSTREAMHDR) {
					struct AdbBackupStreamHeader cnthdr;
					uint32_t crc, cnthdrcrc;

					ADBSTRUCT_STATIC_ASSERT(sizeof(cnthdr) == MAX_ADB_READ);
					totalbytes -= sizeof(result);

					memset(&cnthdr, 0, sizeof(cnthdr));
					memcpy(&cnthdr, result, sizeof(result));
					cnthdrcrc = cnthdr.crc;
					memset(&cnthdr.crc, 0, sizeof(cnthdr.crc));
					crc = crc32(0L, Z_NULL, 0);
					crc = crc32(crc, (const unsigned char*) &cnthdr, sizeof(cnthdr));

					if (crc == cnthdrcrc) {
						adblogwrite("Restoring TWSTREAMHDR\n");
						if (write(adb_control_twrp_fd, result, sizeof(result)) < 0) {
							std::stringstream str;
							str << strerror(errno);
							adblogwrite("Cannot write to adb_control_twrp_fd: " + str.str() + "\n");
							close_restore_fds();
							return -1;
						}
					}
					else {
						adblogwrite("ADB TWSTREAMHDR crc header doesn't match\n");
						close_restore_fds();
						return -1;
					}
				}
				//Tell TWRP we are sending a partition image
				else if (cmdtype == TWIMG) {
					struct twfilehdr twimghdr;
					uint32_t crc, twimghdrcrc;

					digest.init();
					totalbytes -= sizeof(result);
					adblogwrite("Restoring TWIMG\n");
					memset(&twimghdr, 0, sizeof(twimghdr));
					memcpy(&twimghdr, result, sizeof(result));
					md5fnsize = twimghdr.size;
					twimghdrcrc = twimghdr.crc;
					memset(&twimghdr.crc, 0, sizeof(twimghdr.crc));

					crc = crc32(0L, Z_NULL, 0);
					crc = crc32(crc, (const unsigned char*) &twimghdr, sizeof(twimghdr));
					if (crc == twimghdrcrc) {
						if (write(adb_control_twrp_fd, result, sizeof(result)) < 1) {
							std::stringstream str;
							str << strerror(errno);
							adblogwrite("Cannot write to adb_control_twrp_fd: " + str.str() + "\n");
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
				//Tell TWRP we are sending a tar stream
				else if (cmdtype == TWFN) {
					struct twfilehdr twfilehdr;
					uint32_t crc, twfilehdrcrc;
					digest.init();

					totalbytes -= sizeof(result);
					adblogwrite("Restoring TWFN\n");
					memset(&twfilehdr, 0, sizeof(twfilehdr));
					memcpy(&twfilehdr, result, sizeof(result));
					md5fnsize = twfilehdr.size;
					twfilehdrcrc = twfilehdr.crc;
					memset(&twfilehdr.crc, 0, sizeof(twfilehdr.crc));

					crc = crc32(0L, Z_NULL, 0);
					crc = crc32(crc, (const unsigned char*) &twfilehdr, sizeof(twfilehdr));

					if (crc == twfilehdrcrc) {
						if (write(adb_control_twrp_fd, result, sizeof(result)) < 1) {
							std::stringstream str;
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
				else if (cmdtype == TWDATA) {
					adblogwrite("found data\n");
					totalbytes -= sizeof(result);
					while (1) {
						if ((readbytes = fread(result, 1, sizeof(result), adbd_fp)) != sizeof(result)) {
							close_restore_fds();
							return -1;
						}

						totalbytes += readbytes;
						std::stringstream str;
						str << readbytes;
						//adblogwrite("readbytes: " + str.str() + "\n");
	
						memcpy(&structcmd, result, sizeof(result));
						std::string cmdtype = structcmd.get_type();

						if (cmdtype.substr(0, sizeof(MD5TRAILER) - 1) == MD5TRAILER) {
							struct AdbBackupFileTrailer md5tr;
							uint32_t crc, md5trcrc, md5ident, md5identmatch;

							ADBSTRUCT_STATIC_ASSERT(sizeof(md5tr) == MAX_ADB_READ);
							memset(&md5tr, 0, sizeof(md5tr));
							memcpy(&md5tr, result, sizeof(result));
							md5ident = md5tr.ident;

							memset(&md5tr.ident, 0, sizeof(md5tr.ident));

							md5identmatch = crc32(0L, Z_NULL, 0);
							md5identmatch = crc32(md5identmatch, (const unsigned char*) &md5tr, sizeof(md5tr));
							md5identmatch = crc32(md5identmatch, (const unsigned char*) &md5fnsize, sizeof(md5fnsize));

							if (md5identmatch == md5ident) {
								totalbytes -= sizeof(result);
								close(adb_write_fd);
								adblogwrite("Restoring MD5TRAILER\n");
								md5trcrc = md5tr.crc;
								memset(&md5tr.crc, 0, sizeof(md5tr.crc));
								crc = crc32(0L, Z_NULL, 0);
								crc = crc32(crc, (const unsigned char*) &md5tr, sizeof(md5tr));
								if (crc == md5trcrc) {
									if (write(adb_control_twrp_fd, result, sizeof(result)) < 1) {
										std::stringstream str;
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

								AdbBackupFileTrailer md5;

								memset(&md5, 0, sizeof(md5));
								strncpy(md5.start_of_trailer, TWRP, sizeof(md5.start_of_trailer));
								strncpy(md5.type, TWMD5, sizeof(md5.type));
								std::string md5string = digest.return_digest_string();
								strncpy(md5.md5, md5string.c_str(), sizeof(md5.md5));

								adblogwrite("Sending MD5Check\n");
								if (write(adb_control_twrp_fd, &md5, sizeof(md5)) < 1) {
									std::stringstream str;
									str << strerror(errno);
									adblogwrite("Cannot write to adb_control_twrp_fd: " + str.str() + "\n");
									close_restore_fds();
									return -1;
								}
								read_from_adb = false; //don't read from adb until TWRP sends TWEOF
								break;
							}
						}
	
						digest.update((unsigned char*)result, sizeof(result));
						dataChunkBytes += readbytes;
						std::stringstream str2;
						str2 << dataChunkBytes;
						//adblogwrite("dataChunkBytes: " + str2.str() + "\n");

						fwrite(result, 1, sizeof(result), fptest);
						adblogwrite("beforewrite\n");
						if (write(adb_write_fd, result, sizeof(result)) < 0) {
							std::stringstream str;
							str << strerror(errno);
							adblogwrite("Cannot write to adb_control_twrp_fd: " + str.str() + "\n");
							close_restore_fds();
							return -1;
						}
						adblogwrite("afterwrite\n");
						if (dataChunkBytes == (DATA_MAX_CHUNK_SIZE - sizeof(result))) {
							std::stringstream str;
							str << dataChunkBytes;
							adblogwrite("data chunk max: " + str.str() + "\n");
							dataChunkBytes = 0;
							break;
						}
						memset(&result, 0, sizeof(result));
					}
				}
			}
		}
	}

	fclose(fptest);
	std::stringstream str;
	str << totalbytes;
	adblogwrite(str.str() + " bytes restored from adbbackup\n");
	return 0;
}

void twrpback::streamFileForTWRP(void) {
	adblogwrite("streamFileForTwrp" + streamFn + "\n");
}

void twrpback::setStreamFileName(std::string fn) {
	streamFn = fn;
	adbd_fd = open(fn.c_str(), O_RDONLY);
	if (adbd_fd < 0) {
		adblogwrite("Unable to open adb_fd\n");
		close(adbd_fd);
		return;
	}
	restore();
}

void twrpback::threadStream(void) {
	pthread_t thread;
	ThreadPtr streamPtr = &twrpback::streamFileForTWRP;
	PThreadPtr p = *(PThreadPtr*)&streamPtr;
	pthread_create(&thread, NULL, p, this);
	pthread_join(thread, NULL);
}
