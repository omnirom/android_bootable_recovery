/*
 * Copyright (C) 2012 The Android Open Source Project
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

/* TO DO:
 *   1. Re-direct fsck output to the kernel log?
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <libgen.h>
#include <time.h>

#include <private/android_filesystem_config.h>
#include <cutils/partition_utils.h>
#include <cutils/properties.h>

#include "fs_mgr_priv.h"

#define KEY_LOC_PROP   "ro.crypto.keyfile.userdata"
#define KEY_IN_FOOTER  "footer"

#define E2FSCK_BIN      "/system/bin/e2fsck"

struct flag_list {
    const char *name;
    unsigned flag;
};

static struct flag_list mount_flags[] = {
    { "noatime",    MS_NOATIME },
    { "noexec",     MS_NOEXEC },
    { "nosuid",     MS_NOSUID },
    { "nodev",      MS_NODEV },
    { "nodiratime", MS_NODIRATIME },
    { "ro",         MS_RDONLY },
    { "rw",         0 },
    { "remount",    MS_REMOUNT },
    { "defaults",   0 },
    { 0,            0 },
};

static struct flag_list fs_mgr_flags[] = {
    { "wait",        MF_WAIT },
    { "check",       MF_CHECK },
    { "encryptable=",MF_CRYPT },
    { "defaults",    0 },
    { 0,             0 },
};

/*
 * gettime() - returns the time in seconds of the system's monotonic clock or
 * zero on error.
 */
