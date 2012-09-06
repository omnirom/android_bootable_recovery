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

#include "../tw_reboot.h"
#include "../bootloader.h"
#include "../common.h"
#include "gui-functions.h"
#include "cutils/properties.h"
#include "../install.h"
#include "../minuitwrp/minui.h"
#include "../minzip/DirUtil.h"
#include "../minzip/Zip.h"
#include "../recovery_ui.h"
#include "../roots.h"
#include "../data.h"
#include "../variables.h"

//kang system() from bionic/libc/unistd and rename it __system() so we can be even more hackish :)
#undef _PATH_BSHELL
#define _PATH_BSHELL "/sbin/sh"

static const char *SIDELOAD_TEMP_DIR = "/tmp/sideload";
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

char* sanitize_device_id(char* id) {
        const char* whitelist ="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890-._";
        char* c = id;
        char* str = (int*) calloc(50, sizeof *id);
        while (*c)
        {
                if (strchr(whitelist, *c))
                {
                        strncat(str, c, 1);
                }
                c++;
        }
        return str;
}

#define CMDLINE_SERIALNO        "androidboot.serialno="
#define CMDLINE_SERIALNO_LEN    (strlen(CMDLINE_SERIALNO))
#define CPUINFO_SERIALNO        "Serial"
#define CPUINFO_SERIALNO_LEN    (strlen(CPUINFO_SERIALNO))
#define CPUINFO_HARDWARE        "Hardware"
#define CPUINFO_HARDWARE_LEN    (strlen(CPUINFO_HARDWARE))

char* get_path (char* path) {
        char *s;

        /* Go to the end of the string.  */
        s = path + strlen(path) - 1;

        /* Strip off trailing /s (unless it is also the leading /).  */
        while (path < s && s[0] == '/')
                s--;

        /* Strip the last component.  */
        while (path <= s && s[0] != '/')
                s--;

        while (path < s && s[0] == '/')
                s--;

        if (s < path)
                return ".";

        s[1] = '\0';
	return path;
}

char* basename(char* name) {
	const char* base;	
	for (base = name; *name; name++)
	{
		if(*name == '/')
		{
			base = name + 1;
		}		
	}
	return (char *) base;
}

/*
    Checks md5 for a path
    Return values:
        -1 : MD5 does not exist
        0 : Failed
        1 : Success
*/
int check_md5(char* path) {
    int o; 
    char cmd[PATH_MAX + 30];
    char md5file[PATH_MAX + 40];
    strcpy(md5file, path);
    strcat(md5file, ".md5");
    char dirpath[PATH_MAX];
    char* file;
    if (access(md5file, F_OK ) != -1) {
	strcpy(dirpath, md5file);
	get_path(dirpath);
	chdir(dirpath);
	file = basename(md5file);
	sprintf(cmd, "/sbin/busybox md5sum -c '%s'", file);
	FILE * cs = __popen(cmd, "r");
	char cs_s[PATH_MAX + 50];
	fgets(cs_s, PATH_MAX + 50, cs);
	char* OK = strstr(cs_s, "OK");
	if (OK != NULL) {
		printf("MD5 is good. returning 1\n");
		o = 1;
	}
	else {
		printf("MD5 is bad. return -2\n");
		o = -2;
	}

	__pclose(cs);
    } 
    else {
	//No md5 file
	printf("setting o to -1\n");
	o = -1;
    }

    return o;
}

static void set_sdcard_update_bootloader_message() {
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    set_bootloader_message(&boot);
}

