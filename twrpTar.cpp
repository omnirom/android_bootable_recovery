
/*
	Copyright 2013 TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

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

extern "C" {
	#include "libtar/libtar.h"
	#include "twrpTar.h"
	#include "tarWrite.h"
	#include "set_metadata.h"
}
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <csignal>
#include <dirent.h>
#include <libgen.h>
#include <sys/mman.h>
#include "twrpTar.hpp"
#include "twcommon.h"
#include "variables.h"
#include "twrp-functions.hpp"
#ifndef BUILD_TWRPTAR_MAIN
#include "data.hpp"
#include "infomanager.hpp"
#include "gui/gui.hpp"
extern "C" {
	#include "set_metadata.h"
}
#endif //ndef BUILD_TWRPTAR_MAIN

using namespace std;

twrpTar::twrpTar(void) {
	use_encryption = 0;
	userdata_encryption = 0;
	use_compression = 0;
	split_archives = 0;
	has_data_media = 0;
	pigz_pid = 0;
	oaes_pid = 0;
	Total_Backup_Size = 0;
	Archive_Current_Size = 0;
	include_root_dir = true;
}

twrpTar::~twrpTar(void) {
	// Do nothing
}

void twrpTar::setfn(string fn) {
	tarfn = fn;
}

void twrpTar::setdir(string dir) {
	tardir = dir;
}

void twrpTar::setsize(unsigned long long backup_size) {
	Total_Backup_Size = backup_size;
}

void twrpTar::setpassword(string pass) {
	password = pass;
}

void twrpTar::Signal_Kill(int signum) {
	_exit(255);
}

int twrpTar::createTarFork(const unsigned long long *overall_size, const unsigned long long *other_backups_size, pid_t &fork_pid) {
	int status = 0;
	pid_t rc_pid, tar_fork_pid;
	int progress_pipe[2], ret;

	file_count = 0;

	if (pipe(progress_pipe) < 0) {
		LOGINFO("Error creating progress tracking pipe\n");
		gui_err("backup_error=Error creating backup.");
		return -1;
	}
	if ((tar_fork_pid = fork()) == -1) {
		LOGINFO("create tar failed to fork.\n");
		gui_err("backup_error=Error creating backup.");
		close(progress_pipe[0]);
		close(progress_pipe[1]);
		return -1;
	}

	if (tar_fork_pid == 0) {
		// Child process
		// Child closes input side of progress pipe
		signal(SIGUSR2, twrpTar::Signal_Kill);
		close(progress_pipe[0]);
		progress_pipe_fd = progress_pipe[1];

		if (use_encryption || userdata_encryption) {
			LOGINFO("Using encryption\n");
			DIR* d;
			struct dirent* de;
			unsigned long long regular_size = 0, encrypt_size = 0, target_size = 0, total_size;
			unsigned enc_thread_id = 1, regular_thread_id = 0, i, start_thread_id = 1, core_count = 1;
			int item_len, ret, thread_error = 0;
			std::vector<TarListStruct> RegularList;
			std::vector<TarListStruct> EncryptList;
			string FileName;
			struct TarListStruct TarItem;
			twrpTar reg, enc[9];
			struct stat st;
			pthread_t enc_thread[9];
			pthread_attr_t tattr;
			void *thread_return;

			core_count = sysconf(_SC_NPROCESSORS_CONF);
			if (core_count > 8)
				core_count = 8;
			LOGINFO("   Core Count      : %u\n", core_count);
			Archive_Current_Size = 0;

			d = opendir(tardir.c_str());
			if (d == NULL) {
				gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(tardir)(strerror(errno)));
				close(progress_pipe[1]);
				_exit(-1);
			}
			// Figure out the size of all data to be encrypted and create a list of unencrypted files
			while ((de = readdir(d)) != NULL) {
				FileName = tardir + "/" + de->d_name;

				if (de->d_type == DT_BLK || de->d_type == DT_CHR || du.check_skip_dirs(FileName))
					continue;
				if (de->d_type == DT_DIR) {
					item_len = strlen(de->d_name);
					if (userdata_encryption && ((item_len >= 3 && strncmp(de->d_name, "app", 3) == 0) || (item_len >= 6 && strncmp(de->d_name, "dalvik", 6) == 0))) {
						ret = Generate_TarList(FileName, &RegularList, &target_size, &regular_thread_id);
						if (ret < 0) {
							LOGINFO("Error in Generate_TarList with regular list!\n");
							gui_err("backup_error=Error creating backup.");
							closedir(d);
							close(progress_pipe_fd);
							close(progress_pipe[1]);
							_exit(-1);
						}
						file_count = (unsigned long long)(ret);
						regular_size += du.Get_Folder_Size(FileName);
					} else {
						encrypt_size += du.Get_Folder_Size(FileName);
					}
				} else if (de->d_type == DT_REG) {
					stat(FileName.c_str(), &st);
					encrypt_size += (unsigned long long)(st.st_size);
				}
			}
			closedir(d);

			target_size = encrypt_size / core_count;
			target_size++;
			LOGINFO("   Unencrypted size: %llu\n", regular_size);
			LOGINFO("   Encrypted size  : %llu\n", encrypt_size);
			LOGINFO("   Target size     : %llu\n", target_size);
			if (!userdata_encryption) {
				enc_thread_id = 0;
				start_thread_id = 0;
				core_count--;
			}
			Archive_Current_Size = 0;

			d = opendir(tardir.c_str());
			if (d == NULL) {
				gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(tardir)(strerror(errno)));
				close(progress_pipe[1]);
				_exit(-1);
			}
			// Divide up the encrypted file list for threading
			while ((de = readdir(d)) != NULL) {
				FileName = tardir + "/" + de->d_name;

				if (de->d_type == DT_BLK || de->d_type == DT_CHR || du.check_skip_dirs(FileName))
					continue;
				if (de->d_type == DT_DIR) {
					item_len = strlen(de->d_name);
					if (userdata_encryption && ((item_len >= 3 && strncmp(de->d_name, "app", 3) == 0) || (item_len >= 6 && strncmp(de->d_name, "dalvik", 6) == 0))) {
						// Do nothing, we added these to RegularList earlier
					} else {
						FileName = tardir + "/" + de->d_name;
						ret = Generate_TarList(FileName, &EncryptList, &target_size, &enc_thread_id);
						if (ret < 0) {
							LOGINFO("Error in Generate_TarList with encrypted list!\n");
							gui_err("backup_error=Error creating backup.");
							closedir(d);
							close(progress_pipe[1]);
							_exit(-1);
						}
						file_count += (unsigned long long)(ret);
					}
				} else if (de->d_type == DT_REG || de->d_type == DT_LNK) {
					stat(FileName.c_str(), &st);
					if (de->d_type == DT_REG)
						Archive_Current_Size += (unsigned long long)(st.st_size);
					TarItem.fn = FileName;
					TarItem.thread_id = enc_thread_id;
					EncryptList.push_back(TarItem);
					file_count++;
				}
			}
			closedir(d);
			if (enc_thread_id != core_count) {
				LOGINFO("Error dividing up threads for encryption, %u threads for %u cores!\n", enc_thread_id, core_count);
				if (enc_thread_id > core_count) {
					gui_err("backup_error=Error creating backup.");
					close(progress_pipe[1]);
					_exit(-1);
				} else {
					LOGINFO("Continuining anyway.");
				}
			}

			// Send file count to parent
			write(progress_pipe_fd, &file_count, sizeof(file_count));
			// Send backup size to parent
			total_size = regular_size + encrypt_size;
			write(progress_pipe_fd, &total_size, sizeof(total_size));

			if (userdata_encryption) {
				// Create a backup of unencrypted data
				reg.setfn(tarfn);
				reg.ItemList = &RegularList;
				reg.thread_id = 0;
				reg.use_encryption = 0;
				reg.use_compression = use_compression;
				reg.split_archives = 1;
				reg.progress_pipe_fd = progress_pipe_fd;
				LOGINFO("Creating unencrypted backup...\n");
				if (createList((void*)&reg) != 0) {
					LOGINFO("Error creating unencrypted backup.\n");
					gui_err("backup_error=Error creating backup.");
					close(progress_pipe[1]);
					_exit(-1);
				}
			}

			if (pthread_attr_init(&tattr)) {
				LOGINFO("Unable to pthread_attr_init\n");
				gui_err("backup_error=Error creating backup.");
				close(progress_pipe[1]);
				_exit(-1);
			}
			if (pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE)) {
				LOGINFO("Error setting pthread_attr_setdetachstate\n");
				gui_err("backup_error=Error creating backup.");
				close(progress_pipe[1]);
				_exit(-1);
			}
			if (pthread_attr_setscope(&tattr, PTHREAD_SCOPE_SYSTEM)) {
				LOGINFO("Error setting pthread_attr_setscope\n");
				gui_err("backup_error=Error creating backup.");
				close(progress_pipe[1]);
				_exit(-1);
			}
			/*if (pthread_attr_setstacksize(&tattr, 524288)) {
				LOGERR("Error setting pthread_attr_setstacksize\n");
				_exit(-1);
			}*/

			// Create threads for the divided up encryption lists
			for (i = start_thread_id; i <= core_count; i++) {
				enc[i].setdir(tardir);
				enc[i].setfn(tarfn);
				enc[i].ItemList = &EncryptList;
				enc[i].thread_id = i;
				enc[i].use_encryption = use_encryption;
				enc[i].setpassword(password);
				enc[i].use_compression = use_compression;
				enc[i].split_archives = 1;
				enc[i].progress_pipe_fd = progress_pipe_fd;
				LOGINFO("Start encryption thread %i\n", i);
				ret = pthread_create(&enc_thread[i], &tattr, createList, (void*)&enc[i]);
				if (ret) {
					LOGINFO("Unable to create %i thread for encryption! %i\nContinuing in same thread (backup will be slower).\n", i, ret);
					if (createList((void*)&enc[i]) != 0) {
						LOGINFO("Error creating encrypted backup %i.\n", i);
						gui_err("backup_error=Error creating backup.");
						close(progress_pipe[1]);
						_exit(-1);
					} else {
						enc[i].thread_id = i + 1;
					}
				}
				usleep(100000); // Need a short delay before starting the next thread or the threads will never finish for some reason.
			}
			if (pthread_attr_destroy(&tattr)) {
				LOGINFO("Failed to pthread_attr_destroy\n");
			}
			for (i = start_thread_id; i <= core_count; i++) {
				if (enc[i].thread_id == i) {
					if (pthread_join(enc_thread[i], &thread_return)) {
						LOGINFO("Error joining thread %i\n", i);
						gui_err("backup_error=Error creating backup.");
						close(progress_pipe[1]);
						_exit(-1);
					} else {
						LOGINFO("Joined thread %i.\n", i);
						ret = (int)(intptr_t)thread_return;
						if (ret != 0) {
							thread_error = 1;
							LOGINFO("Thread %i returned an error %i.\n", i, ret);
							gui_err("backup_error=Error creating backup.");
							close(progress_pipe[1]);
							_exit(-1);
						}
					}
				} else {
					LOGINFO("Skipping joining thread %i because of pthread failure.\n", i);
				}
			}
			if (thread_error) {
				LOGINFO("Error returned by one or more threads.\n");
				gui_err("backup_error=Error creating backup.");
				close(progress_pipe[1]);
				_exit(-1);
			}
			LOGINFO("Finished encrypted backup.\n");
			close(progress_pipe[1]);
			_exit(0);
		} else {
			// Not encrypted
			std::vector<TarListStruct> FileList;
			unsigned thread_id = 0;
			unsigned long long target_size = 0;
			twrpTar reg;
			int ret;

			// Generate list of files to back up
			ret = Generate_TarList(tardir, &FileList, &target_size, &thread_id);
			if (ret < 0) {
				LOGINFO("Error in Generate_TarList!\n");
				gui_err("backup_error=Error creating backup.");
				close(progress_pipe[1]);
				_exit(-1);
			}
			file_count = (unsigned long long)(ret);
			// Create a backup
			reg.setfn(tarfn);
			reg.ItemList = &FileList;
			reg.thread_id = 0;
			reg.use_encryption = 0;
			reg.use_compression = use_compression;
			reg.setsize(Total_Backup_Size);
			reg.progress_pipe_fd = progress_pipe_fd;
			if (Total_Backup_Size > MAX_ARCHIVE_SIZE) {
				gui_msg("split_backup=Breaking backup file into multiple archives...");
				reg.split_archives = 1;
			} else {
				reg.split_archives = 0;
			}
			LOGINFO("Creating backup...\n");
			write(progress_pipe_fd, &file_count, sizeof(file_count));
			write(progress_pipe_fd, &Total_Backup_Size, sizeof(Total_Backup_Size));
			if (createList((void*)&reg) != 0) {
				gui_err("backup_error=Error creating backup.");
				close(progress_pipe[1]);
				_exit(-1);
			}
			close(progress_pipe[1]);
			_exit(0);
		}
	} else {
		// Parent side
		unsigned long long fs, size_backup, files_backup, total_backup_size;
		int first_data = 0;
		double display_percent, progress_percent;
		char file_progress[1024];
		char size_progress[1024];
		files_backup = 0;
		size_backup = 0;
		string file_prog = gui_lookup("file_progress", "%llu of %llu files, %i%%");
		string size_prog = gui_lookup("size_progress", "%lluMB of %lluMB, %i%%");

		fork_pid = tar_fork_pid;

		// Parent closes output side
		close(progress_pipe[1]);

		// Read progress data from children
		while (read(progress_pipe[0], &fs, sizeof(fs)) > 0) {
			if (first_data == 0) {
				// First incoming data is the file count
				file_count = fs;
				if (file_count == 0) file_count = 1; // prevent division by 0 below
				first_data = 1;
			} else if (first_data == 1) {
				// Second incoming data is total size
				total_backup_size = fs;
				first_data = 2;
			} else {
				files_backup++;
				size_backup += fs;
				display_percent = (double)(files_backup) / (double)(file_count) * 100;
				sprintf(file_progress, file_prog.c_str(), files_backup, file_count, (int)(display_percent));
#ifndef BUILD_TWRPTAR_MAIN
				DataManager::SetValue("tw_file_progress", file_progress);
				display_percent = (double)(size_backup + *other_backups_size) / (double)(*overall_size) * 100;
				sprintf(size_progress, size_prog.c_str(), (size_backup + *other_backups_size) / 1048576, *overall_size / 1048576, (int)(display_percent));
				DataManager::SetValue("tw_size_progress", size_progress);
				progress_percent = (display_percent / 100);
				DataManager::SetProgress((float)(progress_percent));
#endif //ndef BUILD_TWRPTAR_MAIN
			}
		}
		close(progress_pipe[0]);
#ifndef BUILD_TWRPTAR_MAIN
		DataManager::SetValue("tw_file_progress", "");
		DataManager::SetValue("tw_size_progress", "");

		InfoManager backup_info(backup_folder + partition_name + ".info");
		backup_info.SetValue("backup_size", size_backup);
		if (use_compression && use_encryption)
			backup_info.SetValue("backup_type", 3);
		else if (use_encryption)
			backup_info.SetValue("backup_type", 2);
		else if (use_compression)
			backup_info.SetValue("backup_type", 1);
		else
			backup_info.SetValue("backup_type", 0);
		backup_info.SetValue("file_count", files_backup);
		backup_info.SaveValues();
#endif //ndef BUILD_TWRPTAR_MAIN
		if (TWFunc::Wait_For_Child(tar_fork_pid, &status, "createTarFork()") != 0)
			return -1;
	}
	return 0;
}

