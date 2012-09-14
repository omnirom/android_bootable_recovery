/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <ctype.h>
#include "cutils/misc.h"
#include "cutils/properties.h"
#include <dirent.h>
#include <getopt.h>
#include <linux/input.h>
#include <signal.h>
#include <sys/limits.h>
#include <termios.h>
#include <time.h>
#include <sys/vfs.h>

#include "tw_reboot.h"
#include "bootloader.h"
#include "common.h"
#include "extra-functions.h"
#include "minuitwrp/minui.h"
#include "minzip/DirUtil.h"
#include "minzip/Zip.h"
#include "recovery_ui.h"
#include "roots.h"
#include "data.h"
#include "variables.h"
#include "mincrypt/rsa.h"
#include "verifier.h"
#include "mincrypt/sha.h"

#ifndef PUBLIC_KEYS_FILE
#define PUBLIC_KEYS_FILE "/res/keys"
#endif
#ifndef ASSUMED_UPDATE_BINARY_NAME
#define ASSUMED_UPDATE_BINARY_NAME  "META-INF/com/google/android/update-binary"
#endif
enum { INSTALL_SUCCESS, INSTALL_ERROR, INSTALL_CORRUPT };

//kang system() from bionic/libc/unistd and rename it __system() so we can be even more hackish :)
#undef _PATH_BSHELL
#define _PATH_BSHELL "/sbin/sh"

extern char **environ;

int __system(const char *command) {
  pid_t pid;
	sig_t intsave, quitsave;
	sigset_t mask, omask;
	int pstat;
	char *argp[] = {"sh", "-c", NULL, NULL};

	if (!command)		/* just checking... */
		return(1);

	argp[2] = (char *)command;

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &mask, &omask);
	switch (pid = vfork()) {
	case -1:			/* error */
		sigprocmask(SIG_SETMASK, &omask, NULL);
		return(-1);
	case 0:				/* child */
		sigprocmask(SIG_SETMASK, &omask, NULL);
		execve(_PATH_BSHELL, argp, environ);
    _exit(127);
  }

	intsave = (sig_t)  bsd_signal(SIGINT, SIG_IGN);
	quitsave = (sig_t) bsd_signal(SIGQUIT, SIG_IGN);
	pid = waitpid(pid, (int *)&pstat, 0);
	sigprocmask(SIG_SETMASK, &omask, NULL);
	(void)bsd_signal(SIGINT, intsave);
	(void)bsd_signal(SIGQUIT, quitsave);
	return (pid == -1 ? -1 : pstat);
}

static struct pid {
	struct pid *next;
	FILE *fp;
	pid_t pid;
} *pidlist;

FILE *__popen(const char *program, const char *type) {
	struct pid * volatile cur;
	FILE *iop;
	int pdes[2];
	pid_t pid;

	if ((*type != 'r' && *type != 'w') || type[1] != '\0') {
		errno = EINVAL;
		return (NULL);
	}

	if ((cur = malloc(sizeof(struct pid))) == NULL)
		return (NULL);

	if (pipe(pdes) < 0) {
		free(cur);
		return (NULL);
	}

	switch (pid = vfork()) {
	case -1:			/* Error. */
		(void)close(pdes[0]);
		(void)close(pdes[1]);
		free(cur);
		return (NULL);
		/* NOTREACHED */
	case 0:				/* Child. */
	    {
		struct pid *pcur;
		/*
		 * because vfork() instead of fork(), must leak FILE *,
		 * but luckily we are terminally headed for an execl()
		 */
		for (pcur = pidlist; pcur; pcur = pcur->next)
			close(fileno(pcur->fp));

		if (*type == 'r') {
			int tpdes1 = pdes[1];

			(void) close(pdes[0]);
			/*
			 * We must NOT modify pdes, due to the
			 * semantics of vfork.
			 */
			if (tpdes1 != STDOUT_FILENO) {
				(void)dup2(tpdes1, STDOUT_FILENO);
				(void)close(tpdes1);
				tpdes1 = STDOUT_FILENO;
			}
		} else {
			(void)close(pdes[1]);
			if (pdes[0] != STDIN_FILENO) {
				(void)dup2(pdes[0], STDIN_FILENO);
				(void)close(pdes[0]);
			}
		}
		execl(_PATH_BSHELL, "sh", "-c", program, (char *)NULL);
		_exit(127);
		/* NOTREACHED */
	    }
	}

	/* Parent; assume fdopen can't fail. */
	if (*type == 'r') {
		iop = fdopen(pdes[0], type);
		(void)close(pdes[1]);
	} else {
		iop = fdopen(pdes[1], type);
		(void)close(pdes[0]);
	}

	/* Link into list of file descriptors. */
	cur->fp = iop;
	cur->pid =  pid;
	cur->next = pidlist;
	pidlist = cur;

	return (iop);
}

