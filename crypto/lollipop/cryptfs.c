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
#include <ext4.h>
#include <linux/kdev_t.h>
#include <fs_mgr.h>
#include <time.h>
#include "cryptfs.h"
#define LOG_TAG "Cryptfs"
#include "cutils/log.h"
#include "cutils/properties.h"
#include "cutils/android_reboot.h"
#include "hardware_legacy/power.h"
#include <logwrap/logwrap.h>
//#include "VolumeManager.h"
//#include "VoldUtil.h"
#include "crypto_scrypt.h"
#include "ext4_utils.h"
#include "f2fs_sparseblock.h"
//#include "CheckBattery.h"
//#include "Process.h"

#include <hardware/keymaster.h>

#define UNUSED __attribute__((unused))

#define UNUSED __attribute__((unused))

#define DM_CRYPT_BUF_SIZE 4096

#define HASH_COUNT 2000
#define KEY_LEN_BYTES 16
#define IV_LEN_BYTES 16

#define KEY_IN_FOOTER  "footer"

// "default_password" encoded into hex (d=0x64 etc)
#define DEFAULT_PASSWORD "64656661756c745f70617373776f7264"

#define EXT4_FS 1
#define F2FS_FS 2

#define TABLE_LOAD_RETRIES 10

#define RSA_KEY_SIZE 2048
#define RSA_KEY_SIZE_BYTES (RSA_KEY_SIZE / 8)
#define RSA_EXPONENT 0x10001

#define RETRY_MOUNT_ATTEMPTS 10
#define RETRY_MOUNT_DELAY_SECONDS 1

char *me = "cryptfs";

static unsigned char saved_master_key[KEY_LEN_BYTES];
static char *saved_mount_point;
static int  master_key_saved = 0;
static struct crypt_persist_data *persist_data = NULL;

static int keymaster_init(keymaster_device_t **keymaster_dev)
{
    int rc;

    const hw_module_t* mod;
    rc = hw_get_module_by_class(KEYSTORE_HARDWARE_MODULE_ID, NULL, &mod);
    if (rc) {
        printf("could not find any keystore module\n");
        goto out;
    }

    rc = keymaster_open(mod, keymaster_dev);
    if (rc) {
        printf("could not open keymaster device in %s (%s)\n",
            KEYSTORE_HARDWARE_MODULE_ID, strerror(-rc));
        goto out;
    }

    return 0;

out:
    *keymaster_dev = NULL;
    return rc;
}

/* Should we use keymaster? */
static int keymaster_check_compatibility()
{
    keymaster_device_t *keymaster_dev = 0;
    int rc = 0;

    if (keymaster_init(&keymaster_dev)) {
        printf("Failed to init keymaster\n");
        rc = -1;
        goto out;
    }

    printf("keymaster version is %d\n", keymaster_dev->common.module->module_api_version);

    if (keymaster_dev->common.module->module_api_version
            < KEYMASTER_MODULE_API_VERSION_0_3) {
        rc = 0;
        goto out;
    }

    if (keymaster_dev->flags & KEYMASTER_BLOBS_ARE_STANDALONE) {
        rc = 1;
    }

out:
    keymaster_close(keymaster_dev);
    return rc;
}

/* Create a new keymaster key and store it in this footer */
static int keymaster_create_key(struct crypt_mnt_ftr *ftr)
{
    uint8_t* key = 0;
    keymaster_device_t *keymaster_dev = 0;

    if (keymaster_init(&keymaster_dev)) {
        printf("Failed to init keymaster\n");
        return -1;
    }

    int rc = 0;

    keymaster_rsa_keygen_params_t params;
    memset(&params, '\0', sizeof(params));
    params.public_exponent = RSA_EXPONENT;
    params.modulus_size = RSA_KEY_SIZE;

    size_t key_size;
    if (keymaster_dev->generate_keypair(keymaster_dev, TYPE_RSA, &params,
                                        &key, &key_size)) {
        printf("Failed to generate keypair\n");
        rc = -1;
        goto out;
    }

    if (key_size > KEYMASTER_BLOB_SIZE) {
        printf("Keymaster key too large for crypto footer\n");
        rc = -1;
        goto out;
    }

    memcpy(ftr->keymaster_blob, key, key_size);
    ftr->keymaster_blob_size = key_size;

out:
    keymaster_close(keymaster_dev);
    free(key);
    return rc;
}

/* This signs the given object using the keymaster key. */
static int keymaster_sign_object(struct crypt_mnt_ftr *ftr,
                                 const unsigned char *object,
                                 const size_t object_size,
                                 unsigned char **signature,
                                 size_t *signature_size)
{
    int rc = 0;
    keymaster_device_t *keymaster_dev = 0;
    if (keymaster_init(&keymaster_dev)) {
        printf("Failed to init keymaster\n");
        return -1;
    }

    /* We currently set the digest type to DIGEST_NONE because it's the
     * only supported value for keymaster. A similar issue exists with
     * PADDING_NONE. Long term both of these should likely change.
     */
    keymaster_rsa_sign_params_t params;
    params.digest_type = DIGEST_NONE;
    params.padding_type = PADDING_NONE;

    unsigned char to_sign[RSA_KEY_SIZE_BYTES];
    size_t to_sign_size = sizeof(to_sign);
    memset(to_sign, 0, RSA_KEY_SIZE_BYTES);

    // To sign a message with RSA, the message must satisfy two
    // constraints:
    //
    // 1. The message, when interpreted as a big-endian numeric value, must
    //    be strictly less than the public modulus of the RSA key.  Note
    //    that because the most significant bit of the public modulus is
    //    guaranteed to be 1 (else it's an (n-1)-bit key, not an n-bit
    //    key), an n-bit message with most significant bit 0 always
    //    satisfies this requirement.
    //
    // 2. The message must have the same length in bits as the public
    //    modulus of the RSA key.  This requirement isn't mathematically
    //    necessary, but is necessary to ensure consistency in
    //    implementations.
    switch (ftr->kdf_type) {
        case KDF_SCRYPT_KEYMASTER_UNPADDED:
            // This is broken: It produces a message which is shorter than
            // the public modulus, failing criterion 2.
            memcpy(to_sign, object, object_size);
            to_sign_size = object_size;
            printf("Signing unpadded object\n");
            break;
        case KDF_SCRYPT_KEYMASTER_BADLY_PADDED:
            // This is broken: Since the value of object is uniformly
            // distributed, it produces a message that is larger than the
            // public modulus with probability 0.25.
            memcpy(to_sign, object, min(RSA_KEY_SIZE_BYTES, object_size));
            printf("Signing end-padded object\n");
            break;
        case KDF_SCRYPT_KEYMASTER:
            // This ensures the most significant byte of the signed message
            // is zero.  We could have zero-padded to the left instead, but
            // this approach is slightly more robust against changes in
            // object size.  However, it's still broken (but not unusably
            // so) because we really should be using a proper RSA padding
            // function, such as OAEP.
            //
            // TODO(paullawrence): When keymaster 0.4 is available, change
            // this to use the padding options it provides.
            memcpy(to_sign + 1, object, min(RSA_KEY_SIZE_BYTES - 1, object_size));
            printf("Signing safely-padded object\n");
            break;
        default:
            printf("Unknown KDF type %d\n", ftr->kdf_type);
            return -1;
    }

    rc = keymaster_dev->sign_data(keymaster_dev,
                                  &params,
                                  ftr->keymaster_blob,
                                  ftr->keymaster_blob_size,
                                  to_sign,
                                  to_sign_size,
                                  signature,
                                  signature_size);

    keymaster_close(keymaster_dev);
    return rc;
}

/* Store password when userdata is successfully decrypted and mounted.
 * Cleared by cryptfs_clear_password
 *
 * To avoid a double prompt at boot, we need to store the CryptKeeper
 * password and pass it to KeyGuard, which uses it to unlock KeyStore.
 * Since the entire framework is torn down and rebuilt after encryption,
 * we have to use a daemon or similar to store the password. Since vold
 * is secured against IPC except from system processes, it seems a reasonable
 * place to store this.
 *
 * password should be cleared once it has been used.
 *
 * password is aged out after password_max_age_seconds seconds.
 */
static char* password = 0;
static int password_expiry_time = 0;
static const int password_max_age_seconds = 60;

struct fstab *fstab;

enum RebootType {reboot, recovery, shutdown};
static void cryptfs_reboot(enum RebootType rt)
{
  switch(rt) {
      case reboot:
          property_set(ANDROID_RB_PROPERTY, "reboot");
          break;

      case recovery:
          property_set(ANDROID_RB_PROPERTY, "reboot,recovery");
          break;

      case shutdown:
          property_set(ANDROID_RB_PROPERTY, "shutdown");
          break;
    }

    sleep(20);

    /* Shouldn't get here, reboot should happen before sleep times out */
    return;
}

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

/**
 * Gets the default device scrypt parameters for key derivation time tuning.
 * The parameters should lead to about one second derivation time for the
 * given device.
 */
static void get_device_scrypt_params(struct crypt_mnt_ftr *ftr) {
    const int default_params[] = SCRYPT_DEFAULTS;
    int params[] = SCRYPT_DEFAULTS;
    char paramstr[PROPERTY_VALUE_MAX];
    char *token;
    char *saveptr;
    int i;

    property_get(SCRYPT_PROP, paramstr, "");
    if (paramstr[0] != '\0') {
        /*
         * The token we're looking for should be three integers separated by
         * colons (e.g., "12:8:1"). Scan the property to make sure it matches.
         */
        for (i = 0, token = strtok_r(paramstr, ":", &saveptr);
                token != NULL && i < 3;
                i++, token = strtok_r(NULL, ":", &saveptr)) {
            char *endptr;
            params[i] = strtol(token, &endptr, 10);

            /*
             * Check that there was a valid number and it's 8-bit. If not,
             * break out and the end check will take the default values.
             */
            if ((*token == '\0') || (*endptr != '\0') || params[i] < 0 || params[i] > 255) {
                break;
            }
        }

        /*
         * If there were not enough tokens or a token was malformed (not an
         * integer), it will end up here and the default parameters can be
         * taken.
         */
        if ((i != 3) || (token != NULL)) {
            printf("bad scrypt parameters '%s' should be like '12:8:1'; using defaults", paramstr);
            memcpy(params, default_params, sizeof(params));
        }
    }

    ftr->N_factor = params[0];
    ftr->r_factor = params[1];
    ftr->p_factor = params[2];
}

static unsigned int get_fs_size(char *dev)
{
    int fd, block_size;
    struct ext4_super_block sb;
    off64_t len;

    if ((fd = open(dev, O_RDONLY)) < 0) {
        printf("Cannot open device to get filesystem size ");
        return 0;
    }

    if (lseek64(fd, 1024, SEEK_SET) < 0) {
        printf("Cannot seek to superblock");
        return 0;
    }

    if (read(fd, &sb, sizeof(sb)) != sizeof(sb)) {
        printf("Cannot read superblock");
        return 0;
    }

    close(fd);

    if (le32_to_cpu(sb.s_magic) != EXT4_SUPER_MAGIC) {
        printf("Not a valid ext4 superblock");
        return 0;
    }
    block_size = 1024 << sb.s_log_block_size;
    /* compute length in bytes */
    len = ( ((off64_t)sb.s_blocks_count_hi << 32) + sb.s_blocks_count_lo) * block_size;

    /* return length in sectors */
    return (unsigned int) (len / 512);
}

static unsigned int get_blkdev_size(int fd)
{
  unsigned int nr_sec;

  if ( (ioctl(fd, BLKGETSIZE, &nr_sec)) == -1) {
    nr_sec = 0;
  }

  return nr_sec;
}