int twrpTar::extractTarFork(const unsigned long long *overall_size, unsigned long long *other_backups_size) {
	int status = 0;
	pid_t rc_pid, tar_fork_pid;
	int progress_pipe[2], ret;

	if (pipe(progress_pipe) < 0) {
		LOGINFO("Error creating progress tracking pipe\n");
		gui_err("restore_error=Error during restore process.");
		return -1;
	}

	tar_fork_pid = fork();
	if (tar_fork_pid >= 0) // fork was successful
	{
		if (tar_fork_pid == 0) // child process
		{
			close(progress_pipe[0]);
			progress_pipe_fd = progress_pipe[1];
			if (TWFunc::Path_Exists(tarfn)) {
				LOGINFO("Single archive\n");
				if (extract() != 0)
					_exit(-1);
				else
					_exit(0);
			} else {
				LOGINFO("Multiple archives\n");
				string temp;
				char actual_filename[255];
				twrpTar tars[9];
				pthread_t tar_thread[9];
				pthread_attr_t tattr;
				unsigned thread_count = 0, i, start_thread_id = 1;
				int ret, thread_error = 0;
				void *thread_return;

				basefn = tarfn;
				temp = basefn + "%i%02i";
				tarfn += "000";
				if (!TWFunc::Path_Exists(tarfn)) {
					LOGINFO("Unable to locate '%s' or '%s'\n", basefn.c_str(), tarfn.c_str());
					gui_err("restore_error=Error during restore process.");
					close(progress_pipe_fd);
					_exit(-1);
				}
				if (TWFunc::Get_File_Type(tarfn) != 2) {
					LOGINFO("First tar file '%s' not encrypted\n", tarfn.c_str());
					tars[0].basefn = basefn;
					tars[0].thread_id = 0;
					tars[0].progress_pipe_fd = progress_pipe_fd;
					if (extractMulti((void*)&tars[0]) != 0) {
						LOGINFO("Error extracting split archive.\n");
						gui_err("restore_error=Error during restore process.");
						close(progress_pipe_fd);
						_exit(-1);
					}
				} else {
					start_thread_id = 0;
				}
				// Start threading encrypted restores
				if (pthread_attr_init(&tattr)) {
					LOGINFO("Unable to pthread_attr_init\n");
					gui_err("restore_error=Error during restore process.");
					close(progress_pipe_fd);
					_exit(-1);
				}
				if (pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_JOINABLE)) {
					LOGINFO("Error setting pthread_attr_setdetachstate\n");
					gui_err("restore_error=Error during restore process.");
					close(progress_pipe_fd);
					_exit(-1);
				}
				if (pthread_attr_setscope(&tattr, PTHREAD_SCOPE_SYSTEM)) {
					LOGINFO("Error setting pthread_attr_setscope\n");
					gui_err("restore_error=Error during restore process.");
					close(progress_pipe_fd);
					_exit(-1);
				}
				/*if (pthread_attr_setstacksize(&tattr, 524288)) {
					LOGERR("Error setting pthread_attr_setstacksize\n");
					close(progress_pipe_fd);
					_exit(-1);
				}*/
				for (i = start_thread_id; i < 9; i++) {
					sprintf(actual_filename, temp.c_str(), i, 0);
					if (TWFunc::Path_Exists(actual_filename)) {
						thread_count++;
						tars[i].basefn = basefn;
						tars[i].setpassword(password);
						tars[i].thread_id = i;
						tars[i].progress_pipe_fd = progress_pipe_fd;
						LOGINFO("Creating extract thread ID %i\n", i);
						ret = pthread_create(&tar_thread[i], &tattr, extractMulti, (void*)&tars[i]);
						if (ret) {
							LOGINFO("Unable to create %i thread for extraction! %i\nContinuing in same thread (restore will be slower).\n", i, ret);
							if (extractMulti((void*)&tars[i]) != 0) {
								LOGINFO("Error extracting backup in thread %i.\n", i);
								gui_err("restore_error=Error during restore process.");
								close(progress_pipe_fd);
								_exit(-1);
							} else {
								tars[i].thread_id = i + 1;
							}
						}
						usleep(100000); // Need a short delay before starting the next thread or the threads will never finish for some reason.
					} else {
						break;
					}
				}
				for (i = start_thread_id; i < thread_count + start_thread_id; i++) {
					if (tars[i].thread_id == i) {
						if (pthread_join(tar_thread[i], &thread_return)) {
							LOGINFO("Error joining thread %i\n", i);
							gui_err("restore_error=Error during restore process.");
							close(progress_pipe_fd);
							_exit(-1);
						} else {
							LOGINFO("Joined thread %i.\n", i);
							ret = (int)(intptr_t)thread_return;
							if (ret != 0) {
								thread_error = 1;
								LOGINFO("Thread %i returned an error %i.\n", i, ret);
								gui_err("restore_error=Error during restore process.");
								close(progress_pipe_fd);
								_exit(-1);
							}
						}
					} else {
						LOGINFO("Skipping joining thread %i because of pthread failure.\n", i);
					}
				}
				if (thread_error) {
					LOGINFO("Error returned by one or more threads.\n");
					gui_err("restore_error=Error during restore process.");
					close(progress_pipe_fd);
					_exit(-1);
				}
				LOGINFO("Finished encrypted restore.\n");
				close(progress_pipe_fd);
				_exit(0);
			}
		}
		else // parent process
		{
			unsigned long long fs, size_backup;
			double display_percent, progress_percent;
			char size_progress[1024];
			size_backup = 0;
			string size_prog = gui_lookup("size_progress", "%lluMB of %lluMB, %i%%");

			// Parent closes output side
			close(progress_pipe[1]);

			// Read progress data from children
			while (read(progress_pipe[0], &fs, sizeof(fs)) > 0) {
				size_backup += fs;
				display_percent = (double)(size_backup + *other_backups_size) / (double)(*overall_size) * 100;
				sprintf(size_progress, size_prog.c_str(), (size_backup + *other_backups_size) / 1048576, *overall_size / 1048576, (int)(display_percent));
				progress_percent = (display_percent / 100);
#ifndef BUILD_TWRPTAR_MAIN
				DataManager::SetValue("tw_size_progress", size_progress);
				DataManager::SetProgress((float)(progress_percent));
#endif //ndef BUILD_TWRPTAR_MAIN
			}
			close(progress_pipe[0]);
#ifndef BUILD_TWRPTAR_MAIN
			DataManager::SetValue("tw_file_progress", "");
#endif //ndef BUILD_TWRPTAR_MAIN
			*other_backups_size += size_backup;

			if (TWFunc::Wait_For_Child(tar_fork_pid, &status, "extractTarFork()") != 0)
				return -1;
		}
	}
	else // fork has failed
	{
		close(progress_pipe[0]);
		close(progress_pipe[1]);
		LOGINFO("extract tar failed to fork.\n");
		return -1;
	}
	return 0;
}

