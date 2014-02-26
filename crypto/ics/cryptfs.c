/*
 * Copyright (C) 2010 The Android Open Source Project
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
 *   1.  Perhaps keep several copies of the encrypted key, in case something
 *       goes horribly wrong?
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
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
#include <openssl/sha.h>
#include <errno.h>
#include <cutils/android_reboot.h>
#include <ext4.h>
#include <linux/kdev_t.h>
#include "cryptfs.h"
#define LOG_TAG "Cryptfs"
#include "cutils/log.h"
#include "cutils/properties.h"
#include "hardware_legacy/power.h"
//#include "VolumeManager.h"

#define DM_CRYPT_BUF_SIZE 4096
#define DATA_MNT_POINT "/data"

#define HASH_COUNT 2000
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
#define KEY_LEN_BYTES_SAMSUNG (sizeof(edk_t))
#endif
#define KEY_LEN_BYTES 16
#define IV_LEN_BYTES 16

#define KEY_LOC_PROP   "ro.crypto.keyfile.userdata"
#define KEY_IN_FOOTER  "footer"

#define EXT4_FS 1
#define FAT_FS 2

#ifndef EXPAND
#define STRINGIFY(x) #x
#define EXPAND(x) STRINGIFY(x)
#endif

char *me = "cryptfs";

static char *saved_data_blkdev;
static char *saved_mount_point;
static int  master_key_saved = 0;
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
static int  using_samsung_encryption = 0;
//static edk_t saved_master_key;
static unsigned char saved_master_key[KEY_LEN_BYTES_SAMSUNG];
edk_payload_t edk_payload;
#else
static unsigned char saved_master_key[KEY_LEN_BYTES];
#endif

int cryptfs_setup_volume(const char *label, const char *real_blkdev, char *crypto_blkdev);


static void ioctl_init(struct dm_ioctl *io, size_t dataSize, const char *name, unsigned flags)
{
    memset(io, 0, dataSize);
    io->data_size = dataSize;
    io->data_start = sizeof(struct dm_ioctl);
    io->version[0] = 4;
    io->version[1] = 0;
    io->version[2] = 0;
    io->flags = flags;
    if (name) {
        strncpy(io->name, name, sizeof(io->name));
    }
}

static unsigned int get_blkdev_size(int fd)
{
  unsigned int nr_sec;

  if ( (ioctl(fd, BLKGETSIZE, &nr_sec)) == -1) {
    nr_sec = 0;
  }

  return nr_sec;
}

/* key or salt can be NULL, in which case just skip writing that value.  Useful to
 * update the failed mount count but not change the key.
 */
static int put_crypt_ftr_and_key(char *real_blk_name, struct crypt_mnt_ftr *crypt_ftr,
                                  unsigned char *key, unsigned char *salt)
{
  // we don't need to update it...
  return 0;
}