static int get_crypt_ftr_info(char **metadata_fname, off64_t *off)
{
  static int cached_data = 0;
  static off64_t cached_off = 0;
  static char cached_metadata_fname[PROPERTY_VALUE_MAX] = "";
  int fd;
  char key_loc[PROPERTY_VALUE_MAX];
  char real_blkdev[PROPERTY_VALUE_MAX];
  unsigned int nr_sec;
  int rc = -1;

  if (!cached_data) {
    fs_mgr_get_crypt_info(fstab, key_loc, real_blkdev, sizeof(key_loc));
    printf("get_crypt_ftr_info crypto key location: '%s'\n", key_loc);
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

/* key or salt can be NULL, in which case just skip writing that value.  Useful to
 * update the failed mount count but not change the key.
 */
static int put_crypt_ftr_and_key(struct crypt_mnt_ftr *crypt_ftr)
{
  printf("TWRP NOT putting crypt footer and key\n");
  return 0;
  int fd;
  unsigned int nr_sec, cnt;
  /* starting_off is set to the SEEK_SET offset
   * where the crypto structure starts
   */
  off64_t starting_off;
  int rc = -1;
  char *fname = NULL;
  struct stat statbuf;

  if (get_crypt_ftr_info(&fname, &starting_off)) {
    printf("Unable to get crypt_ftr_info\n");
    return -1;
  }
  if (fname[0] != '/') {
    printf("Unexpected value for crypto key location\n");
    return -1;
  }
  if ( (fd = open(fname, O_RDWR | O_CREAT, 0600)) < 0) {
    printf("Cannot open footer file %s for put\n", fname);
    return -1;
  }

  /* Seek to the start of the crypt footer */
  if (lseek64(fd, starting_off, SEEK_SET) == -1) {
    printf("Cannot seek to real block device footer\n");
    goto errout;
  }

  if ((cnt = write(fd, crypt_ftr, sizeof(struct crypt_mnt_ftr))) != sizeof(struct crypt_mnt_ftr)) {
    printf("Cannot write real block device footer\n");
    goto errout;
  }

  fstat(fd, &statbuf);
  /* If the keys are kept on a raw block device, do not try to truncate it. */
  if (S_ISREG(statbuf.st_mode)) {
    if (ftruncate(fd, 0x4000)) {
      printf("Cannot set footer file size\n");
      goto errout;
    }
  }

  /* Success! */
  rc = 0;

errout:
  close(fd);
  return rc;

}

static inline int unix_read(int  fd, void*  buff, int  len)
{
    return TEMP_FAILURE_RETRY(read(fd, buff, len));
}

static inline int unix_write(int  fd, const void*  buff, int  len)
{
    return TEMP_FAILURE_RETRY(write(fd, buff, len));
}

static void init_empty_persist_data(struct crypt_persist_data *pdata, int len)
{
    memset(pdata, 0, len);
    pdata->persist_magic = PERSIST_DATA_MAGIC;
    pdata->persist_valid_entries = 0;
}

/* A routine to update the passed in crypt_ftr to the lastest version.
 * fd is open read/write on the device that holds the crypto footer and persistent
 * data, crypt_ftr is a pointer to the struct to be updated, and offset is the
 * absolute offset to the start of the crypt_mnt_ftr on the passed in fd.
 */
static void upgrade_crypt_ftr(int fd, struct crypt_mnt_ftr *crypt_ftr, off64_t offset)
{
    int orig_major = crypt_ftr->major_version;
    int orig_minor = crypt_ftr->minor_version;
printf("TWRP NOT upgrading crypto footer\n");
return; // do not upgrade in recovery
    if ((crypt_ftr->major_version == 1) && (crypt_ftr->minor_version == 0)) {
        struct crypt_persist_data *pdata;
        off64_t pdata_offset = offset + CRYPT_FOOTER_TO_PERSIST_OFFSET;

        printf("upgrading crypto footer to 1.1");

        pdata = malloc(CRYPT_PERSIST_DATA_SIZE);
        if (pdata == NULL) {
            printf("Cannot allocate persisent data\n");
            return;
        }
        memset(pdata, 0, CRYPT_PERSIST_DATA_SIZE);

        /* Need to initialize the persistent data area */
        if (lseek64(fd, pdata_offset, SEEK_SET) == -1) {
            printf("Cannot seek to persisent data offset\n");
            return;
        }
        /* Write all zeros to the first copy, making it invalid */
        unix_write(fd, pdata, CRYPT_PERSIST_DATA_SIZE);

        /* Write a valid but empty structure to the second copy */
        init_empty_persist_data(pdata, CRYPT_PERSIST_DATA_SIZE);
        unix_write(fd, pdata, CRYPT_PERSIST_DATA_SIZE);

        /* Update the footer */
        crypt_ftr->persist_data_size = CRYPT_PERSIST_DATA_SIZE;
        crypt_ftr->persist_data_offset[0] = pdata_offset;
        crypt_ftr->persist_data_offset[1] = pdata_offset + CRYPT_PERSIST_DATA_SIZE;
        crypt_ftr->minor_version = 1;
    }

    if ((crypt_ftr->major_version == 1) && (crypt_ftr->minor_version == 1)) {
        printf("upgrading crypto footer to 1.2");
        /* But keep the old kdf_type.
         * It will get updated later to KDF_SCRYPT after the password has been verified.
         */
        crypt_ftr->kdf_type = KDF_PBKDF2;
        get_device_scrypt_params(crypt_ftr);
        crypt_ftr->minor_version = 2;
    }

    if ((crypt_ftr->major_version == 1) && (crypt_ftr->minor_version == 2)) {
        printf("upgrading crypto footer to 1.3");
        crypt_ftr->crypt_type = CRYPT_TYPE_PASSWORD;
        crypt_ftr->minor_version = 3;
    }

    if ((orig_major != crypt_ftr->major_version) || (orig_minor != crypt_ftr->minor_version)) {
        if (lseek64(fd, offset, SEEK_SET) == -1) {
            printf("Cannot seek to crypt footer\n");
            return;
        }
        unix_write(fd, crypt_ftr, sizeof(struct crypt_mnt_ftr));
    }
}


static int get_crypt_ftr_and_key(struct crypt_mnt_ftr *crypt_ftr)
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
    printf("Unexpected value for crypto key location\n");
    return -1;
  }
  if ( (fd = open(fname, O_RDWR)) < 0) {
    printf("Cannot open footer file %s for get\n", fname);
    return -1;
  }

  /* Make sure it's 16 Kbytes in length */
  fstat(fd, &statbuf);
  if (S_ISREG(statbuf.st_mode) && (statbuf.st_size != 0x4000)) {
    printf("footer file %s is not the expected size!\n", fname);
    goto errout;
  }

  /* Seek to the start of the crypt footer */
  if (lseek64(fd, starting_off, SEEK_SET) == -1) {
    printf("Cannot seek to real block device footer\n");
    goto errout;
  }

  if ( (cnt = read(fd, crypt_ftr, sizeof(struct crypt_mnt_ftr))) != sizeof(struct crypt_mnt_ftr)) {
    printf("Cannot read real block device footer\n");
    goto errout;
  }

  if (crypt_ftr->magic != CRYPT_MNT_MAGIC) {
    printf("Bad magic for real block device %s\n", fname);
    goto errout;
  }

  if (crypt_ftr->major_version != CURRENT_MAJOR_VERSION) {
    printf("Cannot understand major version %d real block device footer; expected %d\n",
          crypt_ftr->major_version, CURRENT_MAJOR_VERSION);
    goto errout;
  }

  if (crypt_ftr->minor_version > CURRENT_MINOR_VERSION) {
    printf("Warning: crypto footer minor version %d, expected <= %d, continuing...\n",
          crypt_ftr->minor_version, CURRENT_MINOR_VERSION);
  }

  /* If this is a verion 1.0 crypt_ftr, make it a 1.1 crypt footer, and update the
   * copy on disk before returning.
   */
  if (crypt_ftr->minor_version < CURRENT_MINOR_VERSION) {
    upgrade_crypt_ftr(fd, crypt_ftr, starting_off);
  }

  /* Success! */
  rc = 0;

errout:
  close(fd);
  return rc;
}

static int validate_persistent_data_storage(struct crypt_mnt_ftr *crypt_ftr)
{
    if (crypt_ftr->persist_data_offset[0] + crypt_ftr->persist_data_size >
        crypt_ftr->persist_data_offset[1]) {
        printf("Crypt_ftr persist data regions overlap");
        return -1;
    }

    if (crypt_ftr->persist_data_offset[0] >= crypt_ftr->persist_data_offset[1]) {
        printf("Crypt_ftr persist data region 0 starts after region 1");
        return -1;
    }

    if (((crypt_ftr->persist_data_offset[1] + crypt_ftr->persist_data_size) -
        (crypt_ftr->persist_data_offset[0] - CRYPT_FOOTER_TO_PERSIST_OFFSET)) >
        CRYPT_FOOTER_OFFSET) {
        printf("Persistent data extends past crypto footer");
        return -1;
    }

    return 0;
}

static int load_persistent_data(void)
{
    struct crypt_mnt_ftr crypt_ftr;
    struct crypt_persist_data *pdata = NULL;
    char encrypted_state[PROPERTY_VALUE_MAX];
    char *fname;
    int found = 0;
    int fd;
    int ret;
    int i;

    if (persist_data) {
        /* Nothing to do, we've already loaded or initialized it */
        return 0;
    }


    /* If not encrypted, just allocate an empty table and initialize it */
    property_get("ro.crypto.state", encrypted_state, "");
    if (strcmp(encrypted_state, "encrypted") ) {
        pdata = malloc(CRYPT_PERSIST_DATA_SIZE);
        if (pdata) {
            init_empty_persist_data(pdata, CRYPT_PERSIST_DATA_SIZE);
            persist_data = pdata;
            return 0;
        }
        return -1;
    }

    if(get_crypt_ftr_and_key(&crypt_ftr)) {
        return -1;
    }

    if ((crypt_ftr.major_version < 1)
        || (crypt_ftr.major_version == 1 && crypt_ftr.minor_version < 1)) {
        printf("Crypt_ftr version doesn't support persistent data");
        return -1;
    }

    if (get_crypt_ftr_info(&fname, NULL)) {
        return -1;
    }

    ret = validate_persistent_data_storage(&crypt_ftr);
    if (ret) {
        return -1;
    }

    fd = open(fname, O_RDONLY);
    if (fd < 0) {
        printf("Cannot open %s metadata file", fname);
        return -1;
    }

    if (persist_data == NULL) {
        pdata = malloc(crypt_ftr.persist_data_size);
        if (pdata == NULL) {
            printf("Cannot allocate memory for persistent data");
            goto err;
        }
    }

    for (i = 0; i < 2; i++) {
        if (lseek64(fd, crypt_ftr.persist_data_offset[i], SEEK_SET) < 0) {
            printf("Cannot seek to read persistent data on %s", fname);
            goto err2;
        }
        if (unix_read(fd, pdata, crypt_ftr.persist_data_size) < 0){
            printf("Error reading persistent data on iteration %d", i);
            goto err2;
        }
        if (pdata->persist_magic == PERSIST_DATA_MAGIC) {
            found = 1;
            break;
        }
    }

    if (!found) {
        printf("Could not find valid persistent data, creating");
        init_empty_persist_data(pdata, crypt_ftr.persist_data_size);
    }

    /* Success */
    persist_data = pdata;
    close(fd);
    return 0;

err2:
    free(pdata);

err:
    close(fd);
    return -1;
}

static int save_persistent_data(void)
{
    struct crypt_mnt_ftr crypt_ftr;
    struct crypt_persist_data *pdata;
    char *fname;
    off64_t write_offset;
    off64_t erase_offset;
    int found = 0;
    int fd;
    int ret;

    if (persist_data == NULL) {
        printf("No persistent data to save");
        return -1;
    }

    if(get_crypt_ftr_and_key(&crypt_ftr)) {
        return -1;
    }

    if ((crypt_ftr.major_version < 1)
        || (crypt_ftr.major_version == 1 && crypt_ftr.minor_version < 1)) {
        printf("Crypt_ftr version doesn't support persistent data");
        return -1;
    }

    ret = validate_persistent_data_storage(&crypt_ftr);
    if (ret) {
        return -1;
    }

    if (get_crypt_ftr_info(&fname, NULL)) {
        return -1;
    }

    fd = open(fname, O_RDWR);
    if (fd < 0) {
        printf("Cannot open %s metadata file", fname);
        return -1;
    }

    pdata = malloc(crypt_ftr.persist_data_size);
    if (pdata == NULL) {
        printf("Cannot allocate persistant data");
        goto err;
    }

    if (lseek64(fd, crypt_ftr.persist_data_offset[0], SEEK_SET) < 0) {
        printf("Cannot seek to read persistent data on %s", fname);
        goto err2;
    }

    if (unix_read(fd, pdata, crypt_ftr.persist_data_size) < 0) {
            printf("Error reading persistent data before save");
            goto err2;
    }

    if (pdata->persist_magic == PERSIST_DATA_MAGIC) {
        /* The first copy is the curent valid copy, so write to
         * the second copy and erase this one */
       write_offset = crypt_ftr.persist_data_offset[1];
       erase_offset = crypt_ftr.persist_data_offset[0];
    } else {
        /* The second copy must be the valid copy, so write to
         * the first copy, and erase the second */
       write_offset = crypt_ftr.persist_data_offset[0];
       erase_offset = crypt_ftr.persist_data_offset[1];
    }

    /* Write the new copy first, if successful, then erase the old copy */
    if (lseek(fd, write_offset, SEEK_SET) < 0) {
        printf("Cannot seek to write persistent data");
        goto err2;
    }
    if (unix_write(fd, persist_data, crypt_ftr.persist_data_size) ==
        (int) crypt_ftr.persist_data_size) {
        if (lseek(fd, erase_offset, SEEK_SET) < 0) {
            printf("Cannot seek to erase previous persistent data");
            goto err2;
        }
        fsync(fd);
        memset(pdata, 0, crypt_ftr.persist_data_size);
        if (unix_write(fd, pdata, crypt_ftr.persist_data_size) !=
            (int) crypt_ftr.persist_data_size) {
            printf("Cannot write to erase previous persistent data");
            goto err2;
        }
        fsync(fd);
    } else {
        printf("Cannot write to save persistent data");
        goto err2;
    }

    /* Success */
    free(pdata);
    close(fd);
    return 0;

err2:
    free(pdata);
err:
    close(fd);
    return -1;
}