int twrpTar::Generate_TarList(string Path, std::vector<TarListStruct> *TarList, unsigned long long *Target_Size, unsigned *thread_id) {
	DIR* d;
	struct dirent* de;
	struct stat st;
	string FileName;
	struct TarListStruct TarItem;
	string::size_type i;
	int ret, file_count;
	file_count = 0;

	d = opendir(Path.c_str());
	if (d == NULL) {
		gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(Path)(strerror(errno)));
		closedir(d);
		return -1;
	}
	while ((de = readdir(d)) != NULL) {
		FileName = Path + "/" + de->d_name;

		if (de->d_type == DT_BLK || de->d_type == DT_CHR || du.check_skip_dirs(FileName))
			continue;
		TarItem.fn = FileName;
		TarItem.thread_id = *thread_id;
		if (de->d_type == DT_DIR) {
			TarList->push_back(TarItem);
			ret = Generate_TarList(FileName, TarList, Target_Size, thread_id);
			if (ret < 0)
				return -1;
			file_count += ret;
		} else if (de->d_type == DT_REG || de->d_type == DT_LNK) {
			stat(FileName.c_str(), &st);
			TarList->push_back(TarItem);
			if (de->d_type == DT_REG) {
				file_count++;
				Archive_Current_Size += st.st_size;
			}
			if (Archive_Current_Size != 0 && *Target_Size != 0 && Archive_Current_Size > *Target_Size) {
				*thread_id = *thread_id + 1;
				Archive_Current_Size = 0;
			}
		}
	}
	closedir(d);
	return file_count;
}

