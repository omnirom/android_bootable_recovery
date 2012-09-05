#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/limits.h>
#include <dirent.h>
#include <sys/stat.h>

#include <signal.h>
#include <sys/wait.h>
#include "../extra-functions.h"

//extern int __system(const char *command);
#define BML_UNLOCK_ALL				0x8A29		///< unlock all partition RO -> RW

#ifndef BOARD_BML_BOOT
#define BOARD_BML_BOOT              "/dev/block/bml7"
#endif

#ifndef BOARD_BML_RECOVERY
#define BOARD_BML_RECOVERY          "/dev/block/bml8"
#endif

#undef _PATH_BSHELL
#define _PATH_BSHELL "/sbin/sh"

int __system(const char *command)
{
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


static int restore_internal(const char* bml, const char* filename)
{
    char buf[4096];
    int dstfd, srcfd, bytes_read, bytes_written, total_read = 0;
    if (filename == NULL)
        srcfd = 0;
    else {
        srcfd = open(filename, O_RDONLY | O_LARGEFILE);
        if (srcfd < 0)
            return 2;
    }
    dstfd = open(bml, O_RDWR | O_LARGEFILE);
    if (dstfd < 0)
        return 3;
    if (ioctl(dstfd, BML_UNLOCK_ALL, 0))
        return 4;
    do {
        total_read += bytes_read = read(srcfd, buf, 4096);
        if (!bytes_read)
            break;
        if (bytes_read < 4096)
            memset(&buf[bytes_read], 0, 4096 - bytes_read);
        if (write(dstfd, buf, 4096) < 4096)
            return 5;
    } while(bytes_read == 4096);
    
    close(dstfd);
    close(srcfd);
    
    return 0;
}

int cmd_bml_restore_raw_partition(const char *partition, const char *filename)
{
    if (strcmp(partition, "boot") != 0 && strcmp(partition, "recovery") != 0 && strcmp(partition, "recoveryonly") != 0 && partition[0] != '/')
        return 6;

    int ret = -1;
    if (strcmp(partition, "recoveryonly") != 0) {
        // always restore boot, regardless of whether recovery or boot is flashed.
        // this is because boot and recovery are the same on some samsung phones.
        // unless of course, recoveryonly is explictly chosen (bml8)
        ret = restore_internal(BOARD_BML_BOOT, filename);
        if (ret != 0)
            return ret;
    }

    if (strcmp(partition, "recovery") == 0 || strcmp(partition, "recoveryonly") == 0)
        ret = restore_internal(BOARD_BML_RECOVERY, filename);

    // support explicitly provided device paths
    if (partition[0] == '/')
        ret = restore_internal(partition, filename);
    return ret;
}

int cmd_bml_backup_raw_partition(const char *partition, const char *out_file)
{
    char* bml;
    if (strcmp("boot", partition) == 0)
        bml = BOARD_BML_BOOT;
    else if (strcmp("recovery", partition) == 0)
        bml = BOARD_BML_RECOVERY;
    else if (partition[0] == '/') {
        // support explicitly provided device paths
        bml = partition;
    }
    else {
        printf("Invalid partition.\n");
        return -1;
    }

    int ch;
    FILE *in;
    FILE *out;
    int val = 0;
    char buf[512];
    unsigned sz = 0;
    unsigned i;
    int ret = -1;
    char *in_file = bml;

    in  = fopen ( in_file,  "r" );
    if (in == NULL)
        goto ERROR3;

    out = fopen ( out_file,  "w" );
    if (out == NULL)
        goto ERROR2;

    fseek(in, 0L, SEEK_END);
    sz = ftell(in);
    fseek(in, 0L, SEEK_SET);

    if (sz % 512)
    {
        while ( ( ch = fgetc ( in ) ) != EOF )
            fputc ( ch, out );
    }
    else
    {
        for (i=0; i< (sz/512); i++)
        {
            if ((fread(buf, 512, 1, in)) != 1)
                goto ERROR1;
            if ((fwrite(buf, 512, 1, out)) != 1)
                goto ERROR1;
        }
    }

    fsync(out);
    ret = 0;
ERROR1:
    fclose ( out );
ERROR2:
    fclose ( in );
ERROR3:
    return ret;
}

int cmd_bml_erase_raw_partition(const char *partition)
{
    // TODO: implement raw wipe
    return 0;
}

int cmd_bml_erase_partition(const char *partition, const char *filesystem)
{
    return -1;
}

int cmd_bml_mount_partition(const char *partition, const char *mount_point, const char *filesystem, int read_only)
{
    return -1;
}

int cmd_bml_get_partition_device(const char *partition, char *device)
{
    return -1;
}

int format_rfs_device (const char *device, const char *path) {
    const char *fatsize = "32";
    const char *sectorsize = "1";

    if (strcmp(path, "/datadata") == 0 || strcmp(path, "/cache") == 0) {
        fatsize = "16";
    }

    // Just in case /data sector size needs to be altered
    else if (strcmp(path, "/data") == 0 ) {
        sectorsize = "1";
    } 

    // dump 10KB of zeros to partition before format due to fat.format bug
    char cmd[PATH_MAX];

    sprintf(cmd, "/sbin/dd if=/dev/zero of=%s bs=4096 count=10", device);
    if(__system(cmd)) {
        printf("failure while zeroing rfs partition.\n");
        return -1;
    }

    // Run fat.format
    sprintf(cmd, "/sbin/fat.format -F %s -S 4096 -s %s %s", fatsize, sectorsize, device);
    if(__system(cmd)) {
        printf("failure while running fat.format\n");
        return -1;
    }

    return 0;
}