/*
 * pclose --
 *	Pclose returns -1 if stream is not associated with a `popened' command,
 *	if already `pclosed', or waitpid returns an error.
 */
int __pclose(FILE *iop) {
	struct pid *cur, *last;
	int pstat;
	pid_t pid;

	/* Find the appropriate file pointer. */
	for (last = NULL, cur = pidlist; cur; last = cur, cur = cur->next)
		if (cur->fp == iop)
			break;

	if (cur == NULL)
		return (-1);

	(void)fclose(iop);

	do {
		pid = waitpid(cur->pid, &pstat, 0);
	} while (pid == -1 && errno == EINTR);

	/* Remove the entry from the linked list. */
	if (last == NULL)
		pidlist = cur->next;
	else
		last->next = cur->next;
	free(cur);

	return (pid == -1 ? -1 : pstat);
}

//partial kangbang from system/vold
#ifndef CUSTOM_LUN_FILE
#define CUSTOM_LUN_FILE "/sys/devices/platform/usb_mass_storage/lun%d/file"
#endif

int usb_storage_enable(void)
{
    int fd;
	char lun_file[255];

	if (DataManager_GetIntValue(TW_HAS_DUAL_STORAGE) == 1 && DataManager_GetIntValue(TW_HAS_DATA_MEDIA) == 0) {
		Volume *vol = volume_for_path(DataManager_GetSettingsStoragePath());
		if (!vol)
		{
			LOGE("Unable to locate volume information.");
			return -1;
		}

		sprintf(lun_file, CUSTOM_LUN_FILE, 0);

		if ((fd = open(lun_file, O_WRONLY)) < 0)
		{
			LOGE("Unable to open ums lunfile '%s': (%s)\n", lun_file, strerror(errno));
			return -1;
		}

		if ((write(fd, vol->device, strlen(vol->device)) < 0) &&
			(!vol->device2 || (write(fd, vol->device, strlen(vol->device2)) < 0))) {
			LOGE("Unable to write to ums lunfile '%s': (%s)\n", lun_file, strerror(errno));
			close(fd);
			return -1;
		}
		close(fd);

		Volume *vol2 = volume_for_path(DataManager_GetStrValue(TW_EXTERNAL_PATH));
		if (!vol)
		{
			LOGE("Unable to locate volume information.\n");
			return -1;
		}

		sprintf(lun_file, CUSTOM_LUN_FILE, 1);

		if ((fd = open(lun_file, O_WRONLY)) < 0)
		{
			LOGE("Unable to open ums lunfile '%s': (%s)\n", lun_file, strerror(errno));
			return -1;
		}

		if ((write(fd, vol2->device, strlen(vol2->device)) < 0) &&
			(!vol2->device2 || (write(fd, vol2->device, strlen(vol2->device2)) < 0))) {
			LOGE("Unable to write to ums lunfile '%s': (%s)\n", lun_file, strerror(errno));
			close(fd);
			return -1;
		}
		close(fd);
	} else {
		if (DataManager_GetIntValue(TW_HAS_DATA_MEDIA) == 0)
			strcpy(lun_file, DataManager_GetCurrentStoragePath());
		else
			strcpy(lun_file, DataManager_GetStrValue(TW_EXTERNAL_PATH));

		Volume *vol = volume_for_path(lun_file);
		if (!vol)
		{
			LOGE("Unable to locate volume information.\n");
			return -1;
		}

		sprintf(lun_file, CUSTOM_LUN_FILE, 0);

		if ((fd = open(lun_file, O_WRONLY)) < 0)
		{
			LOGE("Unable to open ums lunfile '%s': (%s)\n", lun_file, strerror(errno));
			return -1;
		}

		if ((write(fd, vol->device, strlen(vol->device)) < 0) &&
			(!vol->device2 || (write(fd, vol->device, strlen(vol->device2)) < 0))) {
			LOGE("Unable to write to ums lunfile '%s': (%s)\n", lun_file, strerror(errno));
			close(fd);
			return -1;
		}
		close(fd);
	}
	return 0;
}