static int get_crypt_ftr_and_key(char *real_blk_name, struct crypt_mnt_ftr *crypt_ftr,
                                  unsigned char *key, unsigned char *salt)
{
  int fd;
  unsigned int nr_sec, cnt;
  off64_t off;
  int rc = -1;
  char key_loc[PROPERTY_VALUE_MAX];
  char *fname;
  struct stat statbuf;

  property_get(KEY_LOC_PROP, key_loc, KEY_IN_FOOTER);

  if (!strcmp(key_loc, KEY_IN_FOOTER)) {
    fname = real_blk_name;
    if ( (fd = open(fname, O_RDONLY)) < 0) {
      printf("Cannot open real block device %s\n", fname);
      return -1;
    }

    if ( (nr_sec = get_blkdev_size(fd)) == 0) {
      SLOGE("Cannot get size of block device %s\n", fname);
      goto errout;
    }

    /* If it's an encrypted Android partition, the last 16 Kbytes contain the
     * encryption info footer and key, and plenty of bytes to spare for future
     * growth.
     */
    off = ((off64_t)nr_sec * 512) - CRYPT_FOOTER_OFFSET;

    if (lseek64(fd, off, SEEK_SET) == -1) {
      printf("Cannot seek to real block device footer\n");
      goto errout;
    }
  } else if (key_loc[0] == '/') {
    fname = key_loc;
    if ( (fd = open(fname, O_RDONLY)) < 0) {
      printf("Cannot open footer file %s\n", fname);
      return -1;
    }

    /* Make sure it's 16 Kbytes in length */
    fstat(fd, &statbuf);
    if (S_ISREG(statbuf.st_mode) && (statbuf.st_size != 0x4000
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
        && statbuf.st_size != 0x8000
#endif
        )) {
      printf("footer file %s is not the expected size!\n", fname);
      goto errout;
    }
  } else {
    printf("Unexpected value for" KEY_LOC_PROP "\n");
    return -1;;
  }

  if ( (cnt = read(fd, crypt_ftr, sizeof(struct crypt_mnt_ftr))) != sizeof(struct crypt_mnt_ftr)) {
    printf("Cannot read real block device footer\n");
    goto errout;
  }

  if (crypt_ftr->magic != CRYPT_MNT_MAGIC) {
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
	if (crypt_ftr->magic != CRYPT_MNT_MAGIC_SAMSUNG) {
		printf("Bad magic for real block device %s\n", fname);
		goto errout;
	} else {
		printf("Using Samsung encryption.\n");
		using_samsung_encryption = 1;
        if ( (cnt = read(fd, &edk_payload, sizeof(edk_payload_t))) != sizeof(edk_payload_t)) {
            printf("Cannot read EDK payload from real block device footer\n");
            goto errout;
        }
        if (lseek64(fd, sizeof(__le32), SEEK_CUR) == -1) {
            printf("Cannot seek past unknown data from real block device footer\n");
            goto errout;
        }
        memcpy(key, &edk_payload, sizeof(edk_payload_t));
	}
#else
    printf("Bad magic for real block device %s\n", fname);
    goto errout;
#endif
  }

  if (crypt_ftr->major_version != 1) {
    printf("Cannot understand major version %d real block device footer\n",
          crypt_ftr->major_version);
    goto errout;
  }

  if (crypt_ftr->minor_version != 0) {
    printf("Warning: crypto footer minor version %d, expected 0, continuing...\n",
          crypt_ftr->minor_version);
  }

  if (crypt_ftr->ftr_size > sizeof(struct crypt_mnt_ftr)) {
    /* the footer size is bigger than we expected.
     * Skip to it's stated end so we can read the key.
     */
    if (lseek64(fd, crypt_ftr->ftr_size - sizeof(struct crypt_mnt_ftr),  SEEK_CUR) == -1) {
      printf("Cannot seek to start of key\n");
      goto errout;
    }
  }

  if (crypt_ftr->keysize > sizeof(saved_master_key)) {
    printf("Keysize of %d bits not supported for real block device %s\n",
          crypt_ftr->keysize * 8, fname);
    goto errout;
  }

  if ( (cnt = read(fd, key, crypt_ftr->keysize)) != crypt_ftr->keysize) {
    printf("Cannot read key for real block device %s\n", fname);
    goto errout;
  }

  if (lseek64(fd, KEY_TO_SALT_PADDING, SEEK_CUR) == -1) {
    printf("Cannot seek to real block device salt\n");
    goto errout;
  }

  if ( (cnt = read(fd, salt, SALT_LEN)) != SALT_LEN) {
    printf("Cannot read salt for real block device %s\n", fname);
    goto errout;
  }

  /* Success! */
  rc = 0;

errout:
  close(fd);
  return rc;
}

/* Convert a binary key of specified length into an ascii hex string equivalent,
 * without the leading 0x and with null termination
 */
void convert_key_to_hex_ascii(unsigned char *master_key, unsigned int keysize,
                              char *master_key_ascii)
{
  unsigned int i, a;
  unsigned char nibble;

  for (i=0, a=0; i<keysize; i++, a+=2) {
    /* For each byte, write out two ascii hex digits */
    nibble = (master_key[i] >> 4) & 0xf;
    master_key_ascii[a] = nibble + (nibble > 9 ? 0x37 : 0x30);

    nibble = master_key[i] & 0xf;
    master_key_ascii[a+1] = nibble + (nibble > 9 ? 0x37 : 0x30);
  }

  /* Add the null termination */
  master_key_ascii[a] = '\0';

}

