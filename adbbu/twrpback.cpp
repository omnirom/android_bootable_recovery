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
	adblogfile.close();
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
	adblogfile.open("/tmp/adb.log", std::fstream::app);
}

void twrpback::adblogwrite(std::string writemsg) {
	adblogfile << writemsg << std::flush;
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

bool twrpback::backup(std::string command) {
	twrpMD5 digest;
	bool breakloop = false;
	int bytes = 0, errctr = 0;
	char adbReadStream[MAX_ADB_READ];
	uint64_t totalbytes = 0, dataChunkBytes = 0, fileBytes = 0;
	int64_t count = false;			// Count of how many blocks set
	uint64_t md5fnsize = 0;
	struct AdbBackupControlType endadb;

	ADBSTRUCT_STATIC_ASSERT(sizeof(endadb) == MAX_ADB_READ);

	bool writedata = true;
	bool compressed = false;
	bool firstDataPacket = true;

	adbd_fp = fdopen(adbd_fd, "w");
	if (adbd_fp == NULL) {
		adblogwrite("Unable to open adb_fp\n");
		return false;
	}

	if (mkfifo(TW_ADB_BACKUP, 0666) < 0) {
		adblogwrite("Unable to create TW_ADB_BACKUP fifo\n");
		return false;
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
			return false;
		}
	}

	memset(operation, 0, sizeof(operation));
	if (snprintf(operation, sizeof(operation), "adbbackup %s", command.c_str()) >= sizeof(operation)) {
		adblogwrite("Operation too big to write to ORS_INPUT_FILE\n");
		close_backup_fds();
		return false;
	}
	if (write(write_fd, operation, sizeof(operation)) != sizeof(operation)) {
		adblogwrite("Unable to write to ORS_INPUT_FILE\n");
		close_backup_fds();
		return false;
	}

	memset(&adbReadStream, 0, sizeof(adbReadStream));
	memset(&cmd, 0, sizeof(cmd));

	adblogwrite("opening TW_ADB_BU_CONTROL\n");
	adb_control_bu_fd = open(TW_ADB_BU_CONTROL, O_RDONLY | O_NONBLOCK);
	if (adb_control_bu_fd < 0) {
		adblogwrite("Unable to open TW_ADB_BU_CONTROL for reading.\n");
		close_backup_fds();
		return false;
	}

	adblogwrite("opening TW_ADB_BACKUP\n");
	adb_read_fd = open(TW_ADB_BACKUP, O_RDONLY | O_NONBLOCK);
	if (adb_read_fd < 0) {
		adblogwrite("Unable to open TW_ADB_BACKUP for reading.\n");
		close_backup_fds();
		return false;
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
				return false;
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
				adblogwrite("writing TWSTREAMHDR\n");
				if (fwrite(cmd, 1, sizeof(cmd), adbd_fp) != sizeof(cmd)) {
					std::stringstream str;
					str << strerror(errno);
					adblogwrite("Error writing TWSTREAMHDR to adbd" + str.str() + "\n");
					close_backup_fds();
					return false;
				}
				fflush(adbd_fp);
			}
			//we will be writing an image from TWRP
			else if (cmdtype == TWIMG) {
				struct twfilehdr twimghdr;

				adblogwrite("writing TWIMG\n");
				digest.init();
				memset(&twimghdr, 0, sizeof(twimghdr));
				memcpy(&twimghdr, cmd, sizeof(cmd));
				md5fnsize = twimghdr.size;
				compressed = false;

				if (fwrite(cmd, 1, sizeof(cmd), adbd_fp) != sizeof(cmd)) {
					adblogwrite("Error writing TWIMG to adbd\n");
					close_backup_fds();
					return false;
				}
				fflush(adbd_fp);
				writedata = true;
			}
			//we will be writing a tar from TWRP
			else if (cmdtype == TWFN) {
				struct twfilehdr twfilehdr;

				adblogwrite("writing TWFN\n");
				digest.init();

				ADBSTRUCT_STATIC_ASSERT(sizeof(twfilehdr) == MAX_ADB_READ);

				memset(&twfilehdr, 0, sizeof(twfilehdr));
				memcpy(&twfilehdr, cmd, sizeof(cmd));
				md5fnsize = twfilehdr.size;

				compressed = twfilehdr.compressed == 1 ? true: false;

				if (fwrite(cmd, 1, sizeof(cmd), adbd_fp) != sizeof(cmd)) {
					adblogwrite("Error writing TWFN to adbd\n");
					close_backup_fds();
					return false;
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
				while ((bytes = read(adb_read_fd, &adbReadStream, sizeof(adbReadStream)) != 0)) {
					totalbytes += bytes;
					fileBytes += bytes;
					dataChunkBytes += bytes;

					char *writeAdbReadStream = new char [bytes];
					memcpy(writeAdbReadStream, adbReadStream, bytes);

					digest.update((unsigned char *) writeAdbReadStream, bytes);
					if (fwrite(writeAdbReadStream, 1, bytes, adbd_fp) < 0) {
						std::stringstream str;
						str << strerror(errno);
						adblogwrite("Cannot write to adbd stream: " + str.str() + "\n");
					}
					fflush(adbd_fp);
					delete [] writeAdbReadStream;
					memset(adbReadStream, 0, sizeof(adbReadStream));
				}

				count = fileBytes / DATA_MAX_CHUNK_SIZE + 1;
				count = count * DATA_MAX_CHUNK_SIZE;

				if (fileBytes % DATA_MAX_CHUNK_SIZE != 0) {
					char padding[count - fileBytes];
					int paddingBytes = sizeof(padding);
					std::stringstream paddingStr;
					paddingStr << paddingBytes;
					memset(padding, 0, paddingBytes);
					adblogwrite("writing padding to stream: " + paddingStr.str() + " bytes\n");
					if (fwrite(padding, 1, paddingBytes, adbd_fp) != sizeof(padding)) {
						adblogwrite("Error writing padding to adbd\n");
						close_backup_fds();
						return false;
					}
					totalbytes += paddingBytes;
					digest.update((unsigned char *) padding, paddingBytes);
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
					return false;
				}
				fflush(adbd_fp);
				writedata = false;
				firstDataPacket = true;
				fileBytes = 0;
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
					if (!twadbbu::Write_TWDATA(adbd_fp)) {
						close_backup_fds();
						return false;
					}
					fflush(adbd_fp);
					firstDataPacket = false;
					dataChunkBytes += sizeof(adbReadStream);
				}
				char *writeAdbReadStream = new char [bytes];
				memcpy(writeAdbReadStream, adbReadStream, bytes);

				digest.update((unsigned char *) writeAdbReadStream, bytes);

				totalbytes += bytes;
				fileBytes += bytes;
				dataChunkBytes += bytes;

				if (fwrite(writeAdbReadStream, 1, bytes, adbd_fp) != bytes) {
					adblogwrite("Error writing backup data to adbd\n");
					close_backup_fds();
					return false;
				}
				fflush(adbd_fp);
				delete [] writeAdbReadStream;

				memset(&adbReadStream, 0, sizeof(adbReadStream));

				if (dataChunkBytes == DATA_MAX_CHUNK_SIZE) {
					dataChunkBytes = 0;
					firstDataPacket = true;
				}
				else if (dataChunkBytes > (DATA_MAX_CHUNK_SIZE - sizeof(adbReadStream))) {
					int bytesLeft = DATA_MAX_CHUNK_SIZE - dataChunkBytes;
					char extraData[bytesLeft];

					memset(&extraData, 0, bytesLeft);
					while ((bytes = read(adb_read_fd, &extraData, bytesLeft)) != 0) {
						if (bytes > 0) {
							totalbytes += bytes;
							fileBytes += bytes;
							dataChunkBytes += bytes;

							bytesLeft -= bytes;
							char *writeAdbReadStream = new char [bytes];
							memcpy(writeAdbReadStream, extraData, bytes);

							digest.update((unsigned char *) writeAdbReadStream, bytes);
							if (fwrite(writeAdbReadStream, 1, bytes, adbd_fp) < 0) {
								std::stringstream str;
								str << strerror(errno);
								adblogwrite("Cannot write to adbd stream: " + str.str() + "\n");
								close_restore_fds();
								return false;
							}
							fflush(adbd_fp);
							delete [] writeAdbReadStream;
						}
						memset(&extraData, 0, bytesLeft);
						if (bytesLeft == 0) {
							break;
						}
					}

					fflush(adbd_fp);
					dataChunkBytes = 0;
					firstDataPacket = true;
				}
			}
		}
	}

	//Write the final end adb structure to the adb stream
	if (fwrite(&endadb, 1, sizeof(endadb), adbd_fp) != sizeof(endadb)) {
		adblogwrite("Error writing endadb to adbd\n");
		close_backup_fds();
		return false;
	}
	fflush(adbd_fp);
	close_backup_fds();
	return 0;
}

