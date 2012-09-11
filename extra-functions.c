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

int TWtry_update_binary(const char *path, ZipArchive *zip, int* wipe_cache) {
	const ZipEntry* binary_entry =
            mzFindZipEntry(zip, ASSUMED_UPDATE_BINARY_NAME);
    if (binary_entry == NULL) {
        mzCloseZipArchive(zip);
        return INSTALL_CORRUPT;
    }
    const char* binary = "/tmp/update_binary";
    unlink(binary);
    int fd = creat(binary, 0755);
    if (fd < 0) {
        mzCloseZipArchive(zip);
        LOGE("Can't make %s\n", binary);
        return INSTALL_ERROR;
    }
    bool ok = mzExtractZipEntryToFile(zip, binary_entry, fd);
    close(fd);
    mzCloseZipArchive(zip);

    if (!ok) {
        LOGE("Can't copy %s\n", ASSUMED_UPDATE_BINARY_NAME);
        return INSTALL_ERROR;
    }

    int pipefd[2];
    pipe(pipefd);

    // When executing the update binary contained in the package, the
    // arguments passed are:
    //
    //   - the version number for this interface
    //
    //   - an fd to which the program can write in order to update the
    //     progress bar.  The program can write single-line commands:
    //
    //        progress <frac> <secs>
    //            fill up the next <frac> part of of the progress bar
    //            over <secs> seconds.  If <secs> is zero, use
    //            set_progress commands to manually control the
    //            progress of this segment of the bar
    //
    //        set_progress <frac>
    //            <frac> should be between 0.0 and 1.0; sets the
    //            progress bar within the segment defined by the most
    //            recent progress command.
    //
    //        firmware <"hboot"|"radio"> <filename>
    //            arrange to install the contents of <filename> in the
    //            given partition on reboot.
    //
    //            (API v2: <filename> may start with "PACKAGE:" to
    //            indicate taking a file from the OTA package.)
    //
    //            (API v3: this command no longer exists.)
    //
    //        ui_print <string>
    //            display <string> on the screen.
    //
    //   - the name of the package zip file.
    //

    const char** args = (const char**)malloc(sizeof(char*) * 5);
    args[0] = binary;
    args[1] = EXPAND(RECOVERY_API_VERSION);   // defined in Android.mk
    char* temp = (char*)malloc(10);
    sprintf(temp, "%d", pipefd[1]);
    args[2] = temp;
    args[3] = (char*)path;
    args[4] = NULL;

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        execv(binary, (char* const*)args);
        fprintf(stdout, "E:Can't run %s (error)\n", binary);
        _exit(-1);
    }
    close(pipefd[1]);
    *wipe_cache = 0;

    char buffer[1024];
    FILE* from_child = fdopen(pipefd[0], "r");
	LOGI("8\n");
    while (fgets(buffer, sizeof(buffer), from_child) != NULL) {
        char* command = strtok(buffer, " \n");
        if (command == NULL) {
            continue;
        } else if (strcmp(command, "progress") == 0) {
            char* fraction_s = strtok(NULL, " \n");
            char* seconds_s = strtok(NULL, " \n");

            float fraction = strtof(fraction_s, NULL);
            int seconds = strtol(seconds_s, NULL, 10);

            //ui->ShowProgress(fraction * (1-VERIFICATION_PROGRESS_FRACTION), seconds);
        } else if (strcmp(command, "set_progress") == 0) {
            char* fraction_s = strtok(NULL, " \n");
            float fraction = strtof(fraction_s, NULL);
            //ui->SetProgress(fraction);
        } else if (strcmp(command, "ui_print") == 0) {
            char* str = strtok(NULL, "\n");
            if (str) {
                //ui->Print("%s", str);
            } else {
                //ui->Print("\n");
            }
        } else if (strcmp(command, "wipe_cache") == 0) {
            *wipe_cache = 1;
        } else if (strcmp(command, "clear_display") == 0) {
            //ui->SetBackground(RecoveryUI::NONE);
        } else {
            LOGE("unknown command [%s]\n", command);
        }
    }
    fclose(from_child);

    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        LOGE("Error in %s\n(Status %d)\n", path, WEXITSTATUS(status));
        return INSTALL_ERROR;
    }
    return INSTALL_SUCCESS;
}

// Look for an RSA signature embedded in the .ZIP file comment given
// the path to the zip.  Verify it matches one of the given public
// keys.
//
// Return VERIFY_SUCCESS, VERIFY_FAILURE (if any error is encountered
// or no key matches the signature).