static int create_crypto_blk_dev(struct crypt_mnt_ftr *crypt_ftr, unsigned char *master_key,
                                    const char *real_blk_name, char *crypto_blk_name, const char *name)
{
  char buffer[DM_CRYPT_BUF_SIZE];
  char master_key_ascii[129]; /* Large enough to hold 512 bit key and null */
  char *crypt_params;
  struct dm_ioctl *io;
  struct dm_target_spec *tgt;
  unsigned int minor;
  int fd;
  int retval = -1;

  if ((fd = open("/dev/device-mapper", O_RDWR)) < 0 ) {
    printf("Cannot open device-mapper\n");
    goto errout;
  }

  io = (struct dm_ioctl *) buffer;

  ioctl_init(io, DM_CRYPT_BUF_SIZE, name, 0);
  if (ioctl(fd, DM_DEV_CREATE, io)) {
    printf("Cannot create dm-crypt device\n");
    goto errout;
  }

  /* Get the device status, in particular, the name of it's device file */
  ioctl_init(io, DM_CRYPT_BUF_SIZE, name, 0);
  if (ioctl(fd, DM_DEV_STATUS, io)) {
    printf("Cannot retrieve dm-crypt device status\n");
    goto errout;
  }
  minor = (io->dev & 0xff) | ((io->dev >> 12) & 0xfff00);
  snprintf(crypto_blk_name, MAXPATHLEN, "/dev/block/dm-%u", minor);

  /* Load the mapping table for this device */
  tgt = (struct dm_target_spec *) &buffer[sizeof(struct dm_ioctl)];

  ioctl_init(io, 4096, name, 0);
  io->target_count = 1;
  tgt->status = 0;
  tgt->sector_start = 0;
  tgt->length = crypt_ftr->fs_size;
  strcpy(tgt->target_type, "crypt");

  crypt_params = buffer + sizeof(struct dm_ioctl) + sizeof(struct dm_target_spec);
  convert_key_to_hex_ascii(master_key, crypt_ftr->keysize, master_key_ascii);
  sprintf(crypt_params, "%s %s 0 %s 0", crypt_ftr->crypto_type_name,
          master_key_ascii, real_blk_name);
  //printf("cryptsetup params: '%s'\n", crypt_params);
  crypt_params += strlen(crypt_params) + 1;
  crypt_params = (char *) (((unsigned long)crypt_params + 7) & ~8); /* Align to an 8 byte boundary */
  tgt->next = crypt_params - buffer;

  if (ioctl(fd, DM_TABLE_LOAD, io)) {
      printf("Cannot load dm-crypt mapping table.\n");
      goto errout;
  }

  /* Resume this device to activate it */
  ioctl_init(io, 4096, name, 0);

  if (ioctl(fd, DM_DEV_SUSPEND, io)) {
    printf("Cannot resume the dm-crypt device\n");
    goto errout;
  }

  /* We made it here with no errors.  Woot! */
  retval = 0;

errout:
  close(fd);   /* If fd is <0 from a failed open call, it's safe to just ignore the close error */

  return retval;
}

static int delete_crypto_blk_dev(const char *name)
{
  int fd;
  char buffer[DM_CRYPT_BUF_SIZE];
  struct dm_ioctl *io;
  int retval = -1;

  if ((fd = open("/dev/device-mapper", O_RDWR)) < 0 ) {
    printf("Cannot open device-mapper\n");
    goto errout;
  }

  io = (struct dm_ioctl *) buffer;

  ioctl_init(io, DM_CRYPT_BUF_SIZE, name, 0);
  if (ioctl(fd, DM_DEV_REMOVE, io)) {
    printf("Cannot remove dm-crypt device\n");
    goto errout;
  }

  /* We made it here with no errors.  Woot! */
  retval = 0;

errout:
  close(fd);    /* If fd is <0 from a failed open call, it's safe to just ignore the close error */

  return retval;

}

static void pbkdf2(char *passwd, unsigned char *salt, unsigned char *ikey)
{
    /* Turn the password into a key and IV that can decrypt the master key */
    PKCS5_PBKDF2_HMAC_SHA1(passwd, strlen(passwd), salt, SALT_LEN,
                           HASH_COUNT, KEY_LEN_BYTES+IV_LEN_BYTES, ikey);
}