static int hexdigit (char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static unsigned char* convert_hex_ascii_to_key(const char* master_key_ascii,
                                               unsigned int* out_keysize)
{
    unsigned int i;
    *out_keysize = 0;

    size_t size = strlen (master_key_ascii);
    if (size % 2) {
        printf("Trying to convert ascii string of odd length");
        return NULL;
    }

    unsigned char* master_key = (unsigned char*) malloc(size / 2);
    if (master_key == 0) {
        printf("Cannot allocate");
        return NULL;
    }

    for (i = 0; i < size; i += 2) {
        int high_nibble = hexdigit (master_key_ascii[i]);
        int low_nibble = hexdigit (master_key_ascii[i + 1]);

        if(high_nibble < 0 || low_nibble < 0) {
            printf("Invalid hex string");
            free (master_key);
            return NULL;
        }

        master_key[*out_keysize] = high_nibble * 16 + low_nibble;
        (*out_keysize)++;
    }

    return master_key;
}

/* Convert a binary key of specified length into an ascii hex string equivalent,
 * without the leading 0x and with null termination
 */
static void convert_key_to_hex_ascii(unsigned char *master_key, unsigned int keysize,
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

static int load_crypto_mapping_table(struct crypt_mnt_ftr *crypt_ftr, unsigned char *master_key,
                                     char *real_blk_name, const char *name, int fd,
                                     char *extra_params)
{
  char buffer[DM_CRYPT_BUF_SIZE];
  struct dm_ioctl *io;
  struct dm_target_spec *tgt;
  char *crypt_params;
  char master_key_ascii[129]; /* Large enough to hold 512 bit key and null */
  int i;

  io = (struct dm_ioctl *) buffer;

  /* Load the mapping table for this device */
  tgt = (struct dm_target_spec *) &buffer[sizeof(struct dm_ioctl)];

  ioctl_init(io, DM_CRYPT_BUF_SIZE, name, 0);
  io->target_count = 1;
  tgt->status = 0;
  tgt->sector_start = 0;
  tgt->length = crypt_ftr->fs_size;
  strcpy(tgt->target_type, "crypt");

  crypt_params = buffer + sizeof(struct dm_ioctl) + sizeof(struct dm_target_spec);
  convert_key_to_hex_ascii(master_key, crypt_ftr->keysize, master_key_ascii);
  sprintf(crypt_params, "%s %s 0 %s 0 %s", crypt_ftr->crypto_type_name,
          master_key_ascii, real_blk_name, extra_params);
  crypt_params += strlen(crypt_params) + 1;
  crypt_params = (char *) (((unsigned long)crypt_params + 7) & ~8); /* Align to an 8 byte boundary */
  tgt->next = crypt_params - buffer;

  for (i = 0; i < TABLE_LOAD_RETRIES; i++) {
    if (! ioctl(fd, DM_TABLE_LOAD, io)) {
      break;
    }
    usleep(500000);
  }

  if (i == TABLE_LOAD_RETRIES) {
    /* We failed to load the table, return an error */
    return -1;
  } else {
    return i + 1;
  }
}


static int get_dm_crypt_version(int fd, const char *name,  int *version)
{
    char buffer[DM_CRYPT_BUF_SIZE];
    struct dm_ioctl *io;
    struct dm_target_versions *v;
    int i;

    io = (struct dm_ioctl *) buffer;

    ioctl_init(io, DM_CRYPT_BUF_SIZE, name, 0);

    if (ioctl(fd, DM_LIST_VERSIONS, io)) {
        return -1;
    }

    /* Iterate over the returned versions, looking for name of "crypt".
     * When found, get and return the version.
     */
    v = (struct dm_target_versions *) &buffer[sizeof(struct dm_ioctl)];
    while (v->next) {
        if (! strcmp(v->name, "crypt")) {
            /* We found the crypt driver, return the version, and get out */
            version[0] = v->version[0];
            version[1] = v->version[1];
            version[2] = v->version[2];
            return 0;
        }
        v = (struct dm_target_versions *)(((char *)v) + v->next);
    }

    return -1;
}

static int create_crypto_blk_dev(struct crypt_mnt_ftr *crypt_ftr, unsigned char *master_key,
                                    char *real_blk_name, char *crypto_blk_name, const char *name)
{
  char buffer[DM_CRYPT_BUF_SIZE];
  char master_key_ascii[129]; /* Large enough to hold 512 bit key and null */
  char *crypt_params;
  struct dm_ioctl *io;
  struct dm_target_spec *tgt;
  unsigned int minor;
  int fd;
  int i;
  int retval = -1;
  int version[3];
  char *extra_params;
  int load_count;

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

  extra_params = "";
  if (! get_dm_crypt_version(fd, name, version)) {
      /* Support for allow_discards was added in version 1.11.0 */
      if ((version[0] >= 2) ||
          ((version[0] == 1) && (version[1] >= 11))) {
          extra_params = "1 allow_discards";
          printf("Enabling support for allow_discards in dmcrypt.\n");
      }
  }

  load_count = load_crypto_mapping_table(crypt_ftr, master_key, real_blk_name, name,
                                         fd, extra_params);
  if (load_count < 0) {
      printf("Cannot load dm-crypt mapping table.\n");
      goto errout;
  } else if (load_count > 1) {
      printf("Took %d tries to load dmcrypt table.\n", load_count);
  }

  /* Resume this device to activate it */
  ioctl_init(io, DM_CRYPT_BUF_SIZE, name, 0);

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

static int delete_crypto_blk_dev(char *name)
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

static int pbkdf2(const char *passwd, const unsigned char *salt,
                  unsigned char *ikey, void *params UNUSED)
{
    printf("Using pbkdf2 for cryptfs KDF");

    /* Turn the password into a key and IV that can decrypt the master key */
    unsigned int keysize;
    char* master_key = (char*)convert_hex_ascii_to_key(passwd, &keysize);
    if (!master_key) return -1;
    PKCS5_PBKDF2_HMAC_SHA1(master_key, keysize, salt, SALT_LEN,
                           HASH_COUNT, KEY_LEN_BYTES+IV_LEN_BYTES, ikey);

    memset(master_key, 0, keysize);
    free (master_key);
    return 0;
}

static int scrypt(const char *passwd, const unsigned char *salt,
                  unsigned char *ikey, void *params)
{
    printf("Using scrypt for cryptfs KDF\n");

    struct crypt_mnt_ftr *ftr = (struct crypt_mnt_ftr *) params;

    int N = 1 << ftr->N_factor;
    int r = 1 << ftr->r_factor;
    int p = 1 << ftr->p_factor;

    /* Turn the password into a key and IV that can decrypt the master key */
    unsigned int keysize;
    unsigned char* master_key = convert_hex_ascii_to_key(passwd, &keysize);
    if (!master_key) return -1;
    crypto_scrypt(master_key, keysize, salt, SALT_LEN, N, r, p, ikey,
            KEY_LEN_BYTES + IV_LEN_BYTES);

    memset(master_key, 0, keysize);
    free (master_key);
    return 0;
}

static int scrypt_keymaster(const char *passwd, const unsigned char *salt,
                            unsigned char *ikey, void *params)
{
    printf("Using scrypt with keymaster for cryptfs KDF\n");

    int rc;
    unsigned int key_size;
    size_t signature_size;
    unsigned char* signature;
    struct crypt_mnt_ftr *ftr = (struct crypt_mnt_ftr *) params;

    int N = 1 << ftr->N_factor;
    int r = 1 << ftr->r_factor;
    int p = 1 << ftr->p_factor;

    unsigned char* master_key = convert_hex_ascii_to_key(passwd, &key_size);
    if (!master_key) {
        printf("Failed to convert passwd from hex\n");
        return -1;
    }

    rc = crypto_scrypt(master_key, key_size, salt, SALT_LEN,
                       N, r, p, ikey, KEY_LEN_BYTES + IV_LEN_BYTES);
    memset(master_key, 0, key_size);
    free(master_key);

    if (rc) {
        printf("scrypt failed\n");
        return -1;
    }

    if (keymaster_sign_object(ftr, ikey, KEY_LEN_BYTES + IV_LEN_BYTES,
                              &signature, &signature_size)) {
        printf("Signing failed\n");
        return -1;
    }

    rc = crypto_scrypt(signature, signature_size, salt, SALT_LEN,
                       N, r, p, ikey, KEY_LEN_BYTES + IV_LEN_BYTES);
    free(signature);

    if (rc) {
        printf("scrypt failed\n");
        return -1;
    }

    return 0;
}

static int encrypt_master_key(const char *passwd, const unsigned char *salt,
                              const unsigned char *decrypted_master_key,
                              unsigned char *encrypted_master_key,
                              struct crypt_mnt_ftr *crypt_ftr)
{
    unsigned char ikey[32+32] = { 0 }; /* Big enough to hold a 256 bit key and 256 bit IV */
    EVP_CIPHER_CTX e_ctx;
    int encrypted_len, final_len;
    int rc = 0;

    /* Turn the password into an intermediate key and IV that can decrypt the master key */
    get_device_scrypt_params(crypt_ftr);

    switch (crypt_ftr->kdf_type) {
    case KDF_SCRYPT_KEYMASTER_UNPADDED:
    case KDF_SCRYPT_KEYMASTER_BADLY_PADDED:
    case KDF_SCRYPT_KEYMASTER:
        if (keymaster_create_key(crypt_ftr)) {
            printf("keymaster_create_key failed");
            return -1;
        }

        if (scrypt_keymaster(passwd, salt, ikey, crypt_ftr)) {
            printf("scrypt failed");
            return -1;
        }
        break;

    case KDF_SCRYPT:
        if (scrypt(passwd, salt, ikey, crypt_ftr)) {
            printf("scrypt failed");
            return -1;
        }
        break;

    default:
        printf("Invalid kdf_type");
        return -1;
    }

    /* Initialize the decryption engine */
    if (! EVP_EncryptInit(&e_ctx, EVP_aes_128_cbc(), ikey, ikey+KEY_LEN_BYTES)) {
        printf("EVP_EncryptInit failed\n");
        return -1;
    }
    EVP_CIPHER_CTX_set_padding(&e_ctx, 0); /* Turn off padding as our data is block aligned */

    /* Encrypt the master key */
    if (! EVP_EncryptUpdate(&e_ctx, encrypted_master_key, &encrypted_len,
                              decrypted_master_key, KEY_LEN_BYTES)) {
        printf("EVP_EncryptUpdate failed\n");
        return -1;
    }
    if (! EVP_EncryptFinal(&e_ctx, encrypted_master_key + encrypted_len, &final_len)) {
        printf("EVP_EncryptFinal failed\n");
        return -1;
    }

    if (encrypted_len + final_len != KEY_LEN_BYTES) {
        printf("EVP_Encryption length check failed with %d, %d bytes\n", encrypted_len, final_len);
        return -1;
    }

    /* Store the scrypt of the intermediate key, so we can validate if it's a
       password error or mount error when things go wrong.
       Note there's no need to check for errors, since if this is incorrect, we
       simply won't wipe userdata, which is the correct default behavior
    */
    int N = 1 << crypt_ftr->N_factor;
    int r = 1 << crypt_ftr->r_factor;
    int p = 1 << crypt_ftr->p_factor;

    rc = crypto_scrypt(ikey, KEY_LEN_BYTES,
                       crypt_ftr->salt, sizeof(crypt_ftr->salt), N, r, p,
                       crypt_ftr->scrypted_intermediate_key,
                       sizeof(crypt_ftr->scrypted_intermediate_key));

    if (rc) {
      printf("encrypt_master_key: crypto_scrypt failed");
    }

    return 0;
}

static int decrypt_master_key_aux(char *passwd, unsigned char *salt,
                                  unsigned char *encrypted_master_key,
                                  unsigned char *decrypted_master_key,
                                  kdf_func kdf, void *kdf_params,
                                  unsigned char** intermediate_key,
                                  size_t* intermediate_key_size)
{
  unsigned char ikey[32+32] = { 0 }; /* Big enough to hold a 256 bit key and 256 bit IV */
  EVP_CIPHER_CTX d_ctx;
  int decrypted_len, final_len;

  /* Turn the password into an intermediate key and IV that can decrypt the
     master key */
  if (kdf(passwd, salt, ikey, kdf_params)) {
    printf("kdf failed");
    return -1;
  }

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
  }

  /* Copy intermediate key if needed by params */
  if (intermediate_key && intermediate_key_size) {
    *intermediate_key = (unsigned char*) malloc(KEY_LEN_BYTES);
    if (intermediate_key) {
      memcpy(*intermediate_key, ikey, KEY_LEN_BYTES);
      *intermediate_key_size = KEY_LEN_BYTES;
    }
  }

  return 0;
}

static void get_kdf_func(struct crypt_mnt_ftr *ftr, kdf_func *kdf, void** kdf_params)
{
    if (ftr->kdf_type == KDF_SCRYPT_KEYMASTER_UNPADDED ||
        ftr->kdf_type == KDF_SCRYPT_KEYMASTER_BADLY_PADDED ||
        ftr->kdf_type == KDF_SCRYPT_KEYMASTER) {
        *kdf = scrypt_keymaster;
        *kdf_params = ftr;
    } else if (ftr->kdf_type == KDF_SCRYPT) {
        *kdf = scrypt;
        *kdf_params = ftr;
    } else {
        *kdf = pbkdf2;
        *kdf_params = NULL;
    }
}

static int decrypt_master_key(char *passwd, unsigned char *decrypted_master_key,
                              struct crypt_mnt_ftr *crypt_ftr,
                              unsigned char** intermediate_key,
                              size_t* intermediate_key_size)
{
    kdf_func kdf;
    void *kdf_params;
    int ret;

    get_kdf_func(crypt_ftr, &kdf, &kdf_params);
    ret = decrypt_master_key_aux(passwd, crypt_ftr->salt, crypt_ftr->master_key,
                                 decrypted_master_key, kdf, kdf_params,
                                 intermediate_key, intermediate_key_size);
    if (ret != 0) {
        printf("failure decrypting master key");
    }

    return ret;
}

static int create_encrypted_random_key(char *passwd, unsigned char *master_key, unsigned char *salt,
        struct crypt_mnt_ftr *crypt_ftr) {
    int fd;
    unsigned char key_buf[KEY_LEN_BYTES];
    EVP_CIPHER_CTX e_ctx;
    int encrypted_len, final_len;

    /* Get some random bits for a key */
    fd = open("/dev/urandom", O_RDONLY);
    read(fd, key_buf, sizeof(key_buf));
    read(fd, salt, SALT_LEN);
    close(fd);

    /* Now encrypt it with the password */
    return encrypt_master_key(passwd, salt, key_buf, master_key, crypt_ftr);
}

static int wait_and_unmount(char *mountpoint, bool kill)
{
    int i, err, rc;
#define WAIT_UNMOUNT_COUNT 20

    /*  Now umount the tmpfs filesystem */
    for (i=0; i<WAIT_UNMOUNT_COUNT; i++) {
        if (umount(mountpoint) == 0) {
            break;
        }

        if (errno == EINVAL) {
            /* EINVAL is returned if the directory is not a mountpoint,
             * i.e. there is no filesystem mounted there.  So just get out.
             */
            break;
        }

        err = errno;

        /* If allowed, be increasingly aggressive before the last two retries */
        if (kill) {
            if (i == (WAIT_UNMOUNT_COUNT - 3)) {
                printf("sending SIGHUP to processes with open files\n");
                //vold_killProcessesWithOpenFiles(mountpoint, 1);
            } else if (i == (WAIT_UNMOUNT_COUNT - 2)) {
                printf("sending SIGKILL to processes with open files\n");
                //vold_killProcessesWithOpenFiles(mountpoint, 2);
            }
        }

        sleep(1);
    }

    if (i < WAIT_UNMOUNT_COUNT) {
      printf("unmounting %s succeeded\n", mountpoint);
      rc = 0;
    } else {
      //vold_killProcessesWithOpenFiles(mountpoint, 0);
      printf("unmounting %s failed: %s\n", mountpoint, strerror(err));
      rc = -1;
    }

    return rc;
}

#define DATA_PREP_TIMEOUT 200
static int prep_data_fs(void)
{
    int i;

    /* Do the prep of the /data filesystem */
    property_set("vold.post_fs_data_done", "0");
    property_set("vold.decrypt", "trigger_post_fs_data");
    printf("Just triggered post_fs_data\n");

    /* Wait a max of 50 seconds, hopefully it takes much less */
    for (i=0; i<DATA_PREP_TIMEOUT; i++) {
        char p[PROPERTY_VALUE_MAX];

        property_get("vold.post_fs_data_done", p, "0");
        if (*p == '1') {
            break;
        } else {
            usleep(250000);
        }
    }
    if (i == DATA_PREP_TIMEOUT) {
        /* Ugh, we failed to prep /data in time.  Bail. */
        printf("post_fs_data timed out!\n");
        return -1;
    } else {
        printf("post_fs_data done\n");
        return 0;
    }
}

static void cryptfs_set_corrupt()
{
    // Mark the footer as bad
    struct crypt_mnt_ftr crypt_ftr;
    if (get_crypt_ftr_and_key(&crypt_ftr)) {
        printf("Failed to get crypto footer - panic");
        return;
    }

    crypt_ftr.flags |= CRYPT_DATA_CORRUPT;
    if (put_crypt_ftr_and_key(&crypt_ftr)) {
        printf("Failed to set crypto footer - panic");
        return;
    }
}

static void cryptfs_trigger_restart_min_framework()
{
    if (fs_mgr_do_tmpfs_mount(DATA_MNT_POINT)) {
      printf("Failed to mount tmpfs on data - panic");
      return;
    }

    if (property_set("vold.decrypt", "trigger_post_fs_data")) {
        printf("Failed to trigger post fs data - panic");
        return;
    }

    if (property_set("vold.decrypt", "trigger_restart_min_framework")) {
        printf("Failed to trigger restart min framework - panic");
        return;
    }
}

/* returns < 0 on failure */
static int cryptfs_restart_internal(int restart_main)
{
    char fs_type[32];
    char real_blkdev[MAXPATHLEN];
    char crypto_blkdev[MAXPATHLEN];
    char fs_options[256];
    unsigned long mnt_flags;
    struct stat statbuf;
    int rc = -1, i;
    static int restart_successful = 0;

    /* Validate that it's OK to call this routine */
    if (! master_key_saved) {
        printf("Encrypted filesystem not validated, aborting");
        return -1;
    }

    if (restart_successful) {
        printf("System already restarted with encrypted disk, aborting");
        return -1;
    }

    if (restart_main) {
        /* Here is where we shut down the framework.  The init scripts
         * start all services in one of three classes: core, main or late_start.
         * On boot, we start core and main.  Now, we stop main, but not core,
         * as core includes vold and a few other really important things that
         * we need to keep running.  Once main has stopped, we should be able
         * to umount the tmpfs /data, then mount the encrypted /data.
         * We then restart the class main, and also the class late_start.
         * At the moment, I've only put a few things in late_start that I know
         * are not needed to bring up the framework, and that also cause problems
         * with unmounting the tmpfs /data, but I hope to add add more services
         * to the late_start class as we optimize this to decrease the delay
         * till the user is asked for the password to the filesystem.
         */

        /* The init files are setup to stop the class main when vold.decrypt is
         * set to trigger_reset_main.
         */
        property_set("vold.decrypt", "trigger_reset_main");
        printf("Just asked init to shut down class main\n");

        /* Ugh, shutting down the framework is not synchronous, so until it
         * can be fixed, this horrible hack will wait a moment for it all to
         * shut down before proceeding.  Without it, some devices cannot
         * restart the graphics services.
         */
        sleep(2);
    }

    /* Now that the framework is shutdown, we should be able to umount()
     * the tmpfs filesystem, and mount the real one.
     */

    property_get("ro.crypto.fs_crypto_blkdev", crypto_blkdev, "");
    if (strlen(crypto_blkdev) == 0) {
        printf("fs_crypto_blkdev not set\n");
        return -1;
    }

    if (! (rc = wait_and_unmount(DATA_MNT_POINT, true)) ) {
        /* If ro.crypto.readonly is set to 1, mount the decrypted
         * filesystem readonly.  This is used when /data is mounted by
         * recovery mode.
         */
        char ro_prop[PROPERTY_VALUE_MAX];
        property_get("ro.crypto.readonly", ro_prop, "");
        if (strlen(ro_prop) > 0 && atoi(ro_prop)) {
            struct fstab_rec* rec = fs_mgr_get_entry_for_mount_point(fstab, DATA_MNT_POINT);
            rec->flags |= MS_RDONLY;
        }

        /* If that succeeded, then mount the decrypted filesystem */
        int retries = RETRY_MOUNT_ATTEMPTS;
        int mount_rc;
        while ((mount_rc = fs_mgr_do_mount(fstab, DATA_MNT_POINT,
                                           crypto_blkdev, 0))
               != 0) {
            if (mount_rc == FS_MGR_DOMNT_BUSY) {
                /* TODO: invoke something similar to
                   Process::killProcessWithOpenFiles(DATA_MNT_POINT,
                                   retries > RETRY_MOUNT_ATTEMPT/2 ? 1 : 2 ) */
                printf("Failed to mount %s because it is busy - waiting",
                      crypto_blkdev);
                if (--retries) {
                    sleep(RETRY_MOUNT_DELAY_SECONDS);
                } else {
                    /* Let's hope that a reboot clears away whatever is keeping
                       the mount busy */
                    cryptfs_reboot(reboot);
                }
            } else {
                printf("Failed to mount decrypted data");
                cryptfs_set_corrupt();
                cryptfs_trigger_restart_min_framework();
                printf("Started framework to offer wipe");
                return -1;
            }
        }

        property_set("vold.decrypt", "trigger_load_persist_props");
        /* Create necessary paths on /data */
        if (prep_data_fs()) {
            return -1;
        }

        /* startup service classes main and late_start */
        property_set("vold.decrypt", "trigger_restart_framework");
        printf("Just triggered restart_framework\n");

        /* Give it a few moments to get started */
        sleep(1);
    }

    if (rc == 0) {
        restart_successful = 1;
    }

    return rc;
}

int cryptfs_restart(void)
{
    /* Call internal implementation forcing a restart of main service group */
    return cryptfs_restart_internal(1);
}

static int do_crypto_complete(char *mount_point UNUSED)
{
  struct crypt_mnt_ftr crypt_ftr;
  char encrypted_state[PROPERTY_VALUE_MAX];
  char key_loc[PROPERTY_VALUE_MAX];

  property_get("ro.crypto.state", encrypted_state, "");
  if (strcmp(encrypted_state, "encrypted") ) {
    printf("not running with encryption, aborting");
    return CRYPTO_COMPLETE_NOT_ENCRYPTED;
  }

  if (get_crypt_ftr_and_key(&crypt_ftr)) {
    fs_mgr_get_crypt_info(fstab, key_loc, 0, sizeof(key_loc));

    /*
     * Only report this error if key_loc is a file and it exists.
     * If the device was never encrypted, and /data is not mountable for
     * some reason, returning 1 should prevent the UI from presenting the
     * a "enter password" screen, or worse, a "press button to wipe the
     * device" screen.
     */
    if ((key_loc[0] == '/') && (access("key_loc", F_OK) == -1)) {
      printf("master key file does not exist, aborting");
      return CRYPTO_COMPLETE_NOT_ENCRYPTED;
    } else {
      printf("Error getting crypt footer and key\n");
      return CRYPTO_COMPLETE_BAD_METADATA;
    }
  }

  // Test for possible error flags
  if (crypt_ftr.flags & CRYPT_ENCRYPTION_IN_PROGRESS){
    printf("Encryption process is partway completed\n");
    return CRYPTO_COMPLETE_PARTIAL;
  }

  if (crypt_ftr.flags & CRYPT_INCONSISTENT_STATE){
    printf("Encryption process was interrupted but cannot continue\n");
    return CRYPTO_COMPLETE_INCONSISTENT;
  }

  if (crypt_ftr.flags & CRYPT_DATA_CORRUPT){
    printf("Encryption is successful but data is corrupt\n");
    return CRYPTO_COMPLETE_CORRUPT;
  }

  /* We passed the test! We shall diminish, and return to the west */
  return CRYPTO_COMPLETE_ENCRYPTED;
}

static int test_mount_encrypted_fs(struct crypt_mnt_ftr* crypt_ftr,
                                   char *passwd, char *mount_point, char *label)
{
  /* Allocate enough space for a 256 bit key, but we may use less */
  unsigned char decrypted_master_key[32];
  char crypto_blkdev[MAXPATHLEN];
  char real_blkdev[MAXPATHLEN];
  char tmp_mount_point[64];
  unsigned int orig_failed_decrypt_count;
  int rc;
  kdf_func kdf;
  void *kdf_params;
  int use_keymaster = 0;
  int upgrade = 0;
  unsigned char* intermediate_key = 0;
  size_t intermediate_key_size = 0;

  printf("crypt_ftr->fs_size = %lld\n", crypt_ftr->fs_size);
  orig_failed_decrypt_count = crypt_ftr->failed_decrypt_count;

  if (! (crypt_ftr->flags & CRYPT_MNT_KEY_UNENCRYPTED) ) {
    if (decrypt_master_key(passwd, decrypted_master_key, crypt_ftr,
                           &intermediate_key, &intermediate_key_size)) {
      printf("Failed to decrypt master key\n");
      rc = -1;
      goto errout;
    }
  }

  fs_mgr_get_crypt_info(fstab, 0, real_blkdev, sizeof(real_blkdev));

  // Create crypto block device - all (non fatal) code paths
  // need it
  if (create_crypto_blk_dev(crypt_ftr, decrypted_master_key,
                            real_blkdev, crypto_blkdev, label)) {
     printf("Error creating decrypted block device\n");
     rc = -1;
     goto errout;
  }

  /* Work out if the problem is the password or the data */
  unsigned char scrypted_intermediate_key[sizeof(crypt_ftr->
                                                 scrypted_intermediate_key)];
  int N = 1 << crypt_ftr->N_factor;
  int r = 1 << crypt_ftr->r_factor;
  int p = 1 << crypt_ftr->p_factor;

  rc = crypto_scrypt(intermediate_key, intermediate_key_size,
                     crypt_ftr->salt, sizeof(crypt_ftr->salt),
                     N, r, p, scrypted_intermediate_key,
                     sizeof(scrypted_intermediate_key));

  // Does the key match the crypto footer?
  if (rc == 0 && memcmp(scrypted_intermediate_key,
                        crypt_ftr->scrypted_intermediate_key,
                        sizeof(scrypted_intermediate_key)) == 0) {
    printf("Password matches\n");
    rc = 0;
  } else {
    /* Try mounting the file system anyway, just in case the problem's with
     * the footer, not the key. */
    sprintf(tmp_mount_point, "%s/tmp_mnt", mount_point);
    mkdir(tmp_mount_point, 0755);
    if (fs_mgr_do_mount(fstab, DATA_MNT_POINT, crypto_blkdev, tmp_mount_point)) {
      printf("Error temp mounting decrypted block device '%s'\n", crypto_blkdev);
      delete_crypto_blk_dev(label);

      rc = ++crypt_ftr->failed_decrypt_count;
      //put_crypt_ftr_and_key(crypt_ftr); // Do not penalize for attempting to decrypt in recovery
    } else {
      /* Success! */
      printf("Password did not match but decrypted drive mounted - continue\n");
      umount(tmp_mount_point);
      rc = 0;
    }
  }

  if (rc == 0) {
    /*crypt_ftr->failed_decrypt_count = 0;
    if (orig_failed_decrypt_count != 0) {
      put_crypt_ftr_and_key(crypt_ftr);
    }*/

    /* Save the name of the crypto block device
     * so we can mount it when restarting the framework. */
    property_set("ro.crypto.fs_crypto_blkdev", crypto_blkdev);

    /* Also save a the master key so we can reencrypted the key
     * the key when we want to change the password on it. */
    /*memcpy(saved_master_key, decrypted_master_key, KEY_LEN_BYTES);
    saved_mount_point = strdup(mount_point);
    master_key_saved = 1;
    printf("%s(): Master key saved\n", __FUNCTION__);*/
    rc = 0;

    // Upgrade if we're not using the latest KDF.
    /*use_keymaster = keymaster_check_compatibility();
    if (crypt_ftr->kdf_type == KDF_SCRYPT_KEYMASTER) {
        // Don't allow downgrade
    } else if (use_keymaster == 1 && crypt_ftr->kdf_type != KDF_SCRYPT_KEYMASTER) {
        crypt_ftr->kdf_type = KDF_SCRYPT_KEYMASTER;
        upgrade = 1;
    } else if (use_keymaster == 0 && crypt_ftr->kdf_type != KDF_SCRYPT) {
        crypt_ftr->kdf_type = KDF_SCRYPT;
        upgrade = 1;
    }

    if (upgrade) {
        rc = encrypt_master_key(passwd, crypt_ftr->salt, saved_master_key,
                                crypt_ftr->master_key, crypt_ftr);
        if (!rc) {
            rc = put_crypt_ftr_and_key(crypt_ftr);
        }
        printf("Key Derivation Function upgrade: rc=%d\n", rc);

        // Do not fail even if upgrade failed - machine is bootable
        // Note that if this code is ever hit, there is a *serious* problem
        // since KDFs should never fail. You *must* fix the kdf before
        // proceeding!
        if (rc) {
          printf("Upgrade failed with error %d,"
                " but continuing with previous state\n",
                rc);
          rc = 0;
        }
    }*/
  }

 errout:
  if (intermediate_key) {
    memset(intermediate_key, 0, intermediate_key_size);
    free(intermediate_key);
  }
  return rc;
}

/* Called by vold when it wants to undo the crypto mapping of a volume it
 * manages.  This is usually in response to a factory reset, when we want
 * to undo the crypto mapping so the volume is formatted in the clear.
 */
int cryptfs_revert_volume(const char *label)
{
    return delete_crypto_blk_dev((char *)label);
}

/*
 * Called by vold when it's asked to mount an encrypted, nonremovable volume.
 * Setup a dm-crypt mapping, use the saved master key from
 * setting up the /data mapping, and return the new device path.
 */
int cryptfs_setup_volume(const char *label, int major, int minor,
                         char *crypto_sys_path, unsigned int max_path,
                         int *new_major, int *new_minor)
{
    char real_blkdev[MAXPATHLEN], crypto_blkdev[MAXPATHLEN];
    struct crypt_mnt_ftr sd_crypt_ftr;
    struct stat statbuf;
    int nr_sec, fd;

    sprintf(real_blkdev, "/dev/block/vold/%d:%d", major, minor);

    get_crypt_ftr_and_key(&sd_crypt_ftr);

    /* Update the fs_size field to be the size of the volume */
    fd = open(real_blkdev, O_RDONLY);
    nr_sec = get_blkdev_size(fd);
    close(fd);
    if (nr_sec == 0) {
        printf("Cannot get size of volume %s\n", real_blkdev);
        return -1;
    }

    sd_crypt_ftr.fs_size = nr_sec;
    create_crypto_blk_dev(&sd_crypt_ftr, saved_master_key, real_blkdev, 
                          crypto_blkdev, label);

    stat(crypto_blkdev, &statbuf);
    *new_major = MAJOR(statbuf.st_rdev);
    *new_minor = MINOR(statbuf.st_rdev);

    /* Create path to sys entry for this block device */
    snprintf(crypto_sys_path, max_path, "/devices/virtual/block/%s", strrchr(crypto_blkdev, '/')+1);

    return 0;
}

int cryptfs_crypto_complete(void)
{
  return do_crypto_complete("/data");
}

int check_unmounted_and_get_ftr(struct crypt_mnt_ftr* crypt_ftr)
{
    char encrypted_state[PROPERTY_VALUE_MAX];
    property_get("ro.crypto.state", encrypted_state, "");
    if ( master_key_saved || strcmp(encrypted_state, "encrypted") ) {
        printf("encrypted fs already validated or not running with encryption,"
              " aborting\n");
        //return -1;
    }

    if (get_crypt_ftr_and_key(crypt_ftr)) {
        printf("Error getting crypt footer and key\n");
        return -1;
    }

    return 0;
}

/*
 * TODO - transition patterns to new format in calling code
 *        and remove this vile hack, and the use of hex in
 *        the password passing code.
 *
 * Patterns are passed in zero based (i.e. the top left dot
 * is represented by zero, the top middle one etc), but we want
 * to store them '1' based.
 * This is to allow us to migrate the calling code to use this
 * convention. It also solves a nasty problem whereby scrypt ignores
 * trailing zeros, so patterns ending at the top left could be
 * truncated, and similarly, you could add the top left to any
 * pattern and still match.
 * adjust_passwd is a hack function that returns the alternate representation
 * if the password appears to be a pattern (hex numbers all less than 09)
 * If it succeeds we need to try both, and in particular try the alternate
 * first. If the original matches, then we need to update the footer
 * with the alternate.
 * All code that accepts passwords must adjust them first. Since
 * cryptfs_check_passwd is always the first function called after a migration
 * (and indeed on any boot) we only need to do the double try in this
 * function.
 */
char* adjust_passwd(const char* passwd)
{
    size_t index, length;

    if (!passwd) {
        return 0;
    }

    // Check even length. Hex encoded passwords are always
    // an even length, since each character encodes to two characters.
    length = strlen(passwd);
    if (length % 2) {
        printf("Password not correctly hex encoded.");
        return 0;
    }

    // Check password is old-style pattern - a collection of hex
    // encoded bytes less than 9 (00 through 08)
    for (index = 0; index < length; index +=2) {
        if (passwd[index] != '0'
            || passwd[index + 1] < '0' || passwd[index + 1] > '8') {
            return 0;
        }
    }

    // Allocate room for adjusted passwd and null terminate
    char* adjusted = malloc(length + 1);
    adjusted[length] = 0;

    // Add 0x31 ('1') to each character
    for (index = 0; index < length; index += 2) {
        // output is 31 through 39 so set first byte to three, second to src + 1
        adjusted[index] = '3';
        adjusted[index + 1] = passwd[index + 1] + 1;
    }

    return adjusted;
}

/*
 * Passwords in L get passed from Android to cryptfs in hex, so a '1'
 * gets converted to '31' where 31 is 0x31 which is the ascii character
 * code in hex of the character '1'. This function will convert the
 * regular character codes to their hexadecimal representation to make
 * decrypt work properly with Android 5.0 lollipop decryption.
 */
char* hexadj_passwd(const char* passwd)
{
    size_t index, length;
    char* ptr = passwd;

    if (!passwd) {
        return 0;
    }

    length = strlen(passwd);

    // Allocate room for hex passwd and null terminate
    char* hex = malloc((length * 2) + 1);
    hex[length * 2] = 0;

    // Convert to hex
    for (index = 0; index < length; index++) {
        sprintf(hex + (index * 2), "%02X", *ptr);
        ptr++;
    }

    return hex;
}

#define FSTAB_PREFIX "/fstab."

int cryptfs_check_footer(void)
{
    int rc = -1;
    char fstab_filename[PROPERTY_VALUE_MAX + sizeof(FSTAB_PREFIX)];
    char propbuf[PROPERTY_VALUE_MAX];
    struct crypt_mnt_ftr crypt_ftr;

    property_get("ro.hardware", propbuf, "");
    snprintf(fstab_filename, sizeof(fstab_filename), FSTAB_PREFIX"%s", propbuf);

    fstab = fs_mgr_read_fstab(fstab_filename);
    if (!fstab) {
        printf("failed to open %s\n", fstab_filename);
        return -1;
    }

    rc = get_crypt_ftr_and_key(&crypt_ftr);

    return rc;
}

int cryptfs_check_passwd(char *passwd)
{
    struct crypt_mnt_ftr crypt_ftr;
    int rc;
    char fstab_filename[PROPERTY_VALUE_MAX + sizeof(FSTAB_PREFIX)];
    char propbuf[PROPERTY_VALUE_MAX];

    property_get("ro.hardware", propbuf, "");
    snprintf(fstab_filename, sizeof(fstab_filename), FSTAB_PREFIX"%s", propbuf);

    fstab = fs_mgr_read_fstab(fstab_filename);
    if (!fstab) {
        printf("failed to open %s\n", fstab_filename);
        return -1;
    }

    rc = check_unmounted_and_get_ftr(&crypt_ftr);
    if (rc)
        return rc;

    char* adjusted_passwd = adjust_passwd(passwd);
    char* hex_passwd = hexadj_passwd(passwd);

    if (adjusted_passwd) {
        int failed_decrypt_count = crypt_ftr.failed_decrypt_count;
        rc = test_mount_encrypted_fs(&crypt_ftr, adjusted_passwd,
                                     DATA_MNT_POINT, "userdata");

        // Maybe the original one still works?
        if (rc) {
            // Don't double count this failure
            crypt_ftr.failed_decrypt_count = failed_decrypt_count;
            rc = test_mount_encrypted_fs(&crypt_ftr, passwd,
                                         DATA_MNT_POINT, "userdata");
            if (!rc) {
                // cryptfs_changepw also adjusts so pass original
                // Note that adjust_passwd only recognises patterns
                // so we can safely use CRYPT_TYPE_PATTERN
                printf("TWRP NOT Updating pattern to new format");
                //cryptfs_changepw(CRYPT_TYPE_PATTERN, passwd);
            } else if (hex_passwd) {
                rc = test_mount_encrypted_fs(&crypt_ftr, hex_passwd,
                                         DATA_MNT_POINT, "userdata");
            }
        }
        free(adjusted_passwd);
    } else {
        rc = test_mount_encrypted_fs(&crypt_ftr, passwd,
                                     DATA_MNT_POINT, "userdata");
        if (rc && hex_passwd) {
            rc = test_mount_encrypted_fs(&crypt_ftr, hex_passwd,
                                     DATA_MNT_POINT, "userdata");
        }
    }

    if (hex_passwd)
        free(hex_passwd);

    /*if (rc == 0 && crypt_ftr.crypt_type != CRYPT_TYPE_DEFAULT) {
		printf("cryptfs_check_passwd update expiry time?\n");
        cryptfs_clear_password();
        password = strdup(passwd);
        struct timespec now;
        clock_gettime(CLOCK_BOOTTIME, &now);
        password_expiry_time = now.tv_sec + password_max_age_seconds;
    }*/

    return rc;
}

int cryptfs_verify_passwd(char *passwd)
{
    struct crypt_mnt_ftr crypt_ftr;
    /* Allocate enough space for a 256 bit key, but we may use less */
    unsigned char decrypted_master_key[32];
    char encrypted_state[PROPERTY_VALUE_MAX];
    int rc;

    property_get("ro.crypto.state", encrypted_state, "");
    if (strcmp(encrypted_state, "encrypted") ) {
        printf("device not encrypted, aborting");
        return -2;
    }

    if (!master_key_saved) {
        printf("encrypted fs not yet mounted, aborting");
        return -1;
    }

    if (!saved_mount_point) {
        printf("encrypted fs failed to save mount point, aborting");
        return -1;
    }

    if (get_crypt_ftr_and_key(&crypt_ftr)) {
        printf("Error getting crypt footer and key\n");
        return -1;
    }

    if (crypt_ftr.flags & CRYPT_MNT_KEY_UNENCRYPTED) {
        /* If the device has no password, then just say the password is valid */
        rc = 0;
    } else {
        char* adjusted_passwd = adjust_passwd(passwd);
        if (adjusted_passwd) {
            passwd = adjusted_passwd;
        }

        decrypt_master_key(passwd, decrypted_master_key, &crypt_ftr, 0, 0);
        if (!memcmp(decrypted_master_key, saved_master_key, crypt_ftr.keysize)) {
            /* They match, the password is correct */
            rc = 0;
        } else {
            /* If incorrect, sleep for a bit to prevent dictionary attacks */
            sleep(1);
            rc = 1;
        }

        free(adjusted_passwd);
    }

    return rc;
}

/* Initialize a crypt_mnt_ftr structure.  The keysize is
 * defaulted to 16 bytes, and the filesystem size to 0.
 * Presumably, at a minimum, the caller will update the
 * filesystem size and crypto_type_name after calling this function.
 */
static int cryptfs_init_crypt_mnt_ftr(struct crypt_mnt_ftr *ftr)
{
    off64_t off;

    memset(ftr, 0, sizeof(struct crypt_mnt_ftr));
    ftr->magic = CRYPT_MNT_MAGIC;
    ftr->major_version = CURRENT_MAJOR_VERSION;
    ftr->minor_version = CURRENT_MINOR_VERSION;
    ftr->ftr_size = sizeof(struct crypt_mnt_ftr);
    ftr->keysize = KEY_LEN_BYTES;

    switch (keymaster_check_compatibility()) {
    case 1:
        ftr->kdf_type = KDF_SCRYPT_KEYMASTER;
        break;

    case 0:
        ftr->kdf_type = KDF_SCRYPT;
        break;

    default:
        printf("keymaster_check_compatibility failed");
        return -1;
    }

    get_device_scrypt_params(ftr);

    ftr->persist_data_size = CRYPT_PERSIST_DATA_SIZE;
    if (get_crypt_ftr_info(NULL, &off) == 0) {
        ftr->persist_data_offset[0] = off + CRYPT_FOOTER_TO_PERSIST_OFFSET;
        ftr->persist_data_offset[1] = off + CRYPT_FOOTER_TO_PERSIST_OFFSET +
                                    ftr->persist_data_size;
    }

    return 0;
}

static int cryptfs_enable_wipe(char *crypto_blkdev, off64_t size, int type)
{
    const char *args[10];
    char size_str[32]; /* Must be large enough to hold a %lld and null byte */
    int num_args;
    int status;
    int tmp;
    int rc = -1;

    if (type == EXT4_FS) {
        args[0] = "/system/bin/make_ext4fs";
        args[1] = "-a";
        args[2] = "/data";
        args[3] = "-l";
        snprintf(size_str, sizeof(size_str), "%" PRId64, size * 512);
        args[4] = size_str;
        args[5] = crypto_blkdev;
        num_args = 6;
        printf("Making empty filesystem with command %s %s %s %s %s %s\n",
              args[0], args[1], args[2], args[3], args[4], args[5]);
    } else if (type == F2FS_FS) {
        args[0] = "/system/bin/mkfs.f2fs";
        args[1] = "-t";
        args[2] = "-d1";
        args[3] = crypto_blkdev;
        snprintf(size_str, sizeof(size_str), "%" PRId64, size);
        args[4] = size_str;
        num_args = 5;
        printf("Making empty filesystem with command %s %s %s %s %s\n",
              args[0], args[1], args[2], args[3], args[4]);
    } else {
        printf("cryptfs_enable_wipe(): unknown filesystem type %d\n", type);
        return -1;
    }

    tmp = android_fork_execvp(num_args, (char **)args, &status, false, true);

    if (tmp != 0) {
      printf("Error creating empty filesystem on %s due to logwrap error\n", crypto_blkdev);
    } else {
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status)) {
                printf("Error creating filesystem on %s, exit status %d ",
                      crypto_blkdev, WEXITSTATUS(status));
            } else {
                printf("Successfully created filesystem on %s\n", crypto_blkdev);
                rc = 0;
            }
        } else {
            printf("Error creating filesystem on %s, did not exit normally\n", crypto_blkdev);
       }
    }

    return rc;
}