int twrpTar::extractTar() {
	char* charRootDir = (char*) tardir.c_str();
	if (openTar() == -1)
		return -1;
	if (tar_extract_all(t, charRootDir, &progress_pipe_fd) != 0) {
		LOGINFO("Unable to extract tar archive '%s'\n", tarfn.c_str());
		gui_err("restore_error=Error during restore process.");
		return -1;
	}
	if (tar_close(t) != 0) {
		LOGINFO("Unable to close tar file\n");
		gui_err("restore_error=Error during restore process.");
		return -1;
	}
	return 0;
}

int twrpTar::extract() {
	Archive_Current_Type = TWFunc::Get_File_Type(tarfn);

	if (Archive_Current_Type == 1) {
		//if you return the extractTGZ function directly, stack crashes happen
		LOGINFO("Extracting gzipped tar\n");
		int ret = extractTar();
		return ret;
	} else if (Archive_Current_Type == 2) {
		int ret = TWFunc::Try_Decrypting_File(tarfn, password);
		if (ret < 1) {
			gui_msg(Msg(msg::kError, "fail_decrypt_tar=Failed to decrypt tar file '{1}'")(tarfn));
			return -1;
		}
		if (ret == 1) {
			LOGINFO("Decrypted file is not in tar format.\n");
			gui_err("restore_error=Error during restore process.");
			return -1;
		}
		if (ret == 3) {
			LOGINFO("Extracting encrypted and compressed tar.\n");
			Archive_Current_Type = 3;
		} else
			LOGINFO("Extracting encrypted tar.\n");
		return extractTar();
	} else {
		LOGINFO("Extracting uncompressed tar\n");
		return extractTar();
	}
}