static time_t gettime(void)
{
    struct timespec ts;
    int ret;

    ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (ret < 0) {
        ERROR("clock_gettime(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        return 0;
    }

    return ts.tv_sec;
}

static int wait_for_file(const char *filename, int timeout)
{
    struct stat info;
    time_t timeout_time = gettime() + timeout;
    int ret = -1;

    while (gettime() < timeout_time && ((ret = stat(filename, &info)) < 0))
        usleep(10000);

    return ret;
}

static int parse_flags(char *flags, struct flag_list *fl, char **key_loc,
                       char *fs_options, int fs_options_len)
{
    int f = 0;
    int i;
    char *p;
    char *savep;

    /* initialize key_loc to null, if we find an MF_CRYPT flag,
     * then we'll set key_loc to the proper value */
    if (key_loc) {
        *key_loc = NULL;
    }
    /* initialize fs_options to the null string */
    if (fs_options && (fs_options_len > 0)) {
        fs_options[0] = '\0';
    }

    p = strtok_r(flags, ",", &savep);
    while (p) {
        /* Look for the flag "p" in the flag list "fl"
         * If not found, the loop exits with fl[i].name being null.
         */
        for (i = 0; fl[i].name; i++) {
            if (!strncmp(p, fl[i].name, strlen(fl[i].name))) {
                f |= fl[i].flag;
                if ((fl[i].flag == MF_CRYPT) && key_loc) {
                    /* The encryptable flag is followed by an = and the
                     * location of the keys.  Get it and return it.
                     */
                    *key_loc = strdup(strchr(p, '=') + 1);
                }
                break;
            }
        }

        if (!fl[i].name) {
            if (fs_options) {
                /* It's not a known flag, so it must be a filesystem specific
                 * option.  Add it to fs_options if it was passed in.
                 */
                strlcat(fs_options, p, fs_options_len);
                strlcat(fs_options, ",", fs_options_len);
            } else {
                /* fs_options was not passed in, so if the flag is unknown
                 * it's an error.
                 */
                ERROR("Warning: unknown flag %s\n", p);
            }
        }
        p = strtok_r(NULL, ",", &savep);
    }

out:
    if (fs_options && fs_options[0]) {
        /* remove the last trailing comma from the list of options */
        fs_options[strlen(fs_options) - 1] = '\0';
    }

    return f;
}

/* Read a line of text till the next newline character.
 * If no newline is found before the buffer is full, continue reading till a new line is seen,
 * then return an empty buffer.  This effectively ignores lines that are too long.
 * On EOF, return null.
 */
static char *fs_mgr_getline(char *buf, int size, FILE *file)
{
    int cnt = 0;
    int eof = 0;
    int eol = 0;
    int c;

    if (size < 1) {
        return NULL;
    }

    while (cnt < (size - 1)) {
        c = getc(file);
        if (c == EOF) {
            eof = 1;
            break;
        }

        *(buf + cnt) = c;
        cnt++;

        if (c == '\n') {
            eol = 1;
            break;
        }
    }

    /* Null terminate what we've read */
    *(buf + cnt) = '\0';

    if (eof) {
        if (cnt) {
            return buf;
        } else {
            return NULL;
        }
    } else if (eol) {
        return buf;
    } else {
        /* The line is too long.  Read till a newline or EOF.
         * If EOF, return null, if newline, return an empty buffer.
         */
        while(1) {
            c = getc(file);
            if (c == EOF) {
                return NULL;
            } else if (c == '\n') {
                *buf = '\0';
                return buf;
            }
        }
    }
}

static struct fstab_rec *read_fstab(char *fstab_path)
{
    FILE *fstab_file;
    int cnt, entries;
    int len;
    char line[256];
    const char *delim = " \t";
    char *save_ptr, *p;
    struct fstab_rec *fstab;
    char *key_loc;
#define FS_OPTIONS_LEN 1024
    char tmp_fs_options[FS_OPTIONS_LEN];

    fstab_file = fopen(fstab_path, "r");
    if (!fstab_file) {
        ERROR("Cannot open file %s\n", fstab_path);
        return 0;
    }

    entries = 0;
    while (fs_mgr_getline(line, sizeof(line), fstab_file)) {
        /* if the last character is a newline, shorten the string by 1 byte */
        len = strlen(line);
        if (line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        /* Skip any leading whitespace */
        p = line;
        while (isspace(*p)) {
            p++;
        }
        /* ignore comments or empty lines */
        if (*p == '#' || *p == '\0')
            continue;
        entries++;
    }

    if (!entries) {
        ERROR("No entries found in fstab\n");
        return 0;
    }

    fstab = calloc(entries + 1, sizeof(struct fstab_rec));

    fseek(fstab_file, 0, SEEK_SET);

    cnt = 0;
    while (fs_mgr_getline(line, sizeof(line), fstab_file)) {
        /* if the last character is a newline, shorten the string by 1 byte */
        len = strlen(line);
        if (line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        /* Skip any leading whitespace */
        p = line;
        while (isspace(*p)) {
            p++;
        }
        /* ignore comments or empty lines */
        if (*p == '#' || *p == '\0')
            continue;

        /* If a non-comment entry is greater than the size we allocated, give an
         * error and quit.  This can happen in the unlikely case the file changes
         * between the two reads.
         */
        if (cnt >= entries) {
            ERROR("Tried to process more entries than counted\n");
            break;
        }

        if (!(p = strtok_r(line, delim, &save_ptr))) {
            ERROR("Error parsing mount source\n");
            return 0;
        }
        fstab[cnt].blk_dev = strdup(p);

        if (!(p = strtok_r(NULL, delim, &save_ptr))) {
            ERROR("Error parsing mnt_point\n");
            return 0;
        }
        fstab[cnt].mnt_point = strdup(p);

        if (!(p = strtok_r(NULL, delim, &save_ptr))) {
            ERROR("Error parsing fs_type\n");
            return 0;
        }
        fstab[cnt].type = strdup(p);

        if (!(p = strtok_r(NULL, delim, &save_ptr))) {
            ERROR("Error parsing mount_flags\n");
            return 0;
        }
        tmp_fs_options[0] = '\0';
        fstab[cnt].flags = parse_flags(p, mount_flags, 0, tmp_fs_options, FS_OPTIONS_LEN);

        /* fs_options are optional */
        if (tmp_fs_options[0]) {
            fstab[cnt].fs_options = strdup(tmp_fs_options);
        } else {
            fstab[cnt].fs_options = NULL;
        }

        if (!(p = strtok_r(NULL, delim, &save_ptr))) {
            ERROR("Error parsing fs_mgr_options\n");
            return 0;
        }
        fstab[cnt].fs_mgr_flags = parse_flags(p, fs_mgr_flags, &key_loc, 0, 0);
        fstab[cnt].key_loc = key_loc;

        cnt++;
    }
    fclose(fstab_file);

    return fstab;
}

static void free_fstab(struct fstab_rec *fstab)
{
    int i = 0;

    while (fstab[i].blk_dev) {
        /* Free the pointers return by strdup(3) */
        free(fstab[i].blk_dev);
        free(fstab[i].mnt_point);
        free(fstab[i].type);
        free(fstab[i].fs_options);
        free(fstab[i].key_loc);

        i++;
    }

    /* Free the actual fstab array created by calloc(3) */
    free(fstab);
}

static void check_fs(char *blk_dev, char *type)
{
    pid_t pid;
    int status;

    /* Check for the types of filesystems we know how to check */
    if (!strcmp(type, "ext2") || !strcmp(type, "ext3") || !strcmp(type, "ext4")) {
        INFO("Running %s on %s\n", E2FSCK_BIN, blk_dev);
        pid = fork();
        if (pid > 0) {
            /* Parent, wait for the child to return */
            waitpid(pid, &status, 0);
        } else if (pid == 0) {
            /* child, run checker */
            execlp(E2FSCK_BIN, E2FSCK_BIN, "-y", blk_dev, (char *)NULL);

            /* Only gets here on error */
            ERROR("Cannot run fs_mgr binary %s\n", E2FSCK_BIN);
        } else {
            /* No need to check for error in fork, we can't really handle it now */
            ERROR("Fork failed trying to run %s\n", E2FSCK_BIN);
        }
    }

    return;
}

static void remove_trailing_slashes(char *n)
{
    int len;

    len = strlen(n) - 1;
    while ((*(n + len) == '/') && len) {
      *(n + len) = '\0';
      len--;
    }
}

static int fs_match(char *in1, char *in2)
{
    char *n1;
    char *n2;
    int ret;

    n1 = strdup(in1);
    n2 = strdup(in2);

    remove_trailing_slashes(n1);
    remove_trailing_slashes(n2);

    ret = !strcmp(n1, n2);

    free(n1);
    free(n2);

    return ret;
}

int fs_mgr_mount_all(char *fstab_file)
{
    int i = 0;
    int encrypted = 0;
    int ret = -1;
    int mret;
    struct fstab_rec *fstab = 0;

    if (!(fstab = read_fstab(fstab_file))) {
        return ret;
    }

    for (i = 0; fstab[i].blk_dev; i++) {
        if (fstab[i].fs_mgr_flags & MF_WAIT) {
            wait_for_file(fstab[i].blk_dev, WAIT_TIMEOUT);
        }

        if (fstab[i].fs_mgr_flags & MF_CHECK) {
            check_fs(fstab[i].blk_dev, fstab[i].type);
        }

        mret = mount(fstab[i].blk_dev, fstab[i].mnt_point, fstab[i].type,
                     fstab[i].flags, fstab[i].fs_options);
        if (!mret) {
            /* Success!  Go get the next one */
            continue;
        }

        /* mount(2) returned an error, check if it's encrypted and deal with it */
        if ((fstab[i].fs_mgr_flags & MF_CRYPT) && !partition_wiped(fstab[i].blk_dev)) {
            /* Need to mount a tmpfs at this mountpoint for now, and set
             * properties that vold will query later for decrypting
             */
            if (mount("tmpfs", fstab[i].mnt_point, "tmpfs",
                  MS_NOATIME | MS_NOSUID | MS_NODEV, CRYPTO_TMPFS_OPTIONS) < 0) {
                ERROR("Cannot mount tmpfs filesystem for encrypted fs at %s\n",
                        fstab[i].mnt_point);
                goto out;
            }
            encrypted = 1;
        } else {
            ERROR("Cannot mount filesystem on %s at %s\n",
                    fstab[i].blk_dev, fstab[i].mnt_point);
            goto out;
        }
    }

    if (encrypted) {
        ret = 1;
    } else {
        ret = 0;
    }

out:
    free_fstab(fstab);
    return ret;
}

/* If tmp_mnt_point is non-null, mount the filesystem there.  This is for the
 * tmp mount we do to check the user password
 */
int fs_mgr_do_mount(char *fstab_file, char *n_name, char *n_blk_dev, char *tmp_mnt_point)
{
    int i = 0;
    int ret = -1;
    struct fstab_rec *fstab = 0;
    char *m;

    if (!(fstab = read_fstab(fstab_file))) {
        return ret;
    }

    for (i = 0; fstab[i].blk_dev; i++) {
        if (!fs_match(fstab[i].mnt_point, n_name)) {
            continue;
        }

        /* We found our match */
        /* First check the filesystem if requested */
        if (fstab[i].fs_mgr_flags & MF_WAIT) {
            wait_for_file(fstab[i].blk_dev, WAIT_TIMEOUT);
        }

        if ((fstab[i].fs_mgr_flags & MF_CHECK) && strcmp("ext4", fstab[i].type) != 0) {
            check_fs(fstab[i].blk_dev, fstab[i].type);
        }

        /* Now mount it where requested */
        if (tmp_mnt_point) {
            m = tmp_mnt_point;
        } else {
            m = fstab[i].mnt_point;
        }
        if (mount(n_blk_dev, m, fstab[i].type,
                  fstab[i].flags, fstab[i].fs_options)) {
            ERROR("Cannot mount filesystem on %s at %s\n",
                    n_blk_dev, m);
            goto out;
        } else {
            ret = 0;
            goto out;
        }
    }

    /* We didn't find a match, say so and return an error */
    ERROR("Cannot find mount point %s in fstab\n", fstab[i].mnt_point);

out:
    free_fstab(fstab);
    return ret;
}

/*
 * mount a tmpfs filesystem at the given point.
 * return 0 on success, non-zero on failure.
 */
int fs_mgr_do_tmpfs_mount(char *n_name)
{
    int ret;

    ret = mount("tmpfs", n_name, "tmpfs",
                MS_NOATIME | MS_NOSUID | MS_NODEV, CRYPTO_TMPFS_OPTIONS);
    if (ret < 0) {
        ERROR("Cannot mount tmpfs filesystem at %s\n", n_name);
        return -1;
    }

    /* Success */
    return 0;
}

int fs_mgr_unmount_all(char *fstab_file)
{
    int i = 0;
    int ret = 0;
    struct fstab_rec *fstab = 0;

    if (!(fstab = read_fstab(fstab_file))) {
        return -1;
    }

    while (fstab[i].blk_dev) {
        if (umount(fstab[i].mnt_point)) {
            ERROR("Cannot unmount filesystem at %s\n", fstab[i].mnt_point);
            ret = -1;
        }
        i++;
    }

    free_fstab(fstab);
    return ret;
}
/*
 * key_loc must be at least PROPERTY_VALUE_MAX bytes long
 *
 * real_blk_dev must be at least PROPERTY_VALUE_MAX bytes long
 */
int fs_mgr_get_crypt_info(char *fstab_file, char *key_loc, char *real_blk_dev, int size)
{
    int i = 0;
    struct fstab_rec *fstab = 0;

    if (!(fstab = read_fstab(fstab_file))) {
        return -1;
    }
    /* Initialize return values to null strings */
    if (key_loc) {
        *key_loc = '\0';
    }
    if (real_blk_dev) {
        *real_blk_dev = '\0';
    }

    /* Look for the encryptable partition to find the data */
    for (i = 0; fstab[i].blk_dev; i++) {
        if (!(fstab[i].fs_mgr_flags & MF_CRYPT)) {
            continue;
        }

        /* We found a match */
        if (key_loc) {
            strlcpy(key_loc, fstab[i].key_loc, size);
        }
        if (real_blk_dev) {
            strlcpy(real_blk_dev, fstab[i].blk_dev, size);
        }
        break;
    }

    free_fstab(fstab);
    return 0;
}