#define CRYPT_INPLACE_BUFSIZE 4096
#define CRYPT_SECTORS_PER_BUFSIZE (CRYPT_INPLACE_BUFSIZE / CRYPT_SECTOR_SIZE)
#define CRYPT_SECTOR_SIZE 512

/* aligned 32K writes tends to make flash happy.
 * SD card association recommends it.
 */
#define BLOCKS_AT_A_TIME 8

struct encryptGroupsData
{
    int realfd;
    int cryptofd;
    off64_t numblocks;
    off64_t one_pct, cur_pct, new_pct;
    off64_t blocks_already_done, tot_numblocks;
    off64_t used_blocks_already_done, tot_used_blocks;
    char* real_blkdev, * crypto_blkdev;
    int count;
    off64_t offset;
    char* buffer;
    off64_t last_written_sector;
    int completed;
    time_t time_started;
    int remaining_time;
};

static void update_progress(struct encryptGroupsData* data, int is_used)
{
    data->blocks_already_done++;

    if (is_used) {
        data->used_blocks_already_done++;
    }
    if (data->tot_used_blocks) {
        data->new_pct = data->used_blocks_already_done / data->one_pct;
    } else {
        data->new_pct = data->blocks_already_done / data->one_pct;
    }

    if (data->new_pct > data->cur_pct) {
        char buf[8];
        data->cur_pct = data->new_pct;
        snprintf(buf, sizeof(buf), "%" PRId64, data->cur_pct);
        property_set("vold.encrypt_progress", buf);
    }

    if (data->cur_pct >= 5) {
        struct timespec time_now;
        if (clock_gettime(CLOCK_MONOTONIC, &time_now)) {
            printf("Error getting time");
        } else {
            double elapsed_time = difftime(time_now.tv_sec, data->time_started);
            off64_t remaining_blocks = data->tot_used_blocks
                                       - data->used_blocks_already_done;
            int remaining_time = (int)(elapsed_time * remaining_blocks
                                       / data->used_blocks_already_done);

            // Change time only if not yet set, lower, or a lot higher for
            // best user experience
            if (data->remaining_time == -1
                || remaining_time < data->remaining_time
                || remaining_time > data->remaining_time + 60) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", remaining_time);
                property_set("vold.encrypt_time_remaining", buf);
                data->remaining_time = remaining_time;
            }
        }
    }
}

