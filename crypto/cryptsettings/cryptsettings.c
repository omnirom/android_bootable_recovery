#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "../crypto/fs_mgr/include/fs_mgr.h"

#include "cutils/properties.h"

#ifndef PROPERTY_VALUE_MAX
#define PROPERTY_VALUE_MAX 255
#endif
#ifndef FSTAB_PREFIX
#define FSTAB_PREFIX "/fstab."
#endif

int main(void)
{
	char prop[PROPERTY_VALUE_MAX];
	char key_loc[PROPERTY_VALUE_MAX];
	char blk_dev[PROPERTY_VALUE_MAX];
	char fstab_filename[PROPERTY_VALUE_MAX + sizeof(FSTAB_PREFIX)];

	printf("This tool will gather the build flags needed for decryption support for TWRP.\n");
	printf("This tool comes with no warranties whatsoever.\n");
	printf("http://teamw.in\n\n");
	property_get("ro.crypto.state", prop, "encrypted");
	if (strcmp(prop, "encrypted") != 0)
		printf("Your device is not encrypted, continuing anyway.\n\nTW_INCLUDE_CRYPTO := true\n");
	property_get("ro.crypto.fs_type", prop, "ERROR");
	printf("TW_CRYPTO_FS_TYPE := \"%s\"\n", prop);
	property_get("ro.crypto.fs_real_blkdev", prop, "ERROR");
	printf("TW_CRYPTO_REAL_BLKDEV := \"%s\"\n", prop);
	property_get("ro.crypto.fs_mnt_point", prop, "ERROR");
	printf("TW_CRYPTO_MNT_POINT := \"%s\"\n", prop);
	property_get("ro.crypto.fs_options", prop, "ERROR");
	printf("TW_CRYPTO_FS_OPTIONS := \"%s\"\n", prop);
	property_get("ro.crypto.fs_flags", prop, "ERROR");
	printf("TW_CRYPTO_FS_FLAGS := \"%s\"\n", prop);
	property_get("ro.crypto.keyfile.userdata", prop, "footer");
	printf("TW_CRYPTO_KEY_LOC := \"%s\"\n", prop);
	printf("\n*** NEW FOR JELLY BEAN:\n");
	strcpy(fstab_filename, FSTAB_PREFIX);
	property_get("ro.hardware", fstab_filename + sizeof(FSTAB_PREFIX) - 1, "");
	fs_mgr_get_crypt_info(fstab_filename, key_loc, blk_dev, sizeof(key_loc));
	printf("fstab file location: '%s'\n\nTW_INCLUDE_JB_CRYPTO := true\n", fstab_filename);

	return 0;
}