int twrpTar::tarList(std::vector<TarListStruct> *TarList, unsigned thread_id) {
	struct stat st;
	char buf[PATH_MAX];
	int list_size = TarList->size(), i = 0, archive_count = 0;
	string temp;
	char actual_filename[PATH_MAX];
	char *ptr;
	unsigned long long fs;

	if (split_archives) {
		basefn = tarfn;
		temp = basefn + "%i%02i";
		sprintf(actual_filename, temp.c_str(), thread_id, archive_count);
		tarfn = actual_filename;
		include_root_dir = true;
	} else {
		include_root_dir = false;
	}
	LOGINFO("Creating tar file '%s'\n", tarfn.c_str());
	if (createTar() != 0) {
		LOGINFO("Error creating tar '%s' for thread %i\n", tarfn.c_str(), thread_id);
		gui_err("backup_error=Error creating backup.");
		return -2;
	}
	Archive_Current_Size = 0;

	while (i < list_size) {
		if (TarList->at(i).thread_id == thread_id) {
			strcpy(buf, TarList->at(i).fn.c_str());
			lstat(buf, &st);
			if (S_ISREG(st.st_mode)) { // item is a regular file
				fs = (unsigned long long)(st.st_size);
				if (split_archives && Archive_Current_Size + fs > MAX_ARCHIVE_SIZE) {
					if (closeTar() != 0) {
						LOGINFO("Error closing '%s' on thread %i\n", tarfn.c_str(), thread_id);
						gui_err("backup_error=Error creating backup.");
						return -3;
					}
					archive_count++;
					gui_msg(Msg("split_thread=Splitting thread ID {1} into archive {2}")(thread_id)(archive_count + 1));
					if (archive_count > 99) {
						LOGINFO("Too many archives for thread %i\n", thread_id);
						gui_err("backup_error=Error creating backup.");
						return -4;
					}
					sprintf(actual_filename, temp.c_str(), thread_id, archive_count);
					tarfn = actual_filename;
					if (createTar() != 0) {
						LOGINFO("Error creating tar '%s' for thread %i\n", tarfn.c_str(), thread_id);
						gui_err("backup_error=Error creating backup.");
						return -2;
					}
					Archive_Current_Size = 0;
				}
				Archive_Current_Size += fs;
				write(progress_pipe_fd, &fs, sizeof(fs));
			}
			LOGINFO("addFile '%s' including root: %i\n", buf, include_root_dir);
			if (addFile(buf, include_root_dir) != 0) {
				LOGINFO("Error adding file '%s' to '%s'\n", buf, tarfn.c_str());
				gui_err("backup_error=Error creating backup.");
				return -1;
			}
		}
		i++;
	}
	if (closeTar() != 0) {
		LOGINFO("Error closing '%s' on thread %i\n", tarfn.c_str(), thread_id);
		gui_err("backup_error=Error creating backup.");
		return -3;
	}
	LOGINFO("Thread id %i tarList done, %i archives.\n", thread_id, archive_count);
	return 0;
}

void* twrpTar::createList(void *cookie) {

	twrpTar* threadTar = (twrpTar*) cookie;
	if (threadTar->tarList(threadTar->ItemList, threadTar->thread_id) != 0) {
		LOGINFO("ERROR tarList for thread ID %i\n", threadTar->thread_id);
		return (void*)-2;
	}
	LOGINFO("Thread ID %i finished successfully.\n", threadTar->thread_id);
	return (void*)0;
}

void* twrpTar::extractMulti(void *cookie) {

	twrpTar* threadTar = (twrpTar*) cookie;
	int archive_count = 0;
	string temp = threadTar->basefn + "%i%02i";
	char actual_filename[255];
	sprintf(actual_filename, temp.c_str(), threadTar->thread_id, archive_count);
	while (TWFunc::Path_Exists(actual_filename)) {
		threadTar->tarfn = actual_filename;
		if (threadTar->extract() != 0) {
			LOGINFO("Error extracting '%s' in thread ID %i\n", actual_filename, threadTar->thread_id);
			return (void*)-2;
		}
		archive_count++;
		if (archive_count > 99)
			break;
		sprintf(actual_filename, temp.c_str(), threadTar->thread_id, archive_count);
	}
	LOGINFO("Thread ID %i finished successfully.\n", threadTar->thread_id);
	return (void*)0;
}

int twrpTar::addFilesToExistingTar(vector <string> files, string fn) {
	char* charTarFile = (char*) fn.c_str();

	if (tar_open(&t, charTarFile, NULL, O_RDONLY | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU | TAR_STORE_SELINUX) == -1)
		return -1;
	removeEOT(charTarFile);
	if (tar_open(&t, charTarFile, NULL, O_WRONLY | O_APPEND | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU | TAR_STORE_SELINUX) == -1)
		return -1;
	for (unsigned int i = 0; i < files.size(); ++i) {
		char* file = (char*) files.at(i).c_str();
		if (tar_append_file(t, file, file) == -1)
			return -1;
	}
	if (tar_append_eof(t) == -1)
		return -1;
	if (tar_close(t) == -1)
		return -1;
	return 0;
}