static char* copy_sideloaded_package(const char* original_path) {
  if (ensure_path_mounted(original_path) != 0) {
    LOGE("Can't mount %s\n", original_path);
    return NULL;
  }

  if (ensure_path_mounted(SIDELOAD_TEMP_DIR) != 0) {
    LOGE("Can't mount %s\n", SIDELOAD_TEMP_DIR);
    return NULL;
  }

  if (mkdir(SIDELOAD_TEMP_DIR, 0700) != 0) {
    if (errno != EEXIST) {
      LOGE("Can't mkdir %s (%s)\n", SIDELOAD_TEMP_DIR, strerror(errno));
      return NULL;
    }
  }

  // verify that SIDELOAD_TEMP_DIR is exactly what we expect: a
  // directory, owned by root, readable and writable only by root.
  struct stat st;
  if (stat(SIDELOAD_TEMP_DIR, &st) != 0) {
    LOGE("failed to stat %s (%s)\n", SIDELOAD_TEMP_DIR, strerror(errno));
    return NULL;
  }
  if (!S_ISDIR(st.st_mode)) {
    LOGE("%s isn't a directory\n", SIDELOAD_TEMP_DIR);
    return NULL;
  }
  if ((st.st_mode & 0777) != 0700) {
    LOGE("%s has perms %o\n", SIDELOAD_TEMP_DIR, st.st_mode);
    return NULL;
  }
  if (st.st_uid != 0) {
    LOGE("%s owned by %lu; not root\n", SIDELOAD_TEMP_DIR, st.st_uid);
    return NULL;
  }

  char copy_path[PATH_MAX];
  strcpy(copy_path, SIDELOAD_TEMP_DIR);
  strcat(copy_path, "/package.zip");

  char* buffer = malloc(BUFSIZ);
  if (buffer == NULL) {
    LOGE("Failed to allocate buffer\n");
    return NULL;
  }

  size_t read;
  FILE* fin = fopen(original_path, "rb");
  if (fin == NULL) {
    LOGE("Failed to open %s (%s)\n", original_path, strerror(errno));
    return NULL;
  }
  FILE* fout = fopen(copy_path, "wb");
  if (fout == NULL) {
    LOGE("Failed to open %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  while ((read = fread(buffer, 1, BUFSIZ, fin)) > 0) {
    if (fwrite(buffer, 1, read, fout) != read) {
      LOGE("Short write of %s (%s)\n", copy_path, strerror(errno));
      return NULL;
    }
  }

  free(buffer);

  if (fclose(fout) != 0) {
    LOGE("Failed to close %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  if (fclose(fin) != 0) {
    LOGE("Failed to close %s (%s)\n", original_path, strerror(errno));
    return NULL;
  }

  // "adb push" is happy to overwrite read-only files when it's
  // running as root, but we'll try anyway.
  if (chmod(copy_path, 0400) != 0) {
    LOGE("Failed to chmod %s (%s)\n", copy_path, strerror(errno));
    return NULL;
  }

  return strdup(copy_path);
}

int install_zip_package(const char* zip_path_filename) {
	int result = 0;

    //mount_current_storage();
	int md5_req = DataManager_GetIntValue(TW_FORCE_MD5_CHECK_VAR);
	if (md5_req == 1) {
		ui_print("\n-- Verify md5 for %s", zip_path_filename);
		int md5chk = check_md5((char*) zip_path_filename);
		if (md5chk == 1) {
			ui_print("\n-- Md5 verified, continue");
			result = 0;
		}
		else if (md5chk == -1) {
			if (md5_req == 1) {
				ui_print("\n-- No md5 file found!");
				ui_print("\n-- Aborting install");
				result = INSTALL_ERROR;
			}
			else {
				ui_print("\n-- No md5 file found, ignoring");
			}
		}
		else if (md5chk == -2) {
			ui_print("\n-- md5 file doesn't match!");
			ui_print("\n-- Aborting install");
			result = INSTALL_ERROR;
		}
		printf("%d\n", result);
	}
	if (result != INSTALL_ERROR) {
		ui_print("\n-- Install %s ...\n", zip_path_filename);
		set_sdcard_update_bootloader_message();
		char* copy;
		if (DataManager_GetIntValue(TW_FLASH_ZIP_IN_PLACE) == 1 && strlen(zip_path_filename) > 6 && strncmp(zip_path_filename, "/cache", 6) != 0) {
			copy = strdup(zip_path_filename);
		} else {
			copy = copy_sideloaded_package(zip_path_filename);
			//unmount_current_storage();
		}
		if (copy) {
			result = really_install_package(copy, 0);
			free(copy);
			//update_system_details();
		} else {
			result = INSTALL_ERROR;
		}
	}
    //mount_current_storage();
    //finish_recovery(NULL);
	return result;
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

void wipe_dalvik_cache()
{
        //ui_set_background(BACKGROUND_ICON_WIPE);
        ensure_path_mounted("/data");
        ensure_path_mounted("/cache");
        ui_print("\n-- Wiping Dalvik Cache Directories...\n");
        __system("rm -rf /data/dalvik-cache");
        ui_print("Cleaned: /data/dalvik-cache...\n");
        __system("rm -rf /cache/dalvik-cache");
        ui_print("Cleaned: /cache/dalvik-cache...\n");
        __system("rm -rf /cache/dc");
        ui_print("Cleaned: /cache/dc\n");

        struct stat st;
		LOGE("TODO: Re-implement wipe dalvik into Partition Manager!\n");
        if (1) //if (0 != stat(sde.blk, &st))
        {
            ui_print("/sd-ext not present, skipping\n");
        } else {
        	__system("mount /sd-ext");
    	    LOGI("Mounting /sd-ext\n");
    	    if (stat("/sd-ext/dalvik-cache",&st) == 0)
    	    {
                __system("rm -rf /sd-ext/dalvik-cache");
        	    ui_print("Cleaned: /sd-ext/dalvik-cache...\n");
    	    }
        }
        ensure_path_unmounted("/data");
        ui_print("-- Dalvik Cache Directories Wipe Complete!\n\n");
        //ui_set_background(BACKGROUND_ICON_MAIN);
        //if (!ui_text_visible()) return;
}

// BATTERY STATS
void wipe_battery_stats()
{
    ensure_path_mounted("/data");
    struct stat st;
    if (0 != stat("/data/system/batterystats.bin", &st))
    {
        ui_print("No Battery Stats Found. No Need To Wipe.\n");
    } else {
        //ui_set_background(BACKGROUND_ICON_WIPE);
        remove("/data/system/batterystats.bin");
        ui_print("Cleared: Battery Stats...\n");
        ensure_path_unmounted("/data");
    }
}

// ROTATION SETTINGS
void wipe_rotate_data()
{
    //ui_set_background(BACKGROUND_ICON_WIPE);
    ensure_path_mounted("/data");
    __system("rm -r /data/misc/akmd*");
    __system("rm -r /data/misc/rild*");
    ui_print("Cleared: Rotatation Data...\n");
    ensure_path_unmounted("/data");
}   

void fix_perms()
{
	ensure_path_mounted("/data");
	ensure_path_mounted("/system");
	//ui_show_progress(1,30);
    ui_print("\n-- Fixing Permissions\n");
	ui_print("This may take a few minutes.\n");
	__system("./sbin/fix_permissions.sh");
	ui_print("-- Done.\n\n");
	//ui_reset_progress();
}

int get_battery_level(void)
{
    static int lastVal = -1;
    static time_t nextSecCheck = 0;

    struct timeval curTime;
    gettimeofday(&curTime, NULL);
    if (curTime.tv_sec > nextSecCheck)
    {
        char cap_s[4];
        FILE * cap = fopen("/sys/class/power_supply/battery/capacity","rt");
        if (cap)
        {
            fgets(cap_s, 4, cap);
            fclose(cap);
            lastVal = atoi(cap_s);
            if (lastVal > 100)  lastVal = 101;
            if (lastVal < 0)    lastVal = 0;
        }
        nextSecCheck = curTime.tv_sec + 60;
    }
    return lastVal;
}

char* 
print_batt_cap()  {
	char* full_cap_s = (char*)malloc(30);
	char full_cap_a[30];
	
	int cap_i = get_battery_level();
    
    //int len = strlen(cap_s);
	//if (cap_s[len-1] == '\n') {
	//	cap_s[len-1] = 0;
	//}
	
	// Get a usable time
	struct tm *current;
	time_t now;
	now = time(0);
	current = localtime(&now);
	
	sprintf(full_cap_a, "Battery Level: %i%% @ %02D:%02D", cap_i, current->tm_hour, current->tm_min);
	strcpy(full_cap_s, full_cap_a);
	
	return full_cap_s;
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

void install_htc_dumlock(void)
{
	struct statfs fs1, fs2;
	int need_libs = 0;

	ui_print("Installing HTC Dumlock to system...\n");
	ensure_path_mounted("/system");
	__system("cp /res/htcd/htcdumlocksys /system/bin/htcdumlock && chmod 755 /system/bin/htcdumlock");
	if (statfs("/system/bin/flash_image", &fs1) != 0) {
		ui_print("Installing flash_image...\n");
		__system("cp /res/htcd/flash_imagesys /system/bin/flash_image && chmod 755 /system/bin/flash_image");
		need_libs = 1;
	} else
		ui_print("flash_image is already installed, skipping...\n");
	if (statfs("/system/bin/dump_image", &fs2) != 0) {
		ui_print("Installing dump_image...\n");
		__system("cp /res/htcd/dump_imagesys /system/bin/dump_image && chmod 755 /system/bin/dump_image");
		need_libs = 1;
	} else
		ui_print("dump_image is already installed, skipping...\n");
	if (need_libs) {
		ui_print("Installing libs needed for flash_image and dump_image...\n");
		__system("cp /res/htcd/libbmlutils.so /system/lib && chmod 755 /system/lib/libbmlutils.so");
		__system("cp /res/htcd/libflashutils.so /system/lib && chmod 755 /system/lib/libflashutils.so");
		__system("cp /res/htcd/libmmcutils.so /system/lib && chmod 755 /system/lib/libmmcutils.so");
		__system("cp /res/htcd/libmtdutils.so /system/lib && chmod 755 /system/lib/libmtdutils.so");
	}
	ui_print("Installing HTC Dumlock app...\n");
	ensure_path_mounted("/data");
	mkdir("/data/app", 0777);
	__system("rm /data/app/com.teamwin.htcdumlock*");
	__system("cp /res/htcd/HTCDumlock.apk /data/app/com.teamwin.htcdumlock.apk");
	sync();
	ui_print("HTC Dumlock is installed.\n");
}

void htc_dumlock_restore_original_boot(void)
{
	ui_print("Restoring original boot...\n");
	__system("htcdumlock restore");
	ui_print("Original boot restored.\n");
}

void htc_dumlock_reflash_recovery_to_boot(void)
{
	ui_print("Reflashing recovery to boot...\n");
	__system("htcdumlock recovery noreboot");
	ui_print("Recovery is flashed to boot.\n");
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
