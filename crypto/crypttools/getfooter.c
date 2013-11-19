#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/dm-ioctl.h>
#include <sys/mount.h>
#include "../fs_mgr/include/fs_mgr.h"
#include "cryptfs.h"

#include "cutils/properties.h"

#ifndef PROPERTY_VALUE_MAX
#define PROPERTY_VALUE_MAX 255
#endif
#ifndef FSTAB_PREFIX
#define FSTAB_PREFIX "/fstab."
#endif
#ifndef KEY_IN_FOOTER
#define KEY_IN_FOOTER "footer"
#endif

struct fstab *fstab;

static unsigned int get_blkdev_size(int fd)
{
  unsigned int nr_sec;

  if ( (ioctl(fd, BLKGETSIZE, &nr_sec)) == -1) {
    nr_sec = 0;
  }

  return nr_sec;
}

int get_crypt_ftr_info(char **metadata_fname, off64_t *off)
{
  static int cached_data = 0;
  static off64_t cached_off = 0;
  static char cached_metadata_fname[PROPERTY_VALUE_MAX] = "";
  int fd;
  char key_loc[PROPERTY_VALUE_MAX];
  char real_blkdev[PROPERTY_VALUE_MAX];
  unsigned int nr_sec;
  int rc = -1;

    fs_mgr_get_crypt_info(fstab, key_loc, real_blkdev, sizeof(key_loc));

    if (!strcmp(key_loc, KEY_IN_FOOTER)) {
      if ( (fd = open(real_blkdev, O_RDWR)) < 0) {
        printf("Cannot open real block device %s\n", real_blkdev);
        return -1;
      }

      if ((nr_sec = get_blkdev_size(fd))) {
        /* If it's an encrypted Android partition, the last 16 Kbytes contain the
         * encryption info footer and key, and plenty of bytes to spare for future
         * growth.
         */
        strlcpy(cached_metadata_fname, real_blkdev, sizeof(cached_metadata_fname));
        cached_off = ((off64_t)nr_sec * 512) - CRYPT_FOOTER_OFFSET;
        cached_data = 1;
      } else {
        printf("Cannot get size of block device %s\n", real_blkdev);
      }
      close(fd);
    } else {
      strlcpy(cached_metadata_fname, key_loc, sizeof(cached_metadata_fname));
      cached_off = 0;
      cached_data = 1;
    }

  if (cached_data) {
    if (metadata_fname) {
        *metadata_fname = cached_metadata_fname;
    }
    if (off) {
        *off = cached_off;
    }
    rc = 0;
  }

  return rc;
}

int get_crypt_ftr_and_key(struct crypt_mnt_ftr *crypt_ftr)
{
  int fd;
  unsigned int nr_sec, cnt;
  off64_t starting_off;
  int rc = -1;
  char *fname = NULL;
  struct stat statbuf;

  if (get_crypt_ftr_info(&fname, &starting_off)) {
    printf("Unable to get crypt_ftr_info\n");
    return -1;
  }
  if (fname[0] != '/') {
    printf("Unexpected value for crypto key location '%s'\n", fname);
    //return -1;
  }
  if ( (fd = open(fname, O_RDWR)) < 0) {
    printf("Cannot open footer file %s for get\n", fname);
    return -1;
  }

  /* Make sure it's 16 Kbytes in length */
  fstat(fd, &statbuf);
  if (S_ISREG(statbuf.st_mode) && (statbuf.st_size != 0x4000)) {
    printf("footer file %s is not the expected size!\n", fname);
	close(fd);
    return -1;
  }

  /* Seek to the start of the crypt footer */
  if (lseek64(fd, starting_off, SEEK_SET) == -1) {
    printf("Cannot seek to real block device footer\n");
	close(fd);
    return -1;
  }

  if ( (cnt = read(fd, crypt_ftr, sizeof(struct crypt_mnt_ftr))) != sizeof(struct crypt_mnt_ftr)) {
    printf("Cannot read real block device footer\n");
	close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

int main(void)
{
	char key_loc[PROPERTY_VALUE_MAX];
	char blk_dev[PROPERTY_VALUE_MAX];
	char fstab_filename[PROPERTY_VALUE_MAX + sizeof(FSTAB_PREFIX)];
	struct stat st;
	struct crypt_mnt_ftr crypt_ftr;
	int fdout;

	printf("This tool comes with no warranties whatsoever.\n");
	printf("http://teamw.in\n\n");
	strcpy(fstab_filename, FSTAB_PREFIX);
	property_get("ro.hardware", fstab_filename + sizeof(FSTAB_PREFIX) - 1, "");

	if (stat(fstab_filename, &st) != 0) {
		printf("Cannot locate fstab file '%s'\n", fstab_filename);
		return -1;
	}

	fstab = fs_mgr_read_fstab(fstab_filename);
	if (!fstab) {
		printf("failed to open %s\n", fstab_filename);
		return -1;
	}

	fs_mgr_get_crypt_info(fstab, key_loc, blk_dev, sizeof(blk_dev));

	if (get_crypt_ftr_and_key(&crypt_ftr)) {
		printf("Error getting crypt footer and key\n");
		return -1;
	}

	if ( (fdout = open("/footerfile", O_WRONLY | O_CREAT, 0644)) < 0) {
		printf("Cannot open output file /footerfile\n");
		return -1;
	}
	if (write(fdout, (void*) &crypt_ftr, sizeof(struct crypt_mnt_ftr)) != sizeof(struct crypt_mnt_ftr)) {
		printf("Failed to write footer.\n");
	}
	close(fdout);

	if (!strcmp(key_loc, KEY_IN_FOOTER)) {
		unsigned int nr_sec, cnt;
		off64_t off = 0;
		char buffer[CRYPT_FOOTER_OFFSET];
		int fd;

		printf("\n\nDumping footer from '%s'...\n", blk_dev);
		if ( (fd = open(blk_dev, O_RDONLY)) < 0) {
			printf("Cannot open real block device %s\n", blk_dev);
			return -1;
		}

		if ((nr_sec = get_blkdev_size(fd))) {
			off = ((off64_t)nr_sec * 512) - CRYPT_FOOTER_OFFSET;
		} else {
			printf("Cannot get size of block device %s\n", blk_dev);
			close(fd);
			return -1;
		}
		printf("Size is %llu, offset is %llu\n", ((off64_t)nr_sec * 512), off);
		if (lseek64(fd, off, SEEK_SET) == -1) {
			printf("Cannot seek to real block device footer\n");
			close(fd);
			return -1;
		}

		if ( (cnt = read(fd, buffer, sizeof(buffer))) != sizeof(buffer)) {
			printf("Cannot read real block device footer\n");
			close(fd);
			return -1;
		}
		close(fd);
		if ( (fdout = open("/footerdump", O_WRONLY | O_CREAT, 0644)) < 0) {
			printf("Cannot open output file /footerdump\n");
			return -1;
		}
		if (write(fdout, buffer, sizeof(buffer)) != sizeof(buffer)) {
			printf("Failed to write footer.\n");
		}
		close(fdout);
	}

	return 0;
}