static int decrypt_master_key(char *passwd, unsigned char *salt,
                              unsigned char *encrypted_master_key,
                              unsigned char *decrypted_master_key)
{
#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
    if (using_samsung_encryption) {
		property_set("rw.km_fips_status", "ready");
		return decrypt_EDK((dek_t*)decrypted_master_key, (edk_payload_t*)encrypted_master_key, passwd);
	}
#endif

  unsigned char ikey[32+32] = { 0 }; /* Big enough to hold a 256 bit key and 256 bit IV */
  EVP_CIPHER_CTX d_ctx;
  int decrypted_len, final_len;

  /* Turn the password into a key and IV that can decrypt the master key */
  pbkdf2(passwd, salt, ikey);

  /* Initialize the decryption engine */
  if (! EVP_DecryptInit(&d_ctx, EVP_aes_128_cbc(), ikey, ikey+KEY_LEN_BYTES)) {
    return -1;
  }
  EVP_CIPHER_CTX_set_padding(&d_ctx, 0); /* Turn off padding as our data is block aligned */
  /* Decrypt the master key */
  if (! EVP_DecryptUpdate(&d_ctx, decrypted_master_key, &decrypted_len,
                            encrypted_master_key, KEY_LEN_BYTES)) {
    return -1;
  }
  if (! EVP_DecryptFinal(&d_ctx, decrypted_master_key + decrypted_len, &final_len)) {
    return -1;
  }

  if (decrypted_len + final_len != KEY_LEN_BYTES) {
    return -1;
  } else {
    return 0;
  }
}

static int get_orig_mount_parms(
        const char *mount_point, char *fs_type, char *real_blkdev,
        unsigned long *mnt_flags, char *fs_options)
{
  char mount_point2[PROPERTY_VALUE_MAX];
  char fs_flags[PROPERTY_VALUE_MAX];

  property_get("ro.crypto.fs_type", fs_type, "");
  property_get("ro.crypto.fs_real_blkdev", real_blkdev, "");
  property_get("ro.crypto.fs_mnt_point", mount_point2, "");
  property_get("ro.crypto.fs_options", fs_options, "");
  property_get("ro.crypto.fs_flags", fs_flags, "");
  *mnt_flags = strtol(fs_flags, 0, 0);

  if (strcmp(mount_point, mount_point2)) {
    /* Consistency check.  These should match. If not, something odd happened. */
    return -1;
  }

  return 0;
}

static int get_orig_mount_parms_sd(
        const char *mount_point, char *fs_type, char *real_blkdev)
{
    char mount_point2[PROPERTY_VALUE_MAX];

    property_get("ro.crypto.sd_fs_type", fs_type, "");
    property_get("ro.crypto.sd_fs_real_blkdev", real_blkdev, "");
    property_get("ro.crypto.sd_fs_mnt_point", mount_point2, "");

    if (strcmp(mount_point, mount_point2)) {
        /* Consistency check.  These should match. If not, something odd happened. */
        return -1;
    }

    return 0;
}