bool twrpback::restore(void) {
	twrpMD5 digest;
	char cmd[MAX_ADB_READ];
	char readAdbStream[MAX_ADB_READ];
	struct AdbBackupControlType structcmd;
	int errctr = 0;
	uint64_t totalbytes = 0, dataChunkBytes = 0;
	uint64_t md5fnsize = 0;
	bool writedata, read_from_adb;
	bool breakloop, eofsent, md5trsent;
	bool compressed;
	bool md5TrailerReceived = false;

	breakloop = false;
	read_from_adb = true;

	signal(SIGPIPE, SIG_IGN);

	adbd_fp = fdopen(adbd_fd, "r");
	if (adbd_fp == NULL) {
		adblogwrite("Unable to open adb_fp\n");
		close_restore_fds();
		return false;
	}

	if(mkfifo(TW_ADB_RESTORE, 0666)) {
		adblogwrite("Unable to create TW_ADB_RESTORE fifo\n");
		close_restore_fds();
		return false;
	}

	adblogwrite("opening TW_ADB_FIFO\n");
	write_fd = open(TW_ADB_FIFO, O_WRONLY);

	while (write_fd < 0) {
		write_fd = open(TW_ADB_FIFO, O_WRONLY);
		errctr++;
		if (errctr > ADB_BU_MAX_ERROR) {
			adblogwrite("Unable to open TW_ADB_FIFO\n");
			close_restore_fds();
			return false;
		}
	}

	memset(operation, 0, sizeof(operation));
	sprintf(operation, "adbrestore");
	if (write(write_fd, operation, sizeof(operation)) != sizeof(operation)) {
		adblogwrite("Unable to write to TW_ADB_FIFO\n");
		close_restore_fds();
		return false;
	}

	memset(&readAdbStream, 0, sizeof(readAdbStream));
	memset(&cmd, 0, sizeof(cmd));

	adblogwrite("opening TW_ADB_BU_CONTROL\n");
	adb_control_bu_fd = open(TW_ADB_BU_CONTROL, O_RDONLY | O_NONBLOCK);
	if (adb_control_bu_fd < 0) {
		std::stringstream str;
		str << strerror(errno);
		adblogwrite("Unable to open TW_ADB_BU_CONTROL for writing. " + str.str() + "\n");
		close_restore_fds();
		return false;
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
				return false;
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
				memcpy(&tweof, readAdbStream, sizeof(readAdbStream));
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
				return false;
			}
		}
		//If we should read from the adb stream, write commands and data to TWRP
		if (read_from_adb) {
			int readbytes;
			if ((readbytes = fread(readAdbStream, 1, sizeof(readAdbStream), adbd_fp)) == sizeof(readAdbStream)) {
				memcpy(&structcmd, readAdbStream, sizeof(readAdbStream));
				std::string cmdtype = structcmd.get_type();

				//Tell TWRP we have read the entire adb stream
				if (cmdtype == TWENDADB) {
					struct AdbBackupControlType endadb;
					uint32_t crc, endadbcrc;

					memset(&endadb, 0, sizeof(endadb));
					memcpy(&endadb, readAdbStream, sizeof(readAdbStream));
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
							return false;
						}
						read_from_adb = false;
					}
					else {
						adblogwrite("ADB TWENDADB crc header doesn't match\n");
						close_restore_fds();
						return false;
					}
				}
				//Send TWRP partition metadata
				else if (cmdtype == TWSTREAMHDR) {
					struct AdbBackupStreamHeader cnthdr;
					uint32_t crc, cnthdrcrc;

					ADBSTRUCT_STATIC_ASSERT(sizeof(cnthdr) == MAX_ADB_READ);

					memset(&cnthdr, 0, sizeof(cnthdr));
					memcpy(&cnthdr, readAdbStream, sizeof(readAdbStream));
					cnthdrcrc = cnthdr.crc;
					memset(&cnthdr.crc, 0, sizeof(cnthdr.crc));
					crc = crc32(0L, Z_NULL, 0);
					crc = crc32(crc, (const unsigned char*) &cnthdr, sizeof(cnthdr));

					if (crc == cnthdrcrc) {
						adblogwrite("Restoring TWSTREAMHDR\n");
						if (write(adb_control_twrp_fd, readAdbStream, sizeof(readAdbStream)) < 0) {
							std::stringstream str;
							str << strerror(errno);
							adblogwrite("Cannot write to adb_control_twrp_fd: " + str.str() + "\n");
							close_restore_fds();
							return false;
						}
					}
					else {
						adblogwrite("ADB TWSTREAMHDR crc header doesn't match\n");
						close_restore_fds();
						return false;
					}
				}
				//Tell TWRP we are sending a partition image
				else if (cmdtype == TWIMG) {
					struct twfilehdr twimghdr;
					uint32_t crc, twimghdrcrc;

					digest.init();
					adblogwrite("Restoring TWIMG\n");
					memset(&twimghdr, 0, sizeof(twimghdr));
					memcpy(&twimghdr, readAdbStream, sizeof(readAdbStream));
					md5fnsize = twimghdr.size;
					twimghdrcrc = twimghdr.crc;
					memset(&twimghdr.crc, 0, sizeof(twimghdr.crc));

					crc = crc32(0L, Z_NULL, 0);
					crc = crc32(crc, (const unsigned char*) &twimghdr, sizeof(twimghdr));
					if (crc == twimghdrcrc) {
						if (write(adb_control_twrp_fd, readAdbStream, sizeof(readAdbStream)) < 1) {
							std::stringstream str;
							str << strerror(errno);
							adblogwrite("Cannot write to adb_control_twrp_fd: " + str.str() + "\n");
							close_restore_fds();
							return false;
						}
					}
					else {
						adblogwrite("ADB TWIMG crc header doesn't match\n");
						close_restore_fds();
						return false;
					}
					adblogwrite("opening TW_ADB_RESTORE\n");
					adb_write_fd = open(TW_ADB_RESTORE, O_WRONLY);
				}
				//Tell TWRP we are sending a tar stream
				else if (cmdtype == TWFN) {
					struct twfilehdr twfilehdr;
					uint32_t crc, twfilehdrcrc;

					digest.init();
					adblogwrite("Restoring TWFN\n");
					memset(&twfilehdr, 0, sizeof(twfilehdr));
					memcpy(&twfilehdr, readAdbStream, sizeof(readAdbStream));
					md5fnsize = twfilehdr.size;
					twfilehdrcrc = twfilehdr.crc;
					memset(&twfilehdr.crc, 0, sizeof(twfilehdr.crc));

					crc = crc32(0L, Z_NULL, 0);
					crc = crc32(crc, (const unsigned char*) &twfilehdr, sizeof(twfilehdr));

					if (crc == twfilehdrcrc) {
						if (write(adb_control_twrp_fd, readAdbStream, sizeof(readAdbStream)) < 1) {
							std::stringstream str;
							str << strerror(errno);
							adblogwrite("Cannot write to adb_control_twrp_fd: " + str.str() + "\n");
							close_restore_fds();
							return false;
						}
					}
					else {
						adblogwrite("ADB TWFN crc header doesn't match\n");
						close_restore_fds();
						return false;
					}

					adblogwrite("opening TW_ADB_RESTORE\n");
					adb_write_fd = open(TW_ADB_RESTORE, O_WRONLY);
				}
				else if (cmdtype == MD5TRAILER) {
					read_from_adb = false; //don't read from adb until TWRP sends TWEOF
					close(adb_write_fd);
					md5TrailerReceived = true;
					if (!checkMD5Trailer(readAdbStream, md5fnsize, &digest)) {
						close_restore_fds();
						return false;
					}
				}
				//Send the tar or partition image md5 to TWRP
				else if (cmdtype == TWDATA) {
					dataChunkBytes += sizeof(readAdbStream);
					while (1) {
						if ((readbytes = fread(readAdbStream, 1, sizeof(readAdbStream), adbd_fp)) != sizeof(readAdbStream)) {
							close_restore_fds();
							return false;
						}

						memcpy(&structcmd, readAdbStream, sizeof(readAdbStream));

						char *readAdbReadStream = new char [readbytes];
						memcpy(readAdbReadStream, readAdbStream, readbytes);
						std::string cmdtype = structcmd.get_type();
						dataChunkBytes += readbytes;
						delete [] readAdbReadStream;
						totalbytes += readbytes;
						digest.update((unsigned char*)readAdbReadStream, readbytes);

						if (cmdtype == MD5TRAILER) {
							read_from_adb = false; //don't read from adb until TWRP sends TWEOF
							close(adb_write_fd);
							if (!checkMD5Trailer(readAdbStream, md5fnsize, &digest)) {
								close_restore_fds();
								return false;
							}
							break;
						}


						if (write(adb_write_fd, readAdbStream, sizeof(readAdbStream)) < 0) {
							adblogwrite("end of stream reached.\n");
							break;
						}
						if (dataChunkBytes == DATA_MAX_CHUNK_SIZE) {
							dataChunkBytes = 0;
							break;
						}
						memset(&readAdbStream, 0, sizeof(readAdbStream));
					}
				}
				else {
					if (!md5TrailerReceived) {
						char *readAdbReadStream = new char [readbytes];
						memcpy(readAdbReadStream, readAdbStream, readbytes);
						digest.update((unsigned char*)readAdbReadStream, readbytes);
						totalbytes += readbytes;
						delete [] readAdbReadStream;
					}

				}
			}
		}
	}

	std::stringstream str;
	str << totalbytes;
	adblogwrite(str.str() + " bytes restored from adbbackup\n");
	return true;
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