int twrpTar::createTar() {
	char* charTarFile = (char*) tarfn.c_str();
	char* charRootDir = (char*) tardir.c_str();
	static tartype_t type = { open, close, read, write_tar };

	if (use_encryption && use_compression) {
		// Compressed and encrypted
		Archive_Current_Type = 3;
		LOGINFO("Using encryption and compression...\n");
		int i, pipes[4];

		if (pipe(pipes) < 0) {
			LOGINFO("Error creating first pipe\n");
			gui_err("backup_error=Error creating backup.");
			return -1;
		}
		if (pipe(pipes + 2) < 0) {
			LOGINFO("Error creating second pipe\n");
			gui_err("backup_error=Error creating backup.");
			return -1;
		}
		int output_fd = open(tarfn.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
		if (output_fd < 0) {
			gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(tarfn)(strerror(errno)));
			for (i = 0; i < 4; i++)
				close(pipes[i]); // close all
			return -1;
		}
		pigz_pid = fork();

		if (pigz_pid < 0) {
			LOGINFO("pigz fork() failed\n");
			gui_err("backup_error=Error creating backup.");
			close(output_fd);
			for (i = 0; i < 4; i++)
				close(pipes[i]); // close all
			return -1;
		} else if (pigz_pid == 0) {
			// pigz Child
			close(pipes[1]);
			close(pipes[2]);
			close(0);
			dup2(pipes[0], 0);
			close(1);
			dup2(pipes[3], 1);
			if (execlp("pigz", "pigz", "-", NULL) < 0) {
				LOGINFO("execlp pigz ERROR!\n");
				gui_err("backup_error=Error creating backup.");
				close(output_fd);
				close(pipes[0]);
				close(pipes[3]);
				_exit(-1);
			}
		} else {
			// Parent
			oaes_pid = fork();

			if (oaes_pid < 0) {
				LOGINFO("openaes fork() failed\n");
				gui_err("backup_error=Error creating backup.");
				close(output_fd);
				for (i = 0; i < 4; i++)
					close(pipes[i]); // close all
				return -1;
			} else if (oaes_pid == 0) {
				// openaes Child
				close(pipes[0]);
				close(pipes[1]);
				close(pipes[3]);
				close(0);
				dup2(pipes[2], 0);
				close(1);
				dup2(output_fd, 1);
				if (execlp("openaes", "openaes", "enc", "--key", password.c_str(), NULL) < 0) {
					LOGINFO("execlp openaes ERROR!\n");
					gui_err("backup_error=Error creating backup.");
					close(pipes[2]);
					close(output_fd);
					_exit(-1);
				}
			} else {
				// Parent
				close(pipes[0]);
				close(pipes[2]);
				close(pipes[3]);
				fd = pipes[1];
				if(tar_fdopen(&t, fd, charRootDir, NULL, O_WRONLY | O_CREAT | O_EXCL | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU | TAR_STORE_SELINUX) != 0) {
					close(fd);
					LOGINFO("tar_fdopen failed\n");
					gui_err("backup_error=Error creating backup.");
					return -1;
				}
				return 0;
			}
		}
	} else if (use_compression) {
		// Compressed
		Archive_Current_Type = 1;
		LOGINFO("Using compression...\n");
		int pigzfd[2];
		int output_fd = open(tarfn.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
		if (output_fd < 0) {
			gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(tarfn)(strerror(errno)));
			close(pigzfd[0]);
			return -1;
		}

		if (pipe(pigzfd) < 0) {
			LOGINFO("Error creating pipe\n");
			gui_err("backup_error=Error creating backup.");
			close(output_fd);
			return -1;
		}
		pigz_pid = fork();

		if (pigz_pid < 0) {
			LOGINFO("fork() failed\n");
			gui_err("backup_error=Error creating backup.");
			close(output_fd);
			close(pigzfd[0]);
			close(pigzfd[1]);
			return -1;
		} else if (pigz_pid == 0) {
			// Child
			close(pigzfd[1]);   // close unused output pipe
			dup2(pigzfd[0], 0); // remap stdin
			dup2(output_fd, 1); // remap stdout to output file
			if (execlp("pigz", "pigz", "-", NULL) < 0) {
				LOGINFO("execlp pigz ERROR!\n");
				gui_err("backup_error=Error creating backup.");
				close(output_fd);
				close(pigzfd[0]);
				_exit(-1);
			}
		} else {
			// Parent
			close(pigzfd[0]); // close parent input
			fd = pigzfd[1];   // copy parent output
			if(tar_fdopen(&t, fd, charRootDir, NULL, O_WRONLY | O_CREAT | O_EXCL | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU | TAR_STORE_SELINUX) != 0) {
				close(fd);
				LOGINFO("tar_fdopen failed\n");
				gui_err("backup_error=Error creating backup.");
				return -1;
			}
		}
	} else if (use_encryption) {
		// Encrypted
		Archive_Current_Type = 2;
		LOGINFO("Using encryption...\n");
		int oaesfd[2];
		int output_fd = open(tarfn.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
		if (output_fd < 0) {
			gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(tarfn)(strerror(errno)));
			return -1;
		}
		if (pipe(oaesfd) < 0) {
			LOGINFO("Error creating pipe\n");
			gui_err("backup_error=Error creating backup.");
			close(output_fd);
			return -1;
		}
		oaes_pid = fork();

		if (oaes_pid < 0) {
			LOGINFO("fork() failed\n");
			gui_err("backup_error=Error creating backup.");
			close(output_fd);
			close(oaesfd[0]);
			close(oaesfd[1]);
			return -1;
		} else if (oaes_pid == 0) {
			// Child
			close(oaesfd[1]);   // close unused
			dup2(oaesfd[0], 0); // remap stdin
			dup2(output_fd, 1); // remap stdout to output file
			if (execlp("openaes", "openaes", "enc", "--key", password.c_str(), NULL) < 0) {
				LOGINFO("execlp openaes ERROR!\n");
				gui_err("backup_error=Error creating backup.");
				close(output_fd);
				close(oaesfd[0]);
				_exit(-1);
			}
		} else {
			// Parent
			close(oaesfd[0]); // close parent input
			fd = oaesfd[1];   // copy parent output
			if(tar_fdopen(&t, fd, charRootDir, NULL, O_WRONLY | O_CREAT | O_EXCL | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU | TAR_STORE_SELINUX) != 0) {
				close(fd);
				LOGINFO("tar_fdopen failed\n");
				gui_err("backup_error=Error creating backup.");
				return -1;
			}
			return 0;
		}
	} else {
		// Not compressed or encrypted
		init_libtar_buffer(0);
		if (tar_open(&t, charTarFile, &type, O_WRONLY | O_CREAT | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU | TAR_STORE_SELINUX) == -1) {
			LOGINFO("tar_open error opening '%s'\n", tarfn.c_str());
			gui_err("backup_error=Error creating backup.");
			return -1;
		}
	}
	return 0;
}