int TWverify_file(const char* path, const RSAPublicKey *pKeys, unsigned int numKeys) {
    //ui->SetProgress(0.0);

    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        LOGE("failed to open %s (%s)\n", path, strerror(errno));
        return VERIFY_FAILURE;
    }

    // An archive with a whole-file signature will end in six bytes:
    //
    //   (2-byte signature start) $ff $ff (2-byte comment size)
    //
    // (As far as the ZIP format is concerned, these are part of the
    // archive comment.)  We start by reading this footer, this tells
    // us how far back from the end we have to start reading to find
    // the whole comment.

#define FOOTER_SIZE 6

    if (fseek(f, -FOOTER_SIZE, SEEK_END) != 0) {
        LOGE("failed to seek in %s (%s)\n", path, strerror(errno));
        fclose(f);
        return VERIFY_FAILURE;
    }

    unsigned char footer[FOOTER_SIZE];
    if (fread(footer, 1, FOOTER_SIZE, f) != FOOTER_SIZE) {
        LOGE("failed to read footer from %s (%s)\n", path, strerror(errno));
        fclose(f);
        return VERIFY_FAILURE;
    }

    if (footer[2] != 0xff || footer[3] != 0xff) {
        fclose(f);
        return VERIFY_FAILURE;
    }

    size_t comment_size = footer[4] + (footer[5] << 8);
    size_t signature_start = footer[0] + (footer[1] << 8);
    LOGI("comment is %d bytes; signature %d bytes from end\n",
         comment_size, signature_start);

    if (signature_start - FOOTER_SIZE < RSANUMBYTES) {
        // "signature" block isn't big enough to contain an RSA block.
        LOGE("signature is too short\n");
        fclose(f);
        return VERIFY_FAILURE;
    }

#define EOCD_HEADER_SIZE 22

    // The end-of-central-directory record is 22 bytes plus any
    // comment length.
    size_t eocd_size = comment_size + EOCD_HEADER_SIZE;

    if (fseek(f, -eocd_size, SEEK_END) != 0) {
        LOGE("failed to seek in %s (%s)\n", path, strerror(errno));
        fclose(f);
        return VERIFY_FAILURE;
    }

    // Determine how much of the file is covered by the signature.
    // This is everything except the signature data and length, which
    // includes all of the EOCD except for the comment length field (2
    // bytes) and the comment data.
    size_t signed_len = ftell(f) + EOCD_HEADER_SIZE - 2;

    unsigned char* eocd = (unsigned char*)malloc(eocd_size);
    if (eocd == NULL) {
        LOGE("malloc for EOCD record failed\n");
        fclose(f);
        return VERIFY_FAILURE;
    }
    if (fread(eocd, 1, eocd_size, f) != eocd_size) {
        LOGE("failed to read eocd from %s (%s)\n", path, strerror(errno));
        fclose(f);
        return VERIFY_FAILURE;
    }

    // If this is really is the EOCD record, it will begin with the
    // magic number $50 $4b $05 $06.
    if (eocd[0] != 0x50 || eocd[1] != 0x4b ||
        eocd[2] != 0x05 || eocd[3] != 0x06) {
        LOGE("signature length doesn't match EOCD marker\n");
        fclose(f);
        return VERIFY_FAILURE;
    }

    size_t i;
    for (i = 4; i < eocd_size-3; ++i) {
        if (eocd[i  ] == 0x50 && eocd[i+1] == 0x4b &&
            eocd[i+2] == 0x05 && eocd[i+3] == 0x06) {
            // if the sequence $50 $4b $05 $06 appears anywhere after
            // the real one, minzip will find the later (wrong) one,
            // which could be exploitable.  Fail verification if
            // this sequence occurs anywhere after the real one.
            LOGE("EOCD marker occurs after start of EOCD\n");
            fclose(f);
            return VERIFY_FAILURE;
        }
    }

#define BUFFER_SIZE 4096

    SHA_CTX ctx;
    SHA_init(&ctx);
    unsigned char* buffer = (unsigned char*)malloc(BUFFER_SIZE);
    if (buffer == NULL) {
        LOGE("failed to alloc memory for sha1 buffer\n");
        fclose(f);
        return VERIFY_FAILURE;
    }

    double frac = -1.0;
    size_t so_far = 0;
    fseek(f, 0, SEEK_SET);
    while (so_far < signed_len) {
        size_t size = BUFFER_SIZE;
        if (signed_len - so_far < size) size = signed_len - so_far;
        if (fread(buffer, 1, size, f) != size) {
            LOGE("failed to read data from %s (%s)\n", path, strerror(errno));
            fclose(f);
            return VERIFY_FAILURE;
        }
        SHA_update(&ctx, buffer, size);
        so_far += size;
        double f = so_far / (double)signed_len;
        if (f > frac + 0.02 || size == so_far) {
            //ui->SetProgress(f);
            frac = f;
        }
    }
    fclose(f);
    free(buffer);

    const uint8_t* sha1 = SHA_final(&ctx);
    for (i = 0; i < numKeys; ++i) {
        // The 6 bytes is the "(signature_start) $ff $ff (comment_size)" that
        // the signing tool appends after the signature itself.
        if (RSA_verify(pKeys+i, eocd + eocd_size - 6 - RSANUMBYTES,
                       RSANUMBYTES, sha1)) {
            LOGI("whole-file signature verified against key %d\n", i);
            free(eocd);
            return VERIFY_SUCCESS;
        }
    }
    free(eocd);
    LOGE("failed to verify whole-file signature\n");
    return VERIFY_FAILURE;
}

