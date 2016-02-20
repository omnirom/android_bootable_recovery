/* vi: set sw=4 ts=4: */
/* Ported to busybox from mtd-utils.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

//usage:#define flash_lock_trivial_usage
//usage:       "MTD_DEVICE OFFSET SECTORS"
//usage:#define flash_lock_full_usage "\n\n"
//usage:       "Lock part or all of an MTD device. If SECTORS is -1, then all sectors\n"
//usage:       "will be locked, regardless of the value of OFFSET"
//usage:
//usage:#define flash_unlock_trivial_usage
//usage:       "MTD_DEVICE"
//usage:#define flash_unlock_full_usage "\n\n"
//usage:       "Unlock an MTD device"

#include "libbb.h"
#include <mtd/mtd-user.h>

int flash_lock_unlock_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int flash_lock_unlock_main(int argc UNUSED_PARAM, char **argv)
{
	/* note: fields in these structs are 32-bits.
	 * apparently we can't win anything by using off_t
	 * or long long's for offset and/or sectors vars. */
	struct mtd_info_user info;
	struct erase_info_user lock;
	unsigned long offset;
	long sectors;
	int fd;

#define do_lock (ENABLE_FLASH_LOCK && (!ENABLE_FLASH_UNLOCK || (applet_name[6] == 'l')))

	if (!argv[1])
		bb_show_usage();

	/* parse offset and number of sectors to lock */
	offset = 0;
	sectors = -1;
	if (do_lock) {
		if (!argv[2] || !argv[3])
			bb_show_usage();
		offset = xstrtoul(argv[2], 0);
		sectors = xstrtol(argv[3], 0);
	}

	fd = xopen(argv[1], O_RDWR);

	xioctl(fd, MEMGETINFO, &info);

	lock.start = 0;
	lock.length = info.size;
	if (do_lock) {
		unsigned long size = info.size - info.erasesize;
		if (offset > size) {
			bb_error_msg_and_die("%lx is beyond device size %lx\n",
					offset, size);
		}

		if (sectors == -1) {
			sectors = info.size / info.erasesize;
		} else {
// isn't this useless?
			long num = info.size / info.erasesize;
			if (sectors > num) {
				bb_error_msg_and_die("%ld are too many "
						"sectors, device only has "
						"%ld\n", sectors, num);
			}
		}

		lock.start = offset;
		lock.length = sectors * info.erasesize;
		xioctl(fd, MEMLOCK, &lock);
	} else {
		xioctl(fd, MEMUNLOCK, &lock);
	}

	return EXIT_SUCCESS;
}