static void log_progress(struct encryptGroupsData const* data, bool completed)
{
    // Precondition - if completed data = 0 else data != 0

    // Track progress so we can skip logging blocks
    static off64_t offset = -1;

    // Need to close existing 'Encrypting from' log?
    if (completed || (offset != -1 && data->offset != offset)) {
        printf("Encrypted to sector %" PRId64,
              offset / info.block_size * CRYPT_SECTOR_SIZE);
        offset = -1;
    }

    // Need to start new 'Encrypting from' log?
    if (!completed && offset != data->offset) {
        printf("Encrypting from sector %" PRId64,
              data->offset / info.block_size * CRYPT_SECTOR_SIZE);
    }

    // Update offset
    if (!completed) {
        offset = data->offset + (off64_t)data->count * info.block_size;
    }
}

static int flush_outstanding_data(struct encryptGroupsData* data)
{
    if (data->count == 0) {
        return 0;
    }

    printf("Copying %d blocks at offset %" PRIx64, data->count, data->offset);

    if (pread64(data->realfd, data->buffer,
                info.block_size * data->count, data->offset)
        <= 0) {
        printf("Error reading real_blkdev %s for inplace encrypt",
              data->real_blkdev);
        return -1;
    }

    if (pwrite64(data->cryptofd, data->buffer,
                 info.block_size * data->count, data->offset)
        <= 0) {
        printf("Error writing crypto_blkdev %s for inplace encrypt",
              data->crypto_blkdev);
        return -1;
    } else {
      log_progress(data, false);
    }

    data->count = 0;
    data->last_written_sector = (data->offset + data->count)
                                / info.block_size * CRYPT_SECTOR_SIZE - 1;
    return 0;
}

