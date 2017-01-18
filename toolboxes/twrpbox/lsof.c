/*
 * Copyright (c) 2010, The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Google, Inc. nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pwd.h>
#include <sys/stat.h>

#define BUF_MAX 1024
#define CMD_DISPLAY_MAX (9 + 1)
#define USER_DISPLAY_MAX (10 + 1)

struct pid_info_t {
    pid_t pid;
    char user[USER_DISPLAY_MAX];

    char cmdline[CMD_DISPLAY_MAX];

    char path[PATH_MAX];
    ssize_t parent_length;
};

static void print_header()
{
    printf("%-9s %5s %10s %4s   %9s %18s %9s %10s %s\n",
            "COMMAND",
            "PID",
            "USER",
            "FD",
            "TYPE",
            "DEVICE",
            "SIZE/OFF",
            "NODE",
            "NAME");
}

static void print_symlink(const char* name, const char* path, struct pid_info_t* info)
{
    static ssize_t link_dest_size;
    static char link_dest[PATH_MAX];

    strlcat(info->path, path, sizeof(info->path));
    if ((link_dest_size = readlink(info->path, link_dest, sizeof(link_dest)-1)) < 0) {
        if (errno == ENOENT)
            goto out;

        snprintf(link_dest, sizeof(link_dest), "%s (readlink: %s)", info->path, strerror(errno));
    } else {
        link_dest[link_dest_size] = '\0';
    }

    // Things that are just the root filesystem are uninteresting (we already know)
    if (!strcmp(link_dest, "/"))
        goto out;

    const char* fd = name;
    char rw = ' ';
    char locks = ' '; // TODO: read /proc/locks

    const char* type = "unknown";
    char device[32] = "?";
    char size_off[32] = "?";
    char node[32] = "?";

    struct stat sb;
    if (lstat(link_dest, &sb) != -1) {
        switch ((sb.st_mode & S_IFMT)) {
          case S_IFSOCK: type = "sock"; break; // TODO: what domain?
          case S_IFLNK: type = "LINK"; break;
          case S_IFREG: type = "REG"; break;
          case S_IFBLK: type = "BLK"; break;
          case S_IFDIR: type = "DIR"; break;
          case S_IFCHR: type = "CHR"; break;
          case S_IFIFO: type = "FIFO"; break;
        }
        snprintf(device, sizeof(device), "%d,%d", (int) sb.st_dev, (int) sb.st_rdev);
        snprintf(node, sizeof(node), "%d", (int) sb.st_ino);
        snprintf(size_off, sizeof(size_off), "%d", (int) sb.st_size);
    }

    if (!name) {
        // We're looking at an fd, so read its flags.
        fd = path;
        char fdinfo_path[PATH_MAX];
        snprintf(fdinfo_path, sizeof(fdinfo_path), "/proc/%d/fdinfo/%s", info->pid, path);
        FILE* fp = fopen(fdinfo_path, "r");
        if (fp != NULL) {
            int pos;
            unsigned flags;

            if (fscanf(fp, "pos: %d flags: %o", &pos, &flags) == 2) {
                flags &= O_ACCMODE;
                if (flags == O_RDONLY) rw = 'r';
                else if (flags == O_WRONLY) rw = 'w';
                else rw = 'u';
            }
            fclose(fp);
        }
    }

    printf("%-9s %5d %10s %4s%c%c %9s %18s %9s %10s %s\n",
            info->cmdline, info->pid, info->user, fd, rw, locks, type, device, size_off, node, link_dest);

out:
    info->path[info->parent_length] = '\0';
}

// Prints out all file that have been memory mapped
static void print_maps(struct pid_info_t* info)
{
    FILE *maps;
    size_t offset;
    char device[10];
    long int inode;
    char file[PATH_MAX];

    strlcat(info->path, "maps", sizeof(info->path));

    maps = fopen(info->path, "r");
    if (!maps)
        goto out;

    while (fscanf(maps, "%*x-%*x %*s %zx %s %ld %s\n", &offset, device, &inode, file) == 4) {
        // We don't care about non-file maps
        if (inode == 0 || !strcmp(device, "00:00"))
            continue;

        printf("%-9s %5d %10s %4s   %9s %18s %9zd %10ld %s\n",
                info->cmdline, info->pid, info->user, "mem",
                "REG", device, offset, inode, file);
    }

    fclose(maps);

out:
    info->path[info->parent_length] = '\0';
}

// Prints out all open file descriptors
static void print_fds(struct pid_info_t* info)
{
    static char* fd_path = "fd/";
    strlcat(info->path, fd_path, sizeof(info->path));

    int previous_length = info->parent_length;
    info->parent_length += strlen(fd_path);

    DIR *dir = opendir(info->path);
    if (dir == NULL) {
        char msg[BUF_MAX];
        snprintf(msg, sizeof(msg), "%s (opendir: %s)", info->path, strerror(errno));
        printf("%-9s %5d %10s %4s   %9s %18s %9s %10s %s\n",
                info->cmdline, info->pid, info->user, "FDS",
                "", "", "", "", msg);
        goto out;
    }

    struct dirent* de;
    while ((de = readdir(dir))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;

        print_symlink(NULL, de->d_name, info);
    }
    closedir(dir);

out:
    info->parent_length = previous_length;
    info->path[info->parent_length] = '\0';
}

static void lsof_dumpinfo(pid_t pid)
{
    int fd;
    struct pid_info_t info;
    struct stat pidstat;
    struct passwd *pw;

    info.pid = pid;
    snprintf(info.path, sizeof(info.path), "/proc/%d/", pid);
    info.parent_length = strlen(info.path);

    // Get the UID by calling stat on the proc/pid directory.
    if (!stat(info.path, &pidstat)) {
        pw = getpwuid(pidstat.st_uid);
        if (pw) {
            strlcpy(info.user, pw->pw_name, sizeof(info.user));
        } else {
            snprintf(info.user, USER_DISPLAY_MAX, "%d", (int)pidstat.st_uid);
        }
    } else {
        strcpy(info.user, "???");
    }

    // Read the command line information; each argument is terminated with NULL.
    strlcat(info.path, "cmdline", sizeof(info.path));
    fd = open(info.path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Couldn't read %s\n", info.path);
        return;
    }

    char cmdline[PATH_MAX];
    int numRead = read(fd, cmdline, sizeof(cmdline) - 1);
    close(fd);

    if (numRead < 0) {
        fprintf(stderr, "Error reading cmdline: %s: %s\n", info.path, strerror(errno));
        return;
    }

    cmdline[numRead] = '\0';

    // We only want the basename of the cmdline
    strlcpy(info.cmdline, basename(cmdline), sizeof(info.cmdline));

    // Read each of these symlinks
    print_symlink("cwd", "cwd", &info);
    print_symlink("txt", "exe", &info);
    print_symlink("rtd", "root", &info);
    print_fds(&info);
    print_maps(&info);
}

int lsof_main(int argc, char *argv[])
{
    long int pid = 0;
    char* endptr;
    if (argc == 2) {
        pid = strtol(argv[1], &endptr, 10);
    }

    print_header();

    if (pid) {
        lsof_dumpinfo(pid);
    } else {
        DIR *dir = opendir("/proc");
        if (dir == NULL) {
            fprintf(stderr, "Couldn't open /proc\n");
            return -1;
        }

        struct dirent* de;
        while ((de = readdir(dir))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                continue;

            // Only inspect directories that are PID numbers
            pid = strtol(de->d_name, &endptr, 10);
            if (*endptr != '\0')
                continue;

            lsof_dumpinfo(pid);
        }
        closedir(dir);
    }

    return 0;
}