// Reads a file containing one or more public keys as produced by
// DumpPublicKey:  this is an RSAPublicKey struct as it would appear
// as a C source literal, eg:
//
//  "{64,0xc926ad21,{1795090719,...,-695002876},{-857949815,...,1175080310}}"
//
// (Note that the braces and commas in this example are actual
// characters the parser expects to find in the file; the ellipses
// indicate more numbers omitted from this example.)
//
// The file may contain multiple keys in this format, separated by
// commas.  The last key must not be followed by a comma.
//
// Returns NULL if the file failed to parse, or if it contain zero keys.
static RSAPublicKey*
TWload_keys(const char* filename, int* numKeys) {
    RSAPublicKey* out = NULL;
    *numKeys = 0;

    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        LOGE("opening %s: ERROR\n", filename);
        goto exit;
    }

    {
        int i;
        bool done = false;
        while (!done) {
            ++*numKeys;
            out = (RSAPublicKey*)realloc(out, *numKeys * sizeof(RSAPublicKey));
            RSAPublicKey* key = out + (*numKeys - 1);
            if (fscanf(f, " { %i , 0x%x , { %u",
                       &(key->len), &(key->n0inv), &(key->n[0])) != 3) {
                goto exit;
            }
            if (key->len != RSANUMWORDS) {
                LOGE("key length (%d) does not match expected size\n", key->len);
                goto exit;
            }
            for (i = 1; i < key->len; ++i) {
                if (fscanf(f, " , %u", &(key->n[i])) != 1) goto exit;
            }
            if (fscanf(f, " } , { %u", &(key->rr[0])) != 1) goto exit;
            for (i = 1; i < key->len; ++i) {
                if (fscanf(f, " , %u", &(key->rr[i])) != 1) goto exit;
            }
            fscanf(f, " } } ");

            // if the line ends in a comma, this file has more keys.
            switch (fgetc(f)) {
            case ',':
                // more keys to come.
                break;

            case EOF:
                done = true;
                break;

            default:
                LOGE("unexpected character between keys\n");
                goto exit;
            }
        }
    }

    fclose(f);
    return out;

exit:
    if (f) fclose(f);
    free(out);
    *numKeys = 0;
    return NULL;
}

int TWinstall_zip(const char* path, int* wipe_cache) {
	int err;

	if (DataManager_GetIntValue(TW_SIGNED_ZIP_VERIFY_VAR)) {
		int numKeys;
		RSAPublicKey* loadedKeys = TWload_keys(PUBLIC_KEYS_FILE, &numKeys);
		if (loadedKeys == NULL) {
			LOGE("Failed to load keys\n");
			return -1;
		}
		LOGI("%d key(s) loaded from %s\n", numKeys, PUBLIC_KEYS_FILE);

		// Give verification half the progress bar...
		//ui->Print("Verifying update package...\n");
		//ui->SetProgressType(RecoveryUI::DETERMINATE);
		//ui->ShowProgress(VERIFICATION_PROGRESS_FRACTION, VERIFICATION_PROGRESS_TIME);

		err = TWverify_file(path, loadedKeys, numKeys);
		free(loadedKeys);
		LOGI("verify_file returned %d\n", err);
		if (err != VERIFY_SUCCESS) {
			LOGE("signature verification failed\n");
			return -1;
		}
	}
	/* Try to open the package.
     */
    ZipArchive zip;
    err = mzOpenZipArchive(path, &zip);
    if (err != 0) {
        LOGE("Can't open %s\n(%s)\n", path, err != -1 ? strerror(err) : "bad");
        return INSTALL_CORRUPT;
    }

    /* Verify and install the contents of the package.
     */
    //ui->Print("Installing update...\n");
    return TWtry_update_binary(path, &zip, wipe_cache);
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