int twrpTar::openTar() {
	char* charRootDir = (char*) tardir.c_str();
	char* charTarFile = (char*) tarfn.c_str();
	string Password;

	if (Archive_Current_Type == 3) {
		LOGINFO("Opening encrypted and compressed backup...\n");
		int i, pipes[4];
		int input_fd = open(tarfn.c_str(), O_RDONLY | O_LARGEFILE);
		if (input_fd < 0) {
			gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(tarfn)(strerror(errno)));
			return -1;
		}

		if (pipe(pipes) < 0) {
			LOGINFO("Error creating first pipe\n");
			gui_err("restore_error=Error during restore process.");
			close(input_fd);
			return -1;
		}
		if (pipe(pipes + 2) < 0) {
			LOGINFO("Error creating second pipe\n");
			gui_err("restore_error=Error during restore process.");
			close(pipes[0]);
			close(pipes[1]);
			close(input_fd);
			return -1;
		}
		oaes_pid = fork();

		if (oaes_pid < 0) {
			LOGINFO("pigz fork() failed\n");
			gui_err("restore_error=Error during restore process.");
			close(input_fd);
			for (i = 0; i < 4; i++)
				close(pipes[i]); // close all
			return -1;
		} else if (oaes_pid == 0) {
			// openaes Child
			close(pipes[0]); // Close pipes that are not used by this child
			close(pipes[2]);
			close(pipes[3]);
			close(0);
			dup2(input_fd, 0);
			close(1);
			dup2(pipes[1], 1);
			if (execlp("openaes", "openaes", "dec", "--key", password.c_str(), NULL) < 0) {
				LOGINFO("execlp openaes ERROR!\n");
				gui_err("restore_error=Error during restore process.");
				close(input_fd);
				close(pipes[1]);
				_exit(-1);
			}
		} else {
			// Parent
			pigz_pid = fork();

			if (pigz_pid < 0) {
				LOGINFO("openaes fork() failed\n");
				gui_err("restore_error=Error during restore process.");
				close(input_fd);
				for (i = 0; i < 4; i++)
					close(pipes[i]); // close all
				return -1;
			} else if (pigz_pid == 0) {
				// pigz Child
				close(pipes[1]); // Close pipes not used by this child
				close(pipes[2]);
				close(0);
				dup2(pipes[0], 0);
				close(1);
				dup2(pipes[3], 1);
				if (execlp("pigz", "pigz", "-d", "-c", NULL) < 0) {
					LOGINFO("execlp pigz ERROR!\n");
					gui_err("restore_error=Error during restore process.");
					close(input_fd);
					close(pipes[0]);
					close(pipes[3]);
					_exit(-1);
				}
			} else {
				// Parent
				close(pipes[0]); // Close pipes not used by parent
				close(pipes[1]);
				close(pipes[3]);
				fd = pipes[2];
				if(tar_fdopen(&t, fd, charRootDir, NULL, O_RDONLY | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU | TAR_STORE_SELINUX) != 0) {
					close(fd);
					LOGINFO("tar_fdopen failed\n");
					gui_err("restore_error=Error during restore process.");
					return -1;
				}
			}
		}
	} else if (Archive_Current_Type == 2) {
		LOGINFO("Opening encrypted backup...\n");
		int oaesfd[2];
		int input_fd = open(tarfn.c_str(), O_RDONLY | O_LARGEFILE);
		if (input_fd < 0) {
			gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(tarfn)(strerror(errno)));
			return -1;
		}

		if (pipe(oaesfd) < 0) {
			LOGINFO("Error creating pipe\n");
			gui_err("restore_error=Error during restore process.");
			close(input_fd);
			return -1;
		}

		oaes_pid = fork();
		if (oaes_pid < 0) {
			LOGINFO("fork() failed\n");
			gui_err("restore_error=Error during restore process.");
			close(input_fd);
			close(oaesfd[0]);
			close(oaesfd[1]);
			return -1;
		} else if (oaes_pid == 0) {
			// Child
			close(oaesfd[0]); // Close unused pipe
			close(0);   // close stdin
			dup2(oaesfd[1], 1); // remap stdout
			dup2(input_fd, 0); // remap input fd to stdin
			if (execlp("openaes", "openaes", "dec", "--key", password.c_str(), NULL) < 0) {
				LOGINFO("execlp openaes ERROR!\n");
				gui_err("restore_error=Error during restore process.");
				close(input_fd);
				close(oaesfd[1]);
				_exit(-1);
			}
		} else {
			// Parent
			close(oaesfd[1]); // close parent output
			fd = oaesfd[0];   // copy parent input
			if(tar_fdopen(&t, fd, charRootDir, NULL, O_RDONLY | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU | TAR_STORE_SELINUX) != 0) {
				close(fd);
				LOGINFO("tar_fdopen failed\n");
				gui_err("restore_error=Error during restore process.");
				return -1;
			}
		}
	} else if (Archive_Current_Type == 1) {
		LOGINFO("Opening as a gzip...\n");
		int pigzfd[2];
		int input_fd = open(tarfn.c_str(), O_RDONLY | O_LARGEFILE);
		if (input_fd < 0) {
			gui_msg(Msg(msg::kError, "error_opening_strerr=Error opening: '{1}' ({2})")(tarfn)(strerror(errno)));
			return -1;
		}
		if (pipe(pigzfd) < 0) {
			LOGINFO("Error creating pipe\n");
			gui_err("restore_error=Error during restore process.");
			close(input_fd);
			return -1;
		}

		pigz_pid = fork();
		if (pigz_pid < 0) {
			LOGINFO("fork() failed\n");
			gui_err("restore_error=Error during restore process.");
			close(input_fd);
			close(pigzfd[0]);
			close(pigzfd[1]);
			return -1;
		} else if (pigz_pid == 0) {
			// Child
			close(pigzfd[0]);
			dup2(input_fd, 0); // remap input fd to stdin
			dup2(pigzfd[1], 1); // remap stdout
			if (execlp("pigz", "pigz", "-d", "-c", NULL) < 0) {
				close(pigzfd[1]);
				close(input_fd);
				LOGINFO("execlp openaes ERROR!\n");
				gui_err("restore_error=Error during restore process.");
				_exit(-1);
			}
		} else {
			// Parent
			close(pigzfd[1]); // close parent output
			fd = pigzfd[0];   // copy parent input
			if(tar_fdopen(&t, fd, charRootDir, NULL, O_RDONLY | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU | TAR_STORE_SELINUX) != 0) {
				close(fd);
				LOGINFO("tar_fdopen failed\n");
				gui_err("restore_error=Error during restore process.");
				return -1;
			}
		}
	} else if (tar_open(&t, charTarFile, NULL, O_RDONLY | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, TAR_GNU | TAR_STORE_SELINUX) != 0) {
		LOGINFO("Unable to open tar archive '%s'\n", charTarFile);
		gui_err("restore_error=Error during restore process.");
		return -1;
	}
	return 0;
}

string twrpTar::Strip_Root_Dir(string Path) {
	string temp;
	size_t slash;

	if (Path.substr(0, 1) == "/")
		temp = Path.substr(1, Path.size() - 1);
	else
		temp = Path;
	slash = temp.find("/");
	if (slash == string::npos)
		return temp;
	else {
		string stripped;

		stripped = temp.substr(slash, temp.size() - slash);
		return stripped;
	}
	return temp;
}

int twrpTar::addFile(string fn, bool include_root) {
	char* charTarFile = (char*) fn.c_str();
	if (include_root) {
		if (tar_append_file(t, charTarFile, NULL) == -1)
			return -1;
	} else {
		string temp = Strip_Root_Dir(fn);
		char* charTarPath = (char*) temp.c_str();
		if (tar_append_file(t, charTarFile, charTarPath) == -1)
			return -1;
	}
	return 0;
}