static int test_mount_encrypted_fs(
        char *passwd, char *mount_point, char *label, char *crypto_blkdev)
{
  struct crypt_mnt_ftr crypt_ftr;
  /* Allocate enough space for a 256 bit key, but we may use less */
  unsigned char encrypted_master_key[256], decrypted_master_key[32];
  unsigned char salt[SALT_LEN];
  char real_blkdev[MAXPATHLEN];
  char fs_type[PROPERTY_VALUE_MAX];
  char fs_options[PROPERTY_VALUE_MAX];
  char tmp_mount_point[MAXPATHLEN];
  unsigned long mnt_flags;
  unsigned int orig_failed_decrypt_count;
  char encrypted_state[PROPERTY_VALUE_MAX];
  int rc;

  property_get("ro.crypto.state", encrypted_state, "");
  if ( master_key_saved || strcmp(encrypted_state, "encrypted") ) {
    printf("encrypted fs already validated or not running with encryption, aborting %s\n", encrypted_state);
    return -1;
  }

  if (get_orig_mount_parms(mount_point, fs_type, real_blkdev, &mnt_flags, fs_options)) {
    printf("Error reading original mount parms for mount point %s\n", mount_point);
    return -1;
  }

  if (get_crypt_ftr_and_key(real_blkdev, &crypt_ftr, encrypted_master_key, salt)) {
    printf("Error getting crypt footer and key\n");
    return -1;
  }

  //printf("crypt_ftr->fs_size = %lld\n", crypt_ftr.fs_size);
  orig_failed_decrypt_count = crypt_ftr.failed_decrypt_count;

  if (! (crypt_ftr.flags & CRYPT_MNT_KEY_UNENCRYPTED) ) {
    decrypt_master_key(passwd, salt, encrypted_master_key, decrypted_master_key);
  }

  if (create_crypto_blk_dev(&crypt_ftr, decrypted_master_key, real_blkdev,
                    crypto_blkdev, label)) {
    printf("Error creating decrypted block device\n");
    return -1;
  }

  /* If init detects an encrypted filesystme, it writes a file for each such
   * encrypted fs into the tmpfs /data filesystem, and then the framework finds those
   * files and passes that data to me */
  /* Create a tmp mount point to try mounting the decryptd fs
   * Since we're here, the mount_point should be a tmpfs filesystem, so make
   * a directory in it to test mount the decrypted filesystem.
   */
  sprintf(tmp_mount_point, "%s/tmp_mnt", mount_point);
  mkdir(tmp_mount_point, 0755);
  if ( mount(crypto_blkdev, tmp_mount_point, fs_type, MS_RDONLY, "") ) {
    printf("Error temp mounting decrypted block device\n");
    delete_crypto_blk_dev(label);
    crypt_ftr.failed_decrypt_count++;
  } else {
    /* Success, so just umount and we'll mount it properly when we restart
     * the framework.
     */
    umount(tmp_mount_point);
    crypt_ftr.failed_decrypt_count  = 0;
  }

  rmdir(tmp_mount_point);

  if (crypt_ftr.failed_decrypt_count) {
    /* We failed to mount the device, so return an error */
    rc = crypt_ftr.failed_decrypt_count;

  } else {
    /* Woot!  Success!  Save the name of the crypto block device
     * so we can mount it when restarting the framework.
     */
    property_set("ro.crypto.fs_crypto_blkdev", crypto_blkdev);

    /* Also save a the master key so we can reencrypted the key
     * the key when we want to change the password on it.
     */
    memcpy(saved_master_key, decrypted_master_key, sizeof(saved_master_key));
    saved_data_blkdev = strdup(real_blkdev);
    saved_mount_point = strdup(mount_point);
    master_key_saved = 1;
    rc = 0;
  }

  return rc;
}

static int test_mount_encrypted_fs_sd(
        const char *passwd, const char *mount_point, const char *label)
{
    char real_blkdev[MAXPATHLEN];
    char crypto_blkdev[MAXPATHLEN];
    char tmp_mount_point[MAXPATHLEN];
    char encrypted_state[PROPERTY_VALUE_MAX];
    char fs_type[PROPERTY_VALUE_MAX];
    int rc;

    property_get("ro.crypto.state", encrypted_state, "");
    if ( !master_key_saved || strcmp(encrypted_state, "encrypted") ) {
        printf("encrypted fs not yet validated or not running with encryption, aborting\n");
        return -1;
    }

    if (get_orig_mount_parms_sd(mount_point, fs_type, real_blkdev)) {
        printf("Error reading original mount parms for mount point %s\n", mount_point);
        return -1;
    }

    rc = cryptfs_setup_volume(label, real_blkdev, crypto_blkdev);
    if(rc){
        printf("Error setting up cryptfs volume %s\n", real_blkdev);
        return -1;
    }

    sprintf(tmp_mount_point, "%s/tmp_mnt", mount_point);
    mkdir(tmp_mount_point, 0755);
    if ( mount(crypto_blkdev, tmp_mount_point, fs_type, MS_RDONLY, "") ) {
        printf("Error temp mounting decrypted block device\n");
        delete_crypto_blk_dev(label);
    } else {
        /* Success, so just umount and we'll mount it properly when we restart
        * the framework.
        */
        umount(tmp_mount_point);

        property_set("ro.crypto.sd_fs_crypto_blkdev", crypto_blkdev);
    }

    rmdir(tmp_mount_point);

    return rc;
}

/*
 * Called by vold when it's asked to mount an encrypted, nonremovable volume.
 * Setup a dm-crypt mapping, use the saved master key from
 * setting up the /data mapping, and return the new device path.
 */