static int encrypt_groups(struct encryptGroupsData* data)
{
    unsigned int i;
    u8 *block_bitmap = 0;
    unsigned int block;
    off64_t ret;
    int rc = -1;

    data->buffer = malloc(info.block_size * BLOCKS_AT_A_TIME);
    if (!data->buffer) {
        printf("Failed to allocate crypto buffer");
        goto errout;
    }

    block_bitmap = malloc(info.block_size);
    if (!block_bitmap) {
        printf("failed to allocate block bitmap");
        goto errout;
    }

    for (i = 0; i < aux_info.groups; ++i) {
        printf("Encrypting group %d", i);

        u32 first_block = aux_info.first_data_block + i * info.blocks_per_group;
        u32 block_count = min(info.blocks_per_group,
                             aux_info.len_blocks - first_block);

        off64_t offset = (u64)info.block_size
                         * aux_info.bg_desc[i].bg_block_bitmap;

        ret = pread64(data->realfd, block_bitmap, info.block_size, offset);
        if (ret != (int)info.block_size) {
            printf("failed to read all of block group bitmap %d", i);
            goto errout;
        }

        offset = (u64)info.block_size * first_block;

        data->count = 0;

        for (block = 0; block < block_count; block++) {
            int used = bitmap_get_bit(block_bitmap, block);
            update_progress(data, used);
            if (used) {
                if (data->count == 0) {
                    data->offset = offset;
                }
                data->count++;
            } else {
                if (flush_outstanding_data(data)) {
                    goto errout;
                }
            }

            offset += info.block_size;

            /* Write data if we are aligned or buffer size reached */
            if (offset % (info.block_size * BLOCKS_AT_A_TIME) == 0
                || data->count == BLOCKS_AT_A_TIME) {
                if (flush_outstanding_data(data)) {
                    goto errout;
                }
            }

            if (1) {
                printf("Stopping encryption due to low battery");
                rc = 0;
                goto errout;
            }

        }
        if (flush_outstanding_data(data)) {
            goto errout;
        }
    }

    data->completed = 1;
    rc = 0;

errout:
    log_progress(0, true);
    free(data->buffer);
    free(block_bitmap);
    return rc;
}

static int cryptfs_enable_inplace_ext4(char *crypto_blkdev,
                                       char *real_blkdev,
                                       off64_t size,
                                       off64_t *size_already_done,
                                       off64_t tot_size,
                                       off64_t previously_encrypted_upto)
{
    u32 i;
    struct encryptGroupsData data;
    int rc; // Can't initialize without causing warning -Wclobbered

    if (previously_encrypted_upto > *size_already_done) {
        printf("Not fast encrypting since resuming part way through");
        return -1;
    }

    memset(&data, 0, sizeof(data));
    data.real_blkdev = real_blkdev;
    data.crypto_blkdev = crypto_blkdev;

    if ( (data.realfd = open(real_blkdev, O_RDWR)) < 0) {
        printf("Error opening real_blkdev %s for inplace encrypt. err=%d(%s)\n",
              real_blkdev, errno, strerror(errno));
        rc = -1;
        goto errout;
    }

    if ( (data.cryptofd = open(crypto_blkdev, O_WRONLY)) < 0) {
        printf("Error opening crypto_blkdev %s for ext4 inplace encrypt. err=%d(%s)\n",
              crypto_blkdev, errno, strerror(errno));
        rc = ENABLE_INPLACE_ERR_DEV;
        goto errout;
    }

    if (setjmp(setjmp_env)) {
        printf("Reading ext4 extent caused an exception\n");
        rc = -1;
        goto errout;
    }

    if (read_ext(data.realfd, 0) != 0) {
        printf("Failed to read ext4 extent\n");
        rc = -1;
        goto errout;
    }

    data.numblocks = size / CRYPT_SECTORS_PER_BUFSIZE;
    data.tot_numblocks = tot_size / CRYPT_SECTORS_PER_BUFSIZE;
    data.blocks_already_done = *size_already_done / CRYPT_SECTORS_PER_BUFSIZE;

    printf("Encrypting ext4 filesystem in place...");

    data.tot_used_blocks = data.numblocks;
    for (i = 0; i < aux_info.groups; ++i) {
      data.tot_used_blocks -= aux_info.bg_desc[i].bg_free_blocks_count;
    }

    data.one_pct = data.tot_used_blocks / 100;
    data.cur_pct = 0;

    struct timespec time_started = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &time_started)) {
        printf("Error getting time at start");
        // Note - continue anyway - we'll run with 0
    }
    data.time_started = time_started.tv_sec;
    data.remaining_time = -1;

    rc = encrypt_groups(&data);
    if (rc) {
        printf("Error encrypting groups");
        goto errout;
    }

    *size_already_done += data.completed ? size : data.last_written_sector;
    rc = 0;

errout:
    close(data.realfd);
    close(data.cryptofd);

    return rc;
}

static void log_progress_f2fs(u64 block, bool completed)
{
    // Precondition - if completed data = 0 else data != 0

    // Track progress so we can skip logging blocks
    static u64 last_block = (u64)-1;

    // Need to close existing 'Encrypting from' log?
    if (completed || (last_block != (u64)-1 && block != last_block + 1)) {
        printf("Encrypted to block %" PRId64, last_block);
        last_block = -1;
    }

    // Need to start new 'Encrypting from' log?
    if (!completed && (last_block == (u64)-1 || block != last_block + 1)) {
        printf("Encrypting from block %" PRId64, block);
    }

    // Update offset
    if (!completed) {
        last_block = block;
    }
}

static int encrypt_one_block_f2fs(u64 pos, void *data)
{
    struct encryptGroupsData *priv_dat = (struct encryptGroupsData *)data;

    priv_dat->blocks_already_done = pos - 1;
    update_progress(priv_dat, 1);

    off64_t offset = pos * CRYPT_INPLACE_BUFSIZE;

    if (pread64(priv_dat->realfd, priv_dat->buffer, CRYPT_INPLACE_BUFSIZE, offset) <= 0) {
        printf("Error reading real_blkdev %s for f2fs inplace encrypt", priv_dat->crypto_blkdev);
        return -1;
    }

    if (pwrite64(priv_dat->cryptofd, priv_dat->buffer, CRYPT_INPLACE_BUFSIZE, offset) <= 0) {
        printf("Error writing crypto_blkdev %s for f2fs inplace encrypt", priv_dat->crypto_blkdev);
        return -1;
    } else {
        log_progress_f2fs(pos, false);
    }

    return 0;
}

static int cryptfs_enable_inplace_f2fs(char *crypto_blkdev,
                                       char *real_blkdev,
                                       off64_t size,
                                       off64_t *size_already_done,
                                       off64_t tot_size,
                                       off64_t previously_encrypted_upto)
{
    u32 i;
    struct encryptGroupsData data;
    struct f2fs_info *f2fs_info = NULL;
    int rc = ENABLE_INPLACE_ERR_OTHER;
    if (previously_encrypted_upto > *size_already_done) {
        printf("Not fast encrypting since resuming part way through");
        return ENABLE_INPLACE_ERR_OTHER;
    }
    memset(&data, 0, sizeof(data));
    data.real_blkdev = real_blkdev;
    data.crypto_blkdev = crypto_blkdev;
    data.realfd = -1;
    data.cryptofd = -1;
    if ( (data.realfd = open64(real_blkdev, O_RDWR)) < 0) {
        printf("Error opening real_blkdev %s for f2fs inplace encrypt\n",
              real_blkdev);
        goto errout;
    }
    if ( (data.cryptofd = open64(crypto_blkdev, O_WRONLY)) < 0) {
        printf("Error opening crypto_blkdev %s for f2fs inplace encrypt. err=%d(%s)\n",
              crypto_blkdev, errno, strerror(errno));
        rc = ENABLE_INPLACE_ERR_DEV;
        goto errout;
    }

    f2fs_info = generate_f2fs_info(data.realfd);
    if (!f2fs_info)
      goto errout;

    data.numblocks = size / CRYPT_SECTORS_PER_BUFSIZE;
    data.tot_numblocks = tot_size / CRYPT_SECTORS_PER_BUFSIZE;
    data.blocks_already_done = *size_already_done / CRYPT_SECTORS_PER_BUFSIZE;

    data.tot_used_blocks = get_num_blocks_used(f2fs_info);

    data.one_pct = data.tot_used_blocks / 100;
    data.cur_pct = 0;
    data.time_started = time(NULL);
    data.remaining_time = -1;

    data.buffer = malloc(f2fs_info->block_size);
    if (!data.buffer) {
        printf("Failed to allocate crypto buffer");
        goto errout;
    }

    data.count = 0;

    /* Currently, this either runs to completion, or hits a nonrecoverable error */
    rc = run_on_used_blocks(data.blocks_already_done, f2fs_info, &encrypt_one_block_f2fs, &data);

    if (rc) {
        printf("Error in running over f2fs blocks");
        rc = ENABLE_INPLACE_ERR_OTHER;
        goto errout;
    }

    *size_already_done += size;
    rc = 0;

errout:
    if (rc)
        printf("Failed to encrypt f2fs filesystem on %s", real_blkdev);

    log_progress_f2fs(0, true);
    free(f2fs_info);
    free(data.buffer);
    close(data.realfd);
    close(data.cryptofd);

    return rc;
}

static int cryptfs_enable_inplace_full(char *crypto_blkdev, char *real_blkdev,
                                       off64_t size, off64_t *size_already_done,
                                       off64_t tot_size,
                                       off64_t previously_encrypted_upto)
{
    int realfd, cryptofd;
    char *buf[CRYPT_INPLACE_BUFSIZE];
    int rc = ENABLE_INPLACE_ERR_OTHER;
    off64_t numblocks, i, remainder;
    off64_t one_pct, cur_pct, new_pct;
    off64_t blocks_already_done, tot_numblocks;

    if ( (realfd = open(real_blkdev, O_RDONLY)) < 0) { 
        printf("Error opening real_blkdev %s for inplace encrypt\n", real_blkdev);
        return ENABLE_INPLACE_ERR_OTHER;
    }

    if ( (cryptofd = open(crypto_blkdev, O_WRONLY)) < 0) { 
        printf("Error opening crypto_blkdev %s for inplace encrypt. err=%d(%s)\n",
              crypto_blkdev, errno, strerror(errno));
        close(realfd);
        return ENABLE_INPLACE_ERR_DEV;
    }

    /* This is pretty much a simple loop of reading 4K, and writing 4K.
     * The size passed in is the number of 512 byte sectors in the filesystem.
     * So compute the number of whole 4K blocks we should read/write,
     * and the remainder.
     */
    numblocks = size / CRYPT_SECTORS_PER_BUFSIZE;
    remainder = size % CRYPT_SECTORS_PER_BUFSIZE;
    tot_numblocks = tot_size / CRYPT_SECTORS_PER_BUFSIZE;
    blocks_already_done = *size_already_done / CRYPT_SECTORS_PER_BUFSIZE;

    printf("Encrypting filesystem in place...");

    i = previously_encrypted_upto + 1 - *size_already_done;

    if (lseek64(realfd, i * CRYPT_SECTOR_SIZE, SEEK_SET) < 0) {
        printf("Cannot seek to previously encrypted point on %s", real_blkdev);
        goto errout;
    }

    if (lseek64(cryptofd, i * CRYPT_SECTOR_SIZE, SEEK_SET) < 0) {
        printf("Cannot seek to previously encrypted point on %s", crypto_blkdev);
        goto errout;
    }

    for (;i < size && i % CRYPT_SECTORS_PER_BUFSIZE != 0; ++i) {
        if (unix_read(realfd, buf, CRYPT_SECTOR_SIZE) <= 0) {
            printf("Error reading initial sectors from real_blkdev %s for "
                  "inplace encrypt\n", crypto_blkdev);
            goto errout;
        }
        if (unix_write(cryptofd, buf, CRYPT_SECTOR_SIZE) <= 0) {
            printf("Error writing initial sectors to crypto_blkdev %s for "
                  "inplace encrypt\n", crypto_blkdev);
            goto errout;
        } else {
            printf("Encrypted 1 block at %" PRId64, i);
        }
    }

    one_pct = tot_numblocks / 100;
    cur_pct = 0;
    /* process the majority of the filesystem in blocks */
    for (i/=CRYPT_SECTORS_PER_BUFSIZE; i<numblocks; i++) {
        new_pct = (i + blocks_already_done) / one_pct;
        if (new_pct > cur_pct) {
            char buf[8];

            cur_pct = new_pct;
            snprintf(buf, sizeof(buf), "%" PRId64, cur_pct);
            property_set("vold.encrypt_progress", buf);
        }
        if (unix_read(realfd, buf, CRYPT_INPLACE_BUFSIZE) <= 0) {
            printf("Error reading real_blkdev %s for inplace encrypt", crypto_blkdev);
            goto errout;
        }
        if (unix_write(cryptofd, buf, CRYPT_INPLACE_BUFSIZE) <= 0) {
            printf("Error writing crypto_blkdev %s for inplace encrypt", crypto_blkdev);
            goto errout;
        } else {
            printf("Encrypted %d block at %" PRId64,
                  CRYPT_SECTORS_PER_BUFSIZE,
                  i * CRYPT_SECTORS_PER_BUFSIZE);
        }

       if (1) {
            printf("Stopping encryption due to low battery");
            *size_already_done += (i + 1) * CRYPT_SECTORS_PER_BUFSIZE - 1;
            rc = 0;
            goto errout;
        }
    }

    /* Do any remaining sectors */
    for (i=0; i<remainder; i++) {
        if (unix_read(realfd, buf, CRYPT_SECTOR_SIZE) <= 0) {
            printf("Error reading final sectors from real_blkdev %s for inplace encrypt", crypto_blkdev);
            goto errout;
        }
        if (unix_write(cryptofd, buf, CRYPT_SECTOR_SIZE) <= 0) {
            printf("Error writing final sectors to crypto_blkdev %s for inplace encrypt", crypto_blkdev);
            goto errout;
        } else {
            printf("Encrypted 1 block at next location");
        }
    }

    *size_already_done += size;
    rc = 0;

errout:
    close(realfd);
    close(cryptofd);

    return rc;
}

