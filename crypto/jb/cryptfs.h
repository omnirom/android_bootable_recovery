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

/* This structure starts 16,384 bytes before the end of a hardware
 * partition that is encrypted, or in a separate partition.  It's location
 * is specified by a property set in init.<device>.rc.
 * The structure allocates 48 bytes for a key, but the real key size is
 * specified in the struct.  Currently, the code is hardcoded to use 128
 * bit keys.
 * The fields after salt are only valid in rev 1.1 and later stuctures.
 * Obviously, the filesystem does not include the last 16 kbytes
 * of the partition if the crypt_mnt_ftr lives at the end of the
 * partition.
 */

#include <cutils/properties.h>

/* The current cryptfs version */
#define CURRENT_MAJOR_VERSION 1
#define CURRENT_MINOR_VERSION 2

#define CRYPT_FOOTER_OFFSET 0x4000
#define CRYPT_FOOTER_TO_PERSIST_OFFSET 0x1000
#define CRYPT_PERSIST_DATA_SIZE 0x1000

#define MAX_CRYPTO_TYPE_NAME_LEN 64

#define MAX_KEY_LEN 48
#define SALT_LEN 16

/* definitions of flags in the structure below */
#define CRYPT_MNT_KEY_UNENCRYPTED 0x1 /* The key for the partition is not encrypted. */
#define CRYPT_ENCRYPTION_IN_PROGRESS 0x2 /* Set when starting encryption,
                                          * clear when done before rebooting */

#define CRYPT_MNT_MAGIC 0xD0B5B1C4
#define PERSIST_DATA_MAGIC 0xE950CD44

#define SCRYPT_PROP "ro.crypto.scrypt_params"
#define SCRYPT_DEFAULTS { 15, 3, 1 }

/* Key Derivation Function algorithms */
#define KDF_PBKDF2 1
#define KDF_SCRYPT 2

#define __le32 unsigned int
#define __le16 unsigned short int
#define __le8  unsigned char

struct crypt_mnt_ftr {
  __le32 magic;		/* See above */
  __le16 major_version;
  __le16 minor_version;
  __le32 ftr_size; 	/* in bytes, not including key following */
  __le32 flags;		/* See above */
  __le32 keysize;	/* in bytes */
  __le32 spare1;	/* ignored */
  __le64 fs_size;	/* Size of the encrypted fs, in 512 byte sectors */
  __le32 failed_decrypt_count; /* count of # of failed attempts to decrypt and
				  mount, set to 0 on successful mount */
  unsigned char crypto_type_name[MAX_CRYPTO_TYPE_NAME_LEN]; /* The type of encryption
							       needed to decrypt this
							       partition, null terminated */
  __le32 spare2;        /* ignored */
  unsigned char master_key[MAX_KEY_LEN]; /* The encrypted key for decrypting the filesystem */
  unsigned char salt[SALT_LEN];   /* The salt used for this encryption */
  __le64 persist_data_offset[2];  /* Absolute offset to both copies of crypt_persist_data
                                   * on device with that info, either the footer of the
                                   * real_blkdevice or the metadata partition. */

  __le32 persist_data_size;       /* The number of bytes allocated to each copy of the
                                   * persistent data table*/

  __le8  kdf_type; /* The key derivation function used. */

  /* scrypt parameters. See www.tarsnap.com/scrypt/scrypt.pdf */
  __le8  N_factor; /* (1 << N) */
  __le8  r_factor; /* (1 << r) */
  __le8  p_factor; /* (1 << p) */
};

/* Persistant data that should be available before decryption.
 * Things like airplane mode, locale and timezone are kept
 * here and can be retrieved by the CryptKeeper UI to properly
 * configure the phone before asking for the password
 * This is only valid if the major and minor version above
 * is set to 1.1 or higher.
 *
 * This is a 4K structure.  There are 2 copies, and the code alternates
 * writing one and then clearing the previous one.  The reading
 * code reads the first valid copy it finds, based on the magic number.
 * The absolute offset to the first of the two copies is kept in rev 1.1
 * and higher crypt_mnt_ftr structures.
 */
struct crypt_persist_entry {
  char key[PROPERTY_KEY_MAX];
  char val[PROPERTY_VALUE_MAX];
};

/* Should be exactly 4K in size */
struct crypt_persist_data {
  __le32 persist_magic;
  __le32 persist_valid_entries;
  __le32 persist_spare[30];
  struct crypt_persist_entry persist_entry[0];
};

struct volume_info {
   unsigned int size;
   unsigned int flags;
   struct crypt_mnt_ftr crypt_ftr;
   char mnt_point[256];
   char blk_dev[256];
   char crypto_blkdev[256];
   char label[256];
};
#define VOL_NONREMOVABLE   0x1
#define VOL_ENCRYPTABLE    0x2
#define VOL_PRIMARY        0x4
#define VOL_PROVIDES_ASEC  0x8

#ifdef __cplusplus
extern "C" {
#endif

  typedef void (*kdf_func)(char *passwd, unsigned char *salt, unsigned char *ikey, void *params);

  int cryptfs_crypto_complete(void);
  int cryptfs_check_passwd(char *pw);
  int cryptfs_verify_passwd(char *newpw);
  int cryptfs_restart(void);
  int cryptfs_enable(char *flag, char *passwd);
  int cryptfs_changepw(char *newpw);
  int cryptfs_setup_volume(const char *label, int major, int minor,
                           char *crypto_dev_path, unsigned int max_pathlen,
                           int *new_major, int *new_minor);
  int cryptfs_revert_volume(const char *label);
  int cryptfs_getfield(char *fieldname, char *value, int len);
  int cryptfs_setfield(char *fieldname, char *value);
#ifdef __cplusplus
}
#endif