int cryptfs_setup_volume(const char *label, const char *real_blkdev, char *crypto_blkdev)
{
    struct crypt_mnt_ftr sd_crypt_ftr;
    unsigned char key[256], salt[32];
    struct stat statbuf;
    int nr_sec, fd, rc;

    /* Just want the footer, but gotta get it all */
    get_crypt_ftr_and_key(saved_data_blkdev, &sd_crypt_ftr, key, salt);

    /* Update the fs_size field to be the size of the volume */
    fd = open(real_blkdev, O_RDONLY);
    nr_sec = get_blkdev_size(fd);
    close(fd);
    if (nr_sec == 0) {
        SLOGE("Cannot get size of volume %s\n", real_blkdev);
        return -1;
    }

#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
    if(using_samsung_encryption) {
        if(!access("/efs/essiv", R_OK)){
            strcpy(sd_crypt_ftr.crypto_type_name, "aes-cbc-plain:sha1");
        }
        else if(!access("/efs/cryptprop_essiv", R_OK)){
            strcpy(sd_crypt_ftr.crypto_type_name, "aes-cbc-essiv:sha256");
        }
    }
#endif

    sd_crypt_ftr.fs_size = nr_sec;
    rc = create_crypto_blk_dev(
            &sd_crypt_ftr, saved_master_key, real_blkdev, crypto_blkdev, label);

    stat(crypto_blkdev, &statbuf);

    return rc;
}

int cryptfs_crypto_complete(void)
{
  return -1;
}

int cryptfs_check_footer(void)
{
    int rc = -1;
    char fs_type[PROPERTY_VALUE_MAX];
    char real_blkdev[MAXPATHLEN];
    char fs_options[PROPERTY_VALUE_MAX];
    unsigned long mnt_flags;
    struct crypt_mnt_ftr crypt_ftr;
    /* Allocate enough space for a 256 bit key, but we may use less */
    unsigned char encrypted_master_key[256];
    unsigned char salt[SALT_LEN];

    if (get_orig_mount_parms(DATA_MNT_POINT, fs_type, real_blkdev, &mnt_flags, fs_options)) {
        printf("Error reading original mount parms for mount point %s\n", DATA_MNT_POINT);
        return rc;
    }

    rc = get_crypt_ftr_and_key(real_blkdev, &crypt_ftr, encrypted_master_key, salt);

    return rc;
}

int cryptfs_check_passwd(const char *passwd)
{
    char pwbuf[256];
    char crypto_blkdev_data[MAXPATHLEN];
    int rc = -1;

    strcpy(pwbuf, passwd);
    rc = test_mount_encrypted_fs(pwbuf, DATA_MNT_POINT, "userdata", crypto_blkdev_data);

#ifdef TW_INCLUDE_CRYPTO_SAMSUNG
    if(using_samsung_encryption) {

        int rc2 = 1;
#ifndef RECOVERY_SDCARD_ON_DATA
#ifdef TW_INTERNAL_STORAGE_PATH
		// internal storage for non data/media devices
        if(!rc) {
            strcpy(pwbuf, passwd);
            rc2 = test_mount_encrypted_fs_sd(
                    pwbuf, EXPAND(TW_INTERNAL_STORAGE_PATH),
                    EXPAND(TW_INTERNAL_STORAGE_MOUNT_POINT));
        }
#endif
#endif
#ifdef TW_EXTERNAL_STORAGE_PATH
		printf("Temp mounting /data\n");
		// mount data so mount_ecryptfs_drive can access edk in /data/system/
		rc2 = mount(crypto_blkdev_data, DATA_MNT_POINT, CRYPTO_FS_TYPE, MS_RDONLY, "");
        // external sd
		char decrypt_external[256], external_blkdev[256];
		property_get("ro.crypto.external_encrypted", decrypt_external, "0");
		// Mount the external storage as ecryptfs so that ecryptfs can act as a pass-through
		if (!rc2 && strcmp(decrypt_external, "1") == 0) {
			printf("Mounting external with ecryptfs...\n");
            strcpy(pwbuf, passwd);
            rc2 = mount_ecryptfs_drive(
                    pwbuf, EXPAND(TW_EXTERNAL_STORAGE_PATH),
                    EXPAND(TW_EXTERNAL_STORAGE_PATH), 0);
			if (rc2 == 0)
				property_set("ro.crypto.external_use_ecryptfs", "1");
			else
				property_set("ro.crypto.external_use_ecryptfs", "0");
        } else {
			printf("Unable to mount external storage with ecryptfs.\n");
			umount(EXPAND(TW_EXTERNAL_STORAGE_PATH));
		}
        umount(DATA_MNT_POINT);
    }
#endif // #ifdef TW_EXTERNAL_STORAGE_PATH
#endif // #ifdef TW_INCLUDE_CRYPTO_SAMSUNG
    return rc;
}
