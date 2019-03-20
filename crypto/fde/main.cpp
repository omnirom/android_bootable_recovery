#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <linux/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <linux/dm-ioctl.h>
#include <libgen.h>
#include <stdlib.h>
#include <sys/param.h>
#include <string.h>
#include <sys/mount.h>
#include <openssl/evp.h>
#include <errno.h>
#include <linux/kdev_t.h>
#include <time.h>
#include "cryptfs.h"
#include "cutils/properties.h"
#include "crypto_scrypt.h"

void usage() {
	printf("  Usage:\n");
	printf("    twrpdec /path/to/userdata /path/to/metadata filesystem password\n");
	printf("\n");
	printf("  The metadata path is the path to the footer. If no metadata\n");
	printf("  partition is present then use footer for this argument.\n");
	printf("\n");
	printf("  Example:\n");
	printf("    twrpdec /dev/block/bootdevice/by-name/userdata footer ext4 0000\n");
}

int main(int argc, char **argv) {
	if (argc != 5) {
		usage();
		return -1;
	}
	set_partition_data(argv[1], argv[2], argv[3]);
	//int ret = cryptfs_check_passwd("30303030");
	int ret = cryptfs_check_passwd(argv[4]);
	return 0;
}