/* returns on of the ENABLE_INPLACE_* return codes */
static int cryptfs_enable_inplace(char *crypto_blkdev, char *real_blkdev,
                                  off64_t size, off64_t *size_already_done,
                                  off64_t tot_size,
                                  off64_t previously_encrypted_upto)
{
    int rc_ext4, rc_f2fs, rc_full;
    if (previously_encrypted_upto) {
        printf("Continuing encryption from %" PRId64, previously_encrypted_upto);
    }

    if (*size_already_done + size < previously_encrypted_upto) {
        *size_already_done += size;
        return 0;
    }

    /* TODO: identify filesystem type.
     * As is, cryptfs_enable_inplace_ext4 will fail on an f2fs partition, and
     * then we will drop down to cryptfs_enable_inplace_f2fs.
     * */
    if ((rc_ext4 = cryptfs_enable_inplace_ext4(crypto_blkdev, real_blkdev,
                                size, size_already_done,
                                tot_size, previously_encrypted_upto)) == 0) {
      return 0;
    }
    printf("cryptfs_enable_inplace_ext4()=%d\n", rc_ext4);

    if ((rc_f2fs = cryptfs_enable_inplace_f2fs(crypto_blkdev, real_blkdev,
                                size, size_already_done,
                                tot_size, previously_encrypted_upto)) == 0) {
      return 0;
    }
    printf("cryptfs_enable_inplace_f2fs()=%d\n", rc_f2fs);

    rc_full = cryptfs_enable_inplace_full(crypto_blkdev, real_blkdev,
                                       size, size_already_done, tot_size,
                                       previously_encrypted_upto);
    printf("cryptfs_enable_inplace_full()=%d\n", rc_full);

    /* Hack for b/17898962, the following is the symptom... */
    if (rc_ext4 == ENABLE_INPLACE_ERR_DEV
        && rc_f2fs == ENABLE_INPLACE_ERR_DEV
        && rc_full == ENABLE_INPLACE_ERR_DEV) {
            return ENABLE_INPLACE_ERR_DEV;
    }
    return rc_full;
}

#define CRYPTO_ENABLE_WIPE 1
#define CRYPTO_ENABLE_INPLACE 2

#define FRAMEWORK_BOOT_WAIT 60

static inline int should_encrypt(struct volume_info *volume)
{
    return (volume->flags & (VOL_ENCRYPTABLE | VOL_NONREMOVABLE)) ==
            (VOL_ENCRYPTABLE | VOL_NONREMOVABLE);
}

static int cryptfs_SHA256_fileblock(const char* filename, __le8* buf)
{
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        printf("Error opening file %s", filename);
        return -1;
    }

    char block[CRYPT_INPLACE_BUFSIZE];
    memset(block, 0, sizeof(block));
    if (unix_read(fd, block, sizeof(block)) < 0) {
        printf("Error reading file %s", filename);
        close(fd);
        return -1;
    }

    close(fd);

    SHA256_CTX c;
    SHA256_Init(&c);
    SHA256_Update(&c, block, sizeof(block));
    SHA256_Final(buf, &c);

    return 0;
}

static int get_fs_type(struct fstab_rec *rec)
{
    if (!strcmp(rec->fs_type, "ext4")) {
        return EXT4_FS;
    } else if (!strcmp(rec->fs_type, "f2fs")) {
        return F2FS_FS;
    } else {
        return -1;
    }
}

static int cryptfs_enable_all_volumes(struct crypt_mnt_ftr *crypt_ftr, int how,
                                      char *crypto_blkdev, char *real_blkdev,
                                      int previously_encrypted_upto)
{
    off64_t cur_encryption_done=0, tot_encryption_size=0;
    int i, rc = -1;

    if (1) {
        printf("Not starting encryption due to low battery");
        return 0;
    }

    /* The size of the userdata partition, and add in the vold volumes below */
    tot_encryption_size = crypt_ftr->fs_size;

    if (how == CRYPTO_ENABLE_WIPE) {
        struct fstab_rec* rec = fs_mgr_get_entry_for_mount_point(fstab, DATA_MNT_POINT);
        int fs_type = get_fs_type(rec);
        if (fs_type < 0) {
            printf("cryptfs_enable: unsupported fs type %s\n", rec->fs_type);
            return -1;
        }
        rc = cryptfs_enable_wipe(crypto_blkdev, crypt_ftr->fs_size, fs_type);
    } else if (how == CRYPTO_ENABLE_INPLACE) {
        rc = cryptfs_enable_inplace(crypto_blkdev, real_blkdev,
                                    crypt_ftr->fs_size, &cur_encryption_done,
                                    tot_encryption_size,
                                    previously_encrypted_upto);

        if (rc == ENABLE_INPLACE_ERR_DEV) {
            /* Hack for b/17898962 */
            printf("cryptfs_enable: crypto block dev failure. Must reboot...\n");
            cryptfs_reboot(reboot);
        }

        if (!rc) {
            crypt_ftr->encrypted_upto = cur_encryption_done;
        }

        if (!rc && crypt_ftr->encrypted_upto == crypt_ftr->fs_size) {
            /* The inplace routine never actually sets the progress to 100% due
             * to the round down nature of integer division, so set it here */
            property_set("vold.encrypt_progress", "100");
        }
    } else {
        /* Shouldn't happen */
        printf("cryptfs_enable: internal error, unknown option\n");
        rc = -1;
    }

    return rc;
}

int cryptfs_enable_internal(char *howarg, int crypt_type, char *passwd,
                            int allow_reboot)
{
    int how = 0;
    char crypto_blkdev[MAXPATHLEN], real_blkdev[MAXPATHLEN];
    unsigned long nr_sec;
    unsigned char decrypted_master_key[KEY_LEN_BYTES];
    int rc=-1, fd, i, ret;
    struct crypt_mnt_ftr crypt_ftr;
    struct crypt_persist_data *pdata;
    char encrypted_state[PROPERTY_VALUE_MAX];
    char lockid[32] = { 0 };
    char key_loc[PROPERTY_VALUE_MAX];
    char fuse_sdcard[PROPERTY_VALUE_MAX];
    char *sd_mnt_point;
    int num_vols;
    struct volume_info *vol_list = 0;
    off64_t previously_encrypted_upto = 0;
printf("cryptfs_enable_internal disabled by TWRP\n");
return -1;
    if (!strcmp(howarg, "wipe")) {
      how = CRYPTO_ENABLE_WIPE;
    } else if (! strcmp(howarg, "inplace")) {
      how = CRYPTO_ENABLE_INPLACE;
    } else {
      /* Shouldn't happen, as CommandListener vets the args */
      goto error_unencrypted;
    }

    /* See if an encryption was underway and interrupted */
    if (how == CRYPTO_ENABLE_INPLACE
          && get_crypt_ftr_and_key(&crypt_ftr) == 0
          && (crypt_ftr.flags & CRYPT_ENCRYPTION_IN_PROGRESS)) {
        previously_encrypted_upto = crypt_ftr.encrypted_upto;
        crypt_ftr.encrypted_upto = 0;
        crypt_ftr.flags &= ~CRYPT_ENCRYPTION_IN_PROGRESS;

        /* At this point, we are in an inconsistent state. Until we successfully
           complete encryption, a reboot will leave us broken. So mark the
           encryption failed in case that happens.
           On successfully completing encryption, remove this flag */
        crypt_ftr.flags |= CRYPT_INCONSISTENT_STATE;

        put_crypt_ftr_and_key(&crypt_ftr);
    }

    property_get("ro.crypto.state", encrypted_state, "");
    if (!strcmp(encrypted_state, "encrypted") && !previously_encrypted_upto) {
        printf("Device is already running encrypted, aborting");
        goto error_unencrypted;
    }

    // TODO refactor fs_mgr_get_crypt_info to get both in one call
    fs_mgr_get_crypt_info(fstab, key_loc, 0, sizeof(key_loc));
    fs_mgr_get_crypt_info(fstab, 0, real_blkdev, sizeof(real_blkdev));

    /* Get the size of the real block device */
    fd = open(real_blkdev, O_RDONLY);
    if ( (nr_sec = get_blkdev_size(fd)) == 0) {
        printf("Cannot get size of block device %s\n", real_blkdev);
        goto error_unencrypted;
    }
    close(fd);

    /* If doing inplace encryption, make sure the orig fs doesn't include the crypto footer */
    if ((how == CRYPTO_ENABLE_INPLACE) && (!strcmp(key_loc, KEY_IN_FOOTER))) {
        unsigned int fs_size_sec, max_fs_size_sec;
        fs_size_sec = get_fs_size(real_blkdev);
        if (fs_size_sec == 0)
            fs_size_sec = get_f2fs_filesystem_size_sec(real_blkdev);

        max_fs_size_sec = nr_sec - (CRYPT_FOOTER_OFFSET / CRYPT_SECTOR_SIZE);

        if (fs_size_sec > max_fs_size_sec) {
            printf("Orig filesystem overlaps crypto footer region.  Cannot encrypt in place.");
            goto error_unencrypted;
        }
    }

    /* Get a wakelock as this may take a while, and we don't want the
     * device to sleep on us.  We'll grab a partial wakelock, and if the UI
     * wants to keep the screen on, it can grab a full wakelock.
     */
    snprintf(lockid, sizeof(lockid), "enablecrypto%d", (int) getpid());
    acquire_wake_lock(PARTIAL_WAKE_LOCK, lockid);

    /* Get the sdcard mount point */
    sd_mnt_point = getenv("EMULATED_STORAGE_SOURCE");
    if (!sd_mnt_point) {
       sd_mnt_point = getenv("EXTERNAL_STORAGE");
    }
    if (!sd_mnt_point) {
        sd_mnt_point = "/mnt/sdcard";
    }

    /* TODO
     * Currently do not have test devices with multiple encryptable volumes.
     * When we acquire some, re-add support.
     */
    num_vols=0/*vold_getNumDirectVolumes()*/;
    vol_list = malloc(sizeof(struct volume_info) * num_vols);
    //vold_getDirectVolumeList(vol_list);

    for (i=0; i<num_vols; i++) {
        if (should_encrypt(&vol_list[i])) {
            printf("Cannot encrypt if there are multiple encryptable volumes"
                  "%s\n", vol_list[i].label);
            goto error_unencrypted;
        }
    }

    /* The init files are setup to stop the class main and late start when
     * vold sets trigger_shutdown_framework.
     */
    property_set("vold.decrypt", "trigger_shutdown_framework");
    printf("Just asked init to shut down class main\n");

    if (1 /*vold_unmountAllAsecs()*/) {
        /* Just report the error.  If any are left mounted,
         * umounting /data below will fail and handle the error.
         */
        printf("Error unmounting internal asecs");
    }

    property_get("ro.crypto.fuse_sdcard", fuse_sdcard, "");
    if (!strcmp(fuse_sdcard, "true")) {
        /* This is a device using the fuse layer to emulate the sdcard semantics
         * on top of the userdata partition.  vold does not manage it, it is managed
         * by the sdcard service.  The sdcard service was killed by the property trigger
         * above, so just unmount it now.  We must do this _AFTER_ killing the framework,
         * unlike the case for vold managed devices above.
         */
        if (wait_and_unmount(sd_mnt_point, false)) {
            goto error_shutting_down;
        }
    }

    /* Now unmount the /data partition. */
    if (wait_and_unmount(DATA_MNT_POINT, false)) {
        if (allow_reboot) {
            goto error_shutting_down;
        } else {
            goto error_unencrypted;
        }
    }

    /* Do extra work for a better UX when doing the long inplace encryption */
    if (how == CRYPTO_ENABLE_INPLACE) {
        /* Now that /data is unmounted, we need to mount a tmpfs
         * /data, set a property saying we're doing inplace encryption,
         * and restart the framework.
         */
        if (fs_mgr_do_tmpfs_mount(DATA_MNT_POINT)) {
            goto error_shutting_down;
        }
        /* Tells the framework that inplace encryption is starting */
        property_set("vold.encrypt_progress", "0");

        /* restart the framework. */
        /* Create necessary paths on /data */
        if (prep_data_fs()) {
            goto error_shutting_down;
        }

        /* Ugh, shutting down the framework is not synchronous, so until it
         * can be fixed, this horrible hack will wait a moment for it all to
         * shut down before proceeding.  Without it, some devices cannot
         * restart the graphics services.
         */
        sleep(2);

        /* startup service classes main and late_start */
        property_set("vold.decrypt", "trigger_restart_min_framework");
        printf("Just triggered restart_min_framework\n");

        /* OK, the framework is restarted and will soon be showing a
         * progress bar.  Time to setup an encrypted mapping, and
         * either write a new filesystem, or encrypt in place updating
         * the progress bar as we work.
         */
    }

    /* Start the actual work of making an encrypted filesystem */
    /* Initialize a crypt_mnt_ftr for the partition */
    if (previously_encrypted_upto == 0) {
        if (cryptfs_init_crypt_mnt_ftr(&crypt_ftr)) {
            goto error_shutting_down;
        }

        if (!strcmp(key_loc, KEY_IN_FOOTER)) {
            crypt_ftr.fs_size = nr_sec
              - (CRYPT_FOOTER_OFFSET / CRYPT_SECTOR_SIZE);
        } else {
            crypt_ftr.fs_size = nr_sec;
        }
        /* At this point, we are in an inconsistent state. Until we successfully
           complete encryption, a reboot will leave us broken. So mark the
           encryption failed in case that happens.
           On successfully completing encryption, remove this flag */
        crypt_ftr.flags |= CRYPT_INCONSISTENT_STATE;
        crypt_ftr.crypt_type = crypt_type;
        strcpy((char *)crypt_ftr.crypto_type_name, "aes-cbc-essiv:sha256");

        /* Make an encrypted master key */
        if (create_encrypted_random_key(passwd, crypt_ftr.master_key, crypt_ftr.salt, &crypt_ftr)) {
            printf("Cannot create encrypted master key\n");
            goto error_shutting_down;
        }

        /* Write the key to the end of the partition */
        put_crypt_ftr_and_key(&crypt_ftr);

        /* If any persistent data has been remembered, save it.
         * If none, create a valid empty table and save that.
         */
        if (!persist_data) {
           pdata = malloc(CRYPT_PERSIST_DATA_SIZE);
           if (pdata) {
               init_empty_persist_data(pdata, CRYPT_PERSIST_DATA_SIZE);
               persist_data = pdata;
           }
        }
        if (persist_data) {
            save_persistent_data();
        }
    }

    decrypt_master_key(passwd, decrypted_master_key, &crypt_ftr, 0, 0);
    create_crypto_blk_dev(&crypt_ftr, decrypted_master_key, real_blkdev, crypto_blkdev,
                          "userdata");

    /* If we are continuing, check checksums match */
    rc = 0;
    if (previously_encrypted_upto) {
        __le8 hash_first_block[SHA256_DIGEST_LENGTH];
        rc = cryptfs_SHA256_fileblock(crypto_blkdev, hash_first_block);

        if (!rc && memcmp(hash_first_block, crypt_ftr.hash_first_block,
                          sizeof(hash_first_block)) != 0) {
            printf("Checksums do not match - trigger wipe");
            rc = -1;
        }
    }

    if (!rc) {
        rc = cryptfs_enable_all_volumes(&crypt_ftr, how,
                                        crypto_blkdev, real_blkdev,
                                        previously_encrypted_upto);
    }

    /* Calculate checksum if we are not finished */
    if (!rc && crypt_ftr.encrypted_upto != crypt_ftr.fs_size) {
        rc = cryptfs_SHA256_fileblock(crypto_blkdev,
                                      crypt_ftr.hash_first_block);
        if (rc) {
            printf("Error calculating checksum for continuing encryption");
            rc = -1;
        }
    }

    /* Undo the dm-crypt mapping whether we succeed or not */
    delete_crypto_blk_dev("userdata");

    free(vol_list);

    if (! rc) {
        /* Success */
        crypt_ftr.flags &= ~CRYPT_INCONSISTENT_STATE;

        if (crypt_ftr.encrypted_upto != crypt_ftr.fs_size) {
            printf("Encrypted up to sector %lld - will continue after reboot",
                  crypt_ftr.encrypted_upto);
            crypt_ftr.flags |= CRYPT_ENCRYPTION_IN_PROGRESS;
        }

        put_crypt_ftr_and_key(&crypt_ftr);

        if (crypt_ftr.encrypted_upto == crypt_ftr.fs_size) {
          char value[PROPERTY_VALUE_MAX];
          property_get("ro.crypto.state", value, "");
          if (!strcmp(value, "")) {
            /* default encryption - continue first boot sequence */
            property_set("ro.crypto.state", "encrypted");
            release_wake_lock(lockid);
            cryptfs_check_passwd(DEFAULT_PASSWORD);
            cryptfs_restart_internal(1);
            return 0;
          } else {
            sleep(2); /* Give the UI a chance to show 100% progress */
            cryptfs_reboot(reboot);
          }
        } else {
            sleep(2); /* Partially encrypted, ensure writes flushed to ssd */
            cryptfs_reboot(shutdown);
        }
    } else {
        char value[PROPERTY_VALUE_MAX];

        property_get("ro.vold.wipe_on_crypt_fail", value, "0");
        if (!strcmp(value, "1")) {
            /* wipe data if encryption failed */
            printf("encryption failed - rebooting into recovery to wipe data\n");
            mkdir("/cache/recovery", 0700);
            int fd = open("/cache/recovery/command", O_RDWR|O_CREAT|O_TRUNC, 0600);
            if (fd >= 0) {
                write(fd, "--wipe_data\n", strlen("--wipe_data\n") + 1);
                write(fd, "--reason=cryptfs_enable_internal\n", strlen("--reason=cryptfs_enable_internal\n") + 1);
                close(fd);
            } else {
                printf("could not open /cache/recovery/command\n");
            }
            cryptfs_reboot(recovery);
        } else {
            /* set property to trigger dialog */
            property_set("vold.encrypt_progress", "error_partially_encrypted");
            release_wake_lock(lockid);
        }
        return -1;
    }

    /* hrm, the encrypt step claims success, but the reboot failed.
     * This should not happen.
     * Set the property and return.  Hope the framework can deal with it.
     */
    property_set("vold.encrypt_progress", "error_reboot_failed");
    release_wake_lock(lockid);
    return rc;

error_unencrypted:
    free(vol_list);
    property_set("vold.encrypt_progress", "error_not_encrypted");
    if (lockid[0]) {
        release_wake_lock(lockid);
    }
    return -1;

error_shutting_down:
    /* we failed, and have not encrypted anthing, so the users's data is still intact,
     * but the framework is stopped and not restarted to show the error, so it's up to
     * vold to restart the system.
     */
    printf("Error enabling encryption after framework is shutdown, no data changed, restarting system");
    cryptfs_reboot(reboot);

    /* shouldn't get here */
    property_set("vold.encrypt_progress", "error_shutting_down");
    free(vol_list);
    if (lockid[0]) {
        release_wake_lock(lockid);
    }
    return -1;
}

