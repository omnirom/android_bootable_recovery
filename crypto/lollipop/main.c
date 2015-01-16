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
#include <hardware/keymaster.h>

int main() {
	printf("blah\n");
	set_partition_data("/dev/block/mmcblk0p28", "/dev/block/mmcblk0p27", "ext4");
	printf("blah2\n");
	int ret = cryptfs_check_passwd("30303030");
	//int ret = cryptfs_check_passwd("0000");
	return 0;
}