int twrpTar::closeTar() {
	flush_libtar_buffer(t->fd);
	if (tar_append_eof(t) != 0) {
		LOGINFO("tar_append_eof(): %s\n", strerror(errno));
		tar_close(t);
		return -1;
	}
	if (tar_close(t) != 0) {
		LOGINFO("Unable to close tar archive: '%s'\n", tarfn.c_str());
		return -1;
	}
	if (Archive_Current_Type > 0) {
		close(fd);
		int status;
		if (pigz_pid > 0 && TWFunc::Wait_For_Child(pigz_pid, &status, "pigz") != 0)
			return -1;
		if (oaes_pid > 0 && TWFunc::Wait_For_Child(oaes_pid, &status, "openaes") != 0)
			return -1;
	}
	free_libtar_buffer();
	if (use_compression && !use_encryption) {
		string gzname = tarfn + ".gz";
		if (TWFunc::Path_Exists(gzname)) {
			rename(gzname.c_str(), tarfn.c_str());
		}
	}
	if (TWFunc::Get_File_Size(tarfn) == 0) {
		gui_msg(Msg(msg::kError, "backup_size=Backup file size for '{1}' is 0 bytes.")(tarfn));
		return -1;
	}
#ifndef BUILD_TWRPTAR_MAIN
	tw_set_default_metadata(tarfn.c_str());
#endif
	return 0;
}

int twrpTar::removeEOT(string tarFile) {
	char* charTarFile = (char*) tarFile.c_str();
	off_t tarFileEnd = 0;
	while (th_read(t) == 0) {
		if (TH_ISREG(t))
			tar_skip_regfile(t);
		tarFileEnd = lseek(t->fd, 0, SEEK_CUR);
	}
	if (tar_close(t) == -1)
		return -1;
	if (tarFileEnd > 0 && truncate(charTarFile, tarFileEnd) == -1)
		return -1;
	return 0;
}

int twrpTar::entryExists(string entry) {
	char* searchstr = (char*)entry.c_str();
	int ret;

	Archive_Current_Type = TWFunc::Get_File_Type(tarfn);

	if (openTar() == -1)
		ret = 0;
	else
		ret = tar_find(t, searchstr);

	if (closeTar() != 0)
		LOGINFO("Unable to close tar after searching for entry.\n");

	return ret;
}

unsigned long long twrpTar::get_size() {
	if (TWFunc::Path_Exists(tarfn)) {
		LOGINFO("Single archive\n");
		int type = 0;
		return uncompressedSize(tarfn, &type);
	} else {
		LOGINFO("Multiple archives\n");
		string temp;
		char actual_filename[255];
		int archive_count = 0, i, type = 0, temp_type = 0;
		unsigned long long total_restore_size = 0;

		basefn = tarfn;
		temp = basefn + "%i%02i";
		tarfn += "000";
		thread_id = 0;
		sprintf(actual_filename, temp.c_str(), thread_id, archive_count);
		if (!TWFunc::Path_Exists(actual_filename)) {
			LOGERR("Unable to locate '%s' or '%s'\n", basefn.c_str(), tarfn.c_str());
			return 0;
		}
		for (i = 0; i < 9; i++) {
			archive_count = 0;
			sprintf(actual_filename, temp.c_str(), i, archive_count);
			while (TWFunc::Path_Exists(actual_filename)) {
				total_restore_size += uncompressedSize(actual_filename, &temp_type);
				if (temp_type > type)
					type = temp_type;
				archive_count++;
				if (archive_count > 99)
					break;
				sprintf(actual_filename, temp.c_str(), i, archive_count);
			}
		}
#ifndef BUILD_TWRPTAR_MAIN
		InfoManager backup_info(backup_folder + "/" + partition_name + ".info");
		backup_info.SetValue("backup_size", total_restore_size);
		backup_info.SetValue("backup_type", type);
		backup_info.SaveValues();
#endif //ndef BUILD_TWRPTAR_MAIN
		return total_restore_size;
	}
	return 0;
}

unsigned long long twrpTar::uncompressedSize(string filename, int *archive_type) {
	int type = 0;
	unsigned long long total_size = 0;
	string Tar, Command, result;
	vector<string> split;

	Tar = TWFunc::Get_Filename(filename);
	type = TWFunc::Get_File_Type(filename);
	if (type == 0) {
		total_size = TWFunc::Get_File_Size(filename);
		*archive_type = 0;
	} else if (type == 1) {
		// Compressed
		Command = "pigz -l '" + filename + "'";
		/* if we set Command = "pigz -l " + tarfn + " | sed '1d' | cut -f5 -d' '";
		we get the uncompressed size at once. */
		TWFunc::Exec_Cmd(Command, result);
		if (!result.empty()) {
			/* Expected output:
			compressed original  reduced name
			95855838   179403776 -1.3%   data.yaffs2.win
			^
			split[5]
			*/
			split = TWFunc::split_string(result, ' ', true);
			if (split.size() > 4)
			total_size = atoi(split[5].c_str());
		}
		*archive_type = 1;
	} else if (type == 2) {
		// File is encrypted and may be compressed
		int ret = TWFunc::Try_Decrypting_File(filename, password);
		*archive_type = 2;
		if (ret < 1) {
			gui_msg(Msg(msg::kError, "fail_decrypt_tar=Failed to decrypt tar file '{1}'")(tarfn));
			total_size = TWFunc::Get_File_Size(filename);
		} else if (ret == 1) {
			LOGERR("Decrypted file is not in tar format.\n");
			total_size = TWFunc::Get_File_Size(filename);
		} else if (ret == 3) {
			*archive_type = 3;
			Command = "openaes dec --key \"" + password + "\" --in '" + filename + "' | pigz -l";
			/* if we set Command = "pigz -l " + tarfn + " | sed '1d' | cut -f5 -d' '";
			we get the uncompressed size at once. */
			TWFunc::Exec_Cmd(Command, result);
			if (!result.empty()) {
				LOGINFO("result was: '%s'\n", result.c_str());
				/* Expected output:
				compressed original  reduced name
				95855838   179403776 -1.3%   data.yaffs2.win
				^
				split[5]
				*/
				split = TWFunc::split_string(result, ' ', true);
				if (split.size() > 4)
				total_size = atoi(split[5].c_str());
			}
		} else {
			total_size = TWFunc::Get_File_Size(filename);
		}
	}

	return total_size;
}

extern "C" ssize_t write_tar(int fd, const void *buffer, size_t size) {
	return (ssize_t) write_libtar_buffer(fd, buffer, size);
}