int cryptfs_enable(char *howarg, int type, char *passwd, int allow_reboot)
{
    char* adjusted_passwd = adjust_passwd(passwd);
    if (adjusted_passwd) {
        passwd = adjusted_passwd;
    }

    int rc = cryptfs_enable_internal(howarg, type, passwd, allow_reboot);

    free(adjusted_passwd);
    return rc;
}

int cryptfs_enable_default(char *howarg, int allow_reboot)
{
    return cryptfs_enable_internal(howarg, CRYPT_TYPE_DEFAULT,
                          DEFAULT_PASSWORD, allow_reboot);
}

int cryptfs_changepw(int crypt_type, const char *newpw)
{
    struct crypt_mnt_ftr crypt_ftr;
    unsigned char decrypted_master_key[KEY_LEN_BYTES];

    /* This is only allowed after we've successfully decrypted the master key */
    if (!master_key_saved) {
        printf("Key not saved, aborting");
        return -1;
    }

    if (crypt_type < 0 || crypt_type > CRYPT_TYPE_MAX_TYPE) {
        printf("Invalid crypt_type %d", crypt_type);
        return -1;
    }

    /* get key */
    if (get_crypt_ftr_and_key(&crypt_ftr)) {
        printf("Error getting crypt footer and key");
        return -1;
    }

    crypt_ftr.crypt_type = crypt_type;

    char* adjusted_passwd = adjust_passwd(newpw);
    if (adjusted_passwd) {
        newpw = adjusted_passwd;
    }

    encrypt_master_key(crypt_type == CRYPT_TYPE_DEFAULT ? DEFAULT_PASSWORD
                                                        : newpw,
                       crypt_ftr.salt,
                       saved_master_key,
                       crypt_ftr.master_key,
                       &crypt_ftr);

    /* save the key */
    put_crypt_ftr_and_key(&crypt_ftr);

    free(adjusted_passwd);
    return 0;
}

static int persist_get_key(char *fieldname, char *value)
{
    unsigned int i;

    if (persist_data == NULL) {
        return -1;
    }
    for (i = 0; i < persist_data->persist_valid_entries; i++) {
        if (!strncmp(persist_data->persist_entry[i].key, fieldname, PROPERTY_KEY_MAX)) {
            /* We found it! */
            strlcpy(value, persist_data->persist_entry[i].val, PROPERTY_VALUE_MAX);
            return 0;
        }
    }

    return -1;
}

static int persist_set_key(char *fieldname, char *value, int encrypted)
{
    unsigned int i;
    unsigned int num;
    struct crypt_mnt_ftr crypt_ftr;
    unsigned int max_persistent_entries;
    unsigned int dsize;

    if (persist_data == NULL) {
        return -1;
    }

    /* If encrypted, use the values from the crypt_ftr, otherwise
     * use the values for the current spec.
     */
    if (encrypted) {
        if(get_crypt_ftr_and_key(&crypt_ftr)) {
            return -1;
        }
        dsize = crypt_ftr.persist_data_size;
    } else {
        dsize = CRYPT_PERSIST_DATA_SIZE;
    }
    max_persistent_entries = (dsize - sizeof(struct crypt_persist_data)) /
                             sizeof(struct crypt_persist_entry);

    num = persist_data->persist_valid_entries;

    for (i = 0; i < num; i++) {
        if (!strncmp(persist_data->persist_entry[i].key, fieldname, PROPERTY_KEY_MAX)) {
            /* We found an existing entry, update it! */
            memset(persist_data->persist_entry[i].val, 0, PROPERTY_VALUE_MAX);
            strlcpy(persist_data->persist_entry[i].val, value, PROPERTY_VALUE_MAX);
            return 0;
        }
    }

    /* We didn't find it, add it to the end, if there is room */
    if (persist_data->persist_valid_entries < max_persistent_entries) {
        memset(&persist_data->persist_entry[num], 0, sizeof(struct crypt_persist_entry));
        strlcpy(persist_data->persist_entry[num].key, fieldname, PROPERTY_KEY_MAX);
        strlcpy(persist_data->persist_entry[num].val, value, PROPERTY_VALUE_MAX);
        persist_data->persist_valid_entries++;
        return 0;
    }

    return -1;
}

/* Return the value of the specified field. */
int cryptfs_getfield(char *fieldname, char *value, int len)
{
    char temp_value[PROPERTY_VALUE_MAX];
    char real_blkdev[MAXPATHLEN];
    /* 0 is success, 1 is not encrypted,
     * -1 is value not set, -2 is any other error
     */
    int rc = -2;

    if (persist_data == NULL) {
        load_persistent_data();
        if (persist_data == NULL) {
            printf("Getfield error, cannot load persistent data");
            goto out;
        }
    }

    if (!persist_get_key(fieldname, temp_value)) {
        /* We found it, copy it to the caller's buffer and return */
        strlcpy(value, temp_value, len);
        rc = 0;
    } else {
        /* Sadness, it's not there.  Return the error */
        rc = -1;
    }

out:
    return rc;
}

/* Set the value of the specified field. */
int cryptfs_setfield(char *fieldname, char *value)
{
    struct crypt_persist_data stored_pdata;
    struct crypt_persist_data *pdata_p;
    struct crypt_mnt_ftr crypt_ftr;
    char encrypted_state[PROPERTY_VALUE_MAX];
    /* 0 is success, -1 is an error */
    int rc = -1;
    int encrypted = 0;

    if (persist_data == NULL) {
        load_persistent_data();
        if (persist_data == NULL) {
            printf("Setfield error, cannot load persistent data");
            goto out;
        }
    }

    property_get("ro.crypto.state", encrypted_state, "");
    if (!strcmp(encrypted_state, "encrypted") ) {
        encrypted = 1;
    }

    if (persist_set_key(fieldname, value, encrypted)) {
        goto out;
    }

    /* If we are running encrypted, save the persistent data now */
    if (encrypted) {
        if (save_persistent_data()) {
            printf("Setfield error, cannot save persistent data");
            goto out;
        }
    }

    rc = 0;

out:
    return rc;
}

/* Checks userdata. Attempt to mount the volume if default-
 * encrypted.
 * On success trigger next init phase and return 0.
 * Currently do not handle failure - see TODO below.
 */
int cryptfs_mount_default_encrypted(void)
{
    char decrypt_state[PROPERTY_VALUE_MAX];
    property_get("vold.decrypt", decrypt_state, "0");
    if (!strcmp(decrypt_state, "0")) {
        printf("Not encrypted - should not call here");
    } else {
        int crypt_type = cryptfs_get_password_type();
        if (crypt_type < 0 || crypt_type > CRYPT_TYPE_MAX_TYPE) {
            printf("Bad crypt type - error");
        } else if (crypt_type != CRYPT_TYPE_DEFAULT) {
            printf("Password is not default - "
                  "starting min framework to prompt");
            property_set("vold.decrypt", "trigger_restart_min_framework");
            return 0;
        } else if (cryptfs_check_passwd(DEFAULT_PASSWORD) == 0) {
            printf("Password is default - restarting filesystem");
            cryptfs_restart_internal(0);
            return 0;
        } else {
            printf("Encrypted, default crypt type but can't decrypt");
        }
    }

    /** Corrupt. Allow us to boot into framework, which will detect bad
        crypto when it calls do_crypto_complete, then do a factory reset
     */
    property_set("vold.decrypt", "trigger_restart_min_framework");
    return 0;
}

/* Returns type of the password, default, pattern, pin or password.
 */
int cryptfs_get_password_type(void)
{
    struct crypt_mnt_ftr crypt_ftr;
    char fstab_filename[PROPERTY_VALUE_MAX + sizeof(FSTAB_PREFIX)];
    char propbuf[PROPERTY_VALUE_MAX];

    property_get("ro.hardware", propbuf, "");
    snprintf(fstab_filename, sizeof(fstab_filename), FSTAB_PREFIX"%s", propbuf);

    fstab = fs_mgr_read_fstab(fstab_filename);
    if (!fstab) {
        printf("failed to open %s\n", fstab_filename);
        return -1;
    }

    if (get_crypt_ftr_and_key(&crypt_ftr)) {
        printf("Error getting crypt footer and key\n");
        return -1;
    }

    if (crypt_ftr.flags & CRYPT_INCONSISTENT_STATE) {
        return -1;
    }

    return crypt_ftr.crypt_type;
}

char* cryptfs_get_password()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec < password_expiry_time) {
        return password;
    } else {
        cryptfs_clear_password();
        return 0;
    }
}

void cryptfs_clear_password()
{
    if (password) {
        size_t len = strlen(password);
        memset(password, 0, len);
        free(password);
        password = 0;
        password_expiry_time = 0;
    }
}