int usb_storage_disable(void)
{
    int fd, index;
	char lun_file[255];

	for (index=0; index<2; index++) {
		sprintf(lun_file, CUSTOM_LUN_FILE, index);

		if ((fd = open(lun_file, O_WRONLY)) < 0)
		{
			if (index == 0)
				LOGE("Unable to open ums lunfile '%s': (%s)", lun_file, strerror(errno));
			return -1;
		}

		char ch = 0;
		if (write(fd, &ch, 1) < 0)
		{
			if (index == 0)
				LOGE("Unable to write to ums lunfile '%s': (%s)", lun_file, strerror(errno));
			close(fd);
			return -1;
		}

		close(fd);
	}
    return 0;
}

void update_tz_environment_variables() {
    setenv("TZ", DataManager_GetStrValue(TW_TIME_ZONE_VAR), 1);
    tzset();
}

void run_script(const char *str1, const char *str2, const char *str3, const char *str4, const char *str5, const char *str6, const char *str7, int request_confirm)
{
	ui_print("%s", str1);
        //ui_clear_key_queue();
	ui_print("\nPress Power to confirm,");
       	ui_print("\nany other key to abort.\n");
	int confirm;
	/*if (request_confirm) // this option is used to skip the confirmation when the gui is in use
		confirm = ui_wait_key();
	else*/
		confirm = KEY_POWER;
	
		if (confirm == BTN_MOUSE || confirm == KEY_POWER || confirm == SELECT_ITEM) {
                	ui_print("%s", str2);
		        pid_t pid = fork();
                	if (pid == 0) {
                		char *args[] = { "/sbin/sh", "-c", (char*)str3, "1>&2", NULL };
                	        execv("/sbin/sh", args);
                	        fprintf(stderr, str4, strerror(errno));
                	        _exit(-1);
                	}
			int status;
			while (waitpid(pid, &status, WNOHANG) == 0) {
				ui_print(".");
               		        sleep(1);
			}
                	ui_print("\n");
			if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
                		ui_print("%s", str5);
                	} else {
                		ui_print("%s", str6);
                	}
		} else {
	       		ui_print("%s", str7);
       	        }
		//if (!ui_text_visible()) return;
}

void check_and_run_script(const char* script_file, const char* display_name)
{
	// Check for and run startup script if script exists
	struct statfs st;
	if (statfs(script_file, &st) == 0) {
		ui_print("Running %s script...\n", display_name);
		char command[255];
		strcpy(command, "chmod 755 ");
		strcat(command, script_file);
		__system(command);
		__system(script_file);
		ui_print("\nFinished running %s script.\n", display_name);
	}
}

int check_backup_name(int show_error) {
	// Check the backup name to ensure that it is the correct size and contains only valid characters
	// and that a backup with that name doesn't already exist
	char backup_name[MAX_BACKUP_NAME_LEN];
	char backup_loc[255], tw_image_dir[255];
	int copy_size = strlen(DataManager_GetStrValue(TW_BACKUP_NAME));
	int index, cur_char;
	struct statfs st;

	// Check size
	if (copy_size > MAX_BACKUP_NAME_LEN) {
		if (show_error)
			LOGE("Backup name is too long.\n");
		return -2;
	}

	// Check characters
	strncpy(backup_name, DataManager_GetStrValue(TW_BACKUP_NAME), copy_size);
	if (strcmp(backup_name, "0") == 0)
		return 0; // A "0" (zero) means to use the current timestamp for the backup name
	for (index=0; index<copy_size; index++) {
		cur_char = (int)backup_name[index];
		if ((cur_char >= 48  && cur_char <= 57) || (cur_char >= 65 && cur_char <= 91) || cur_char == 93 || cur_char == 95 || (cur_char >= 97 && cur_char <= 123) || cur_char == 125 || cur_char == 45 || cur_char == 46) {
			// These are valid characters
			// Numbers
			// Upper case letters
			// Lower case letters
			// and -_.{}[]
		} else {
			if (show_error)
				LOGE("Backup name '%s' contains invalid character: '%c'\n", backup_name, (char)cur_char);
			return -3;
		}
	}

	// Check to make sure that a backup with this name doesn't already exist
	strcpy(backup_loc, DataManager_GetStrValue(TW_BACKUPS_FOLDER_VAR));
	sprintf(tw_image_dir,"%s/%s/.", backup_loc, backup_name);
    if (statfs(tw_image_dir, &st) == 0) {
		if (show_error)
			LOGE("A backup with this name already exists.\n");
		return -4;
	}

	// No problems found, return 0
	return 0;
}