bool twrpback::checkMD5Trailer(char readAdbStream[], uint64_t md5fnsize, twrpMD5 *digest) {
	struct AdbBackupFileTrailer md5tr;
	uint32_t crc, md5trcrc, md5ident, md5identmatch;

	ADBSTRUCT_STATIC_ASSERT(sizeof(md5tr) == MAX_ADB_READ);
	memset(&md5tr, 0, sizeof(md5tr));
	memcpy(&md5tr, readAdbStream, MAX_ADB_READ);
	md5ident = md5tr.ident;

	memset(&md5tr.ident, 0, sizeof(md5tr.ident));

	md5identmatch = crc32(0L, Z_NULL, 0);
	md5identmatch = crc32(md5identmatch, (const unsigned char*) &md5tr, sizeof(md5tr));
	md5identmatch = crc32(md5identmatch, (const unsigned char*) &md5fnsize, sizeof(md5fnsize));

	if (md5identmatch == md5ident) {
		adblogwrite("checking MD5TRAILER\n");
		md5trcrc = md5tr.crc;
		memset(&md5tr.crc, 0, sizeof(md5tr.crc));
		crc = crc32(0L, Z_NULL, 0);
		crc = crc32(crc, (const unsigned char*) &md5tr, sizeof(md5tr));
		if (crc == md5trcrc) {
			if (write(adb_control_twrp_fd, &md5tr, sizeof(md5tr)) < 1) {
				std::stringstream str;
				str << strerror(errno);
				adblogwrite("Cannot write to adb_control_twrp_fd: " + str.str() + "\n");
				close_restore_fds();
				return false;
			}
		}
		else {
			adblogwrite("ADB MD5TRAILER crc header doesn't match\n");
			close_restore_fds();
			return false;
		}

		AdbBackupFileTrailer md5;

		memset(&md5, 0, sizeof(md5));
		strncpy(md5.start_of_trailer, TWRP, sizeof(md5.start_of_trailer));
		strncpy(md5.type, TWMD5, sizeof(md5.type));
		std::string md5string = digest->return_digest_string();
		strncpy(md5.md5, md5string.c_str(), sizeof(md5.md5));

		adblogwrite("sending MD5 verification\n");
		std::stringstream dstr;
		dstr << adb_control_twrp_fd;
		if (write(adb_control_twrp_fd, &md5, sizeof(md5)) < 1) {
			std::stringstream str;
			str << strerror(errno);
			adblogwrite("Cannot write to adb_control_twrp_fd: " + str.str() + "\n");
			close_restore_fds();
			return false;
		}
		return true;
	}
	return false;
}