static const char *COMMAND_FILE = "/cache/recovery/command";
static const char *INTENT_FILE = "/cache/recovery/intent";
static const char *LOG_FILE = "/cache/recovery/log";
static const char *LAST_LOG_FILE = "/cache/recovery/last_log";
static const char *LAST_INSTALL_FILE = "/cache/recovery/last_install";
static const char *CACHE_ROOT = "/cache";
static const char *SDCARD_ROOT = "/sdcard";
static const char *TEMPORARY_LOG_FILE = "/tmp/recovery.log";
static const char *TEMPORARY_INSTALL_FILE = "/tmp/last_install";

// close a file, log an error if the error indicator is set
static void check_and_fclose(FILE *fp, const char *name) {
    fflush(fp);
    if (ferror(fp)) LOGE("Error in %s\n(%s)\n", name, strerror(errno));
    fclose(fp);
}

static void copy_log_file(const char* source, const char* destination, int append) {
    FILE *log = fopen_path(destination, append ? "a" : "w");
    if (log == NULL) {
        LOGE("Can't open %s\n", destination);
    } else {
        FILE *tmplog = fopen(source, "r");
        if (tmplog != NULL) {
            if (append) {
                fseek(tmplog, tmplog_offset, SEEK_SET);  // Since last write
            }
            char buf[4096];
            while (fgets(buf, sizeof(buf), tmplog)) fputs(buf, log);
            if (append) {
                tmplog_offset = ftell(tmplog);
            }
            check_and_fclose(tmplog, source);
        }
        check_and_fclose(log, destination);
    }
}

// clear the recovery command and prepare to boot a (hopefully working) system,
// copy our log file to cache as well (for the system to read), and
// record any intent we were asked to communicate back to the system.
// this function is idempotent: call it as many times as you like.
void twfinish_recovery(const char *send_intent) {
    // By this point, we're ready to return to the main system...
    if (send_intent != NULL) {
        FILE *fp = fopen_path(INTENT_FILE, "w");
        if (fp == NULL) {
            LOGE("Can't open %s\n", INTENT_FILE);
        } else {
            fputs(send_intent, fp);
            check_and_fclose(fp, INTENT_FILE);
        }
    }

    // Copy logs to cache so the system can find out what happened.
    copy_log_file(TEMPORARY_LOG_FILE, LOG_FILE, true);
    copy_log_file(TEMPORARY_LOG_FILE, LAST_LOG_FILE, false);
    copy_log_file(TEMPORARY_INSTALL_FILE, LAST_INSTALL_FILE, false);
    chmod(LOG_FILE, 0600);
    chown(LOG_FILE, 1000, 1000);   // system user
    chmod(LAST_LOG_FILE, 0640);
    chmod(LAST_INSTALL_FILE, 0644);

    // Reset to normal system boot so recovery won't cycle indefinitely.
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    set_bootloader_message(&boot);

    // Remove the command file, so recovery won't repeat indefinitely.
    if (ensure_path_mounted(COMMAND_FILE) != 0 ||
        (unlink(COMMAND_FILE) && errno != ENOENT)) {
        LOGW("Can't unlink %s\n", COMMAND_FILE);
    }

    ensure_path_unmounted(CACHE_ROOT);
    sync();  // For good measure.
}

