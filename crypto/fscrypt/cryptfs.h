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

#ifndef ANDROID_VOLD_CRYPTFS_H
#define ANDROID_VOLD_CRYPTFS_H

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

#include <linux/types.h>
#include <stdbool.h>
#include <stdint.h>

#include <cutils/properties.h>

/* The current cryptfs version */
#define CURRENT_MAJOR_VERSION 1
#define CURRENT_MINOR_VERSION 3

#define CRYPT_FOOTER_OFFSET 0x4000
#define CRYPT_FOOTER_TO_PERSIST_OFFSET 0x1000
#define CRYPT_PERSIST_DATA_SIZE 0x1000

#define MAX_CRYPTO_TYPE_NAME_LEN 64

#define MAX_KEY_LEN 48
#define SALT_LEN 16
#define SCRYPT_LEN 32

/* definitions of flags in the structure below */
#define CRYPT_MNT_KEY_UNENCRYPTED 0x1 /* The key for the partition is not encrypted. */
#define CRYPT_ENCRYPTION_IN_PROGRESS       \
    0x2 /* Encryption partially completed, \
           encrypted_upto valid*/
#define CRYPT_INCONSISTENT_STATE                    \
    0x4 /* Set when starting encryption, clear when \
           exit cleanly, either through success or  \
           correctly marked partial encryption */
#define CRYPT_DATA_CORRUPT                      \
    0x8 /* Set when encryption is fine, but the \
           underlying volume is corrupt */
#define CRYPT_FORCE_ENCRYPTION                        \
    0x10 /* Set when it is time to encrypt this       \
            volume on boot. Everything in this        \
            structure is set up correctly as          \
            though device is encrypted except         \
            that the master key is encrypted with the \
            default password. */
#define CRYPT_FORCE_COMPLETE                           \
    0x20 /* Set when the above encryption cycle is     \
            complete. On next cryptkeeper entry, match \
            the password. If it matches fix the master \
            key and remove this flag. */

/* Allowed values for type in the structure below */
#define CRYPT_TYPE_PASSWORD                       \
    0 /* master_key is encrypted with a password  \
       * Must be zero to be compatible with pre-L \
       * devices where type is always password.*/
#define CRYPT_TYPE_DEFAULT                                           \
    1                        /* master_key is encrypted with default \
                              * password */
#define CRYPT_TYPE_PATTERN 2 /* master_key is encrypted with a pattern */
#define CRYPT_TYPE_PIN 3     /* master_key is encrypted with a pin */
#define CRYPT_TYPE_MAX_TYPE 3 /* type cannot be larger than this value */

#define CRYPT_MNT_MAGIC 0xD0B5B1C4
#define PERSIST_DATA_MAGIC 0xE950CD44

/* Key Derivation Function algorithms */
#define KDF_PBKDF2 1
#define KDF_SCRYPT 2
/* Algorithms 3 & 4 deprecated before shipping outside of google, so removed */
#define KDF_SCRYPT_KEYMASTER 5

/* Maximum allowed keymaster blob size. */
#define KEYMASTER_BLOB_SIZE 2048

/* __le32 and __le16 defined in system/extras/ext4_utils/ext4_utils.h */
#define __le8 unsigned char

#if !defined(SHA256_DIGEST_LENGTH)
#define SHA256_DIGEST_LENGTH 32
#endif

struct crypt_mnt_ftr {
    __le32 magic; /* See above */
    __le16 major_version;
    __le16 minor_version;
    __le32 ftr_size;             /* in bytes, not including key following */
    __le32 flags;                /* See above */
    __le32 keysize;              /* in bytes */
    __le32 crypt_type;           /* how master_key is encrypted. Must be a
                                  * CRYPT_TYPE_XXX value */
    __le64 fs_size;              /* Size of the encrypted fs, in 512 byte sectors */
    __le32 failed_decrypt_count; /* count of # of failed attempts to decrypt and
                                    mount, set to 0 on successful mount */
    unsigned char crypto_type_name[MAX_CRYPTO_TYPE_NAME_LEN]; /* The type of encryption
                                                                 needed to decrypt this
                                                                 partition, null terminated */
    __le32 spare2;                                            /* ignored */
    unsigned char master_key[MAX_KEY_LEN]; /* The encrypted key for decrypting the filesystem */
    unsigned char salt[SALT_LEN];          /* The salt used for this encryption */
    __le64 persist_data_offset[2];         /* Absolute offset to both copies of crypt_persist_data
                                            * on device with that info, either the footer of the
                                            * real_blkdevice or the metadata partition. */

    __le32 persist_data_size; /* The number of bytes allocated to each copy of the
                               * persistent data table*/

    __le8 kdf_type; /* The key derivation function used. */

    /* scrypt parameters. See www.tarsnap.com/scrypt/scrypt.pdf */
    __le8 N_factor;        /* (1 << N) */
    __le8 r_factor;        /* (1 << r) */
    __le8 p_factor;        /* (1 << p) */
    __le64 encrypted_upto; /* If we are in state CRYPT_ENCRYPTION_IN_PROGRESS and
                              we have to stop (e.g. power low) this is the last
                              encrypted 512 byte sector.*/
    __le8 hash_first_block[SHA256_DIGEST_LENGTH]; /* When CRYPT_ENCRYPTION_IN_PROGRESS
                                                     set, hash of first block, used
                                                     to validate before continuing*/

    /* key_master key, used to sign the derived key which is then used to generate
     * the intermediate key
     * This key should be used for no other purposes! We use this key to sign unpadded
     * data, which is acceptable but only if the key is not reused elsewhere. */
    __le8 keymaster_blob[KEYMASTER_BLOB_SIZE];
    __le32 keymaster_blob_size;

    /* Store scrypt of salted intermediate key. When decryption fails, we can
       check if this matches, and if it does, we know that the problem is with the
       drive, and there is no point in asking the user for more passwords.

       Note that if any part of this structure is corrupt, this will not match and
       we will continue to believe the user entered the wrong password. In that
       case the only solution is for the user to enter a password enough times to
       force a wipe.

       Note also that there is no need to worry about migration. If this data is
       wrong, we simply won't recognise a right password, and will continue to
       prompt. On the first password change, this value will be populated and
       then we will be OK.
     */
    unsigned char scrypted_intermediate_key[SCRYPT_LEN];

    /* sha of this structure with this element set to zero
       Used when encrypting on reboot to validate structure before doing something
       fatal
     */
    unsigned char sha256[SHA256_DIGEST_LENGTH];
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

#define DATA_MNT_POINT "/data"
#define METADATA_MNT_POINT "/metadata"

/* Return values for cryptfs_crypto_complete */
#define CRYPTO_COMPLETE_NOT_ENCRYPTED 1
#define CRYPTO_COMPLETE_ENCRYPTED 0
#define CRYPTO_COMPLETE_BAD_METADATA (-1)
#define CRYPTO_COMPLETE_PARTIAL (-2)
#define CRYPTO_COMPLETE_INCONSISTENT (-3)
#define CRYPTO_COMPLETE_CORRUPT (-4)

/* Return values for cryptfs_enable_inplace*() */
#define ENABLE_INPLACE_OK 0
#define ENABLE_INPLACE_ERR_OTHER (-1)
#define ENABLE_INPLACE_ERR_DEV (-2) /* crypto_blkdev issue */

/* Return values for cryptfs_getfield */
#define CRYPTO_GETFIELD_OK 0
#define CRYPTO_GETFIELD_ERROR_NO_FIELD (-1)
#define CRYPTO_GETFIELD_ERROR_OTHER (-2)
#define CRYPTO_GETFIELD_ERROR_BUF_TOO_SMALL (-3)

/* Return values for cryptfs_setfield */
#define CRYPTO_SETFIELD_OK 0
#define CRYPTO_SETFIELD_ERROR_OTHER (-1)
#define CRYPTO_SETFIELD_ERROR_FIELD_TOO_LONG (-2)
#define CRYPTO_SETFIELD_ERROR_VALUE_TOO_LONG (-3)

/* Return values for persist_del_key */
#define PERSIST_DEL_KEY_OK 0
#define PERSIST_DEL_KEY_ERROR_OTHER (-1)
#define PERSIST_DEL_KEY_ERROR_NO_FIELD (-2)

int match_multi_entry(const char* key, const char* field, unsigned index);
int wait_and_unmount(const char* mountpoint, bool kill);

typedef int (*kdf_func)(const char* passwd, const unsigned char* salt, unsigned char* ikey,
                        void* params);

int cryptfs_crypto_complete(void);
int cryptfs_check_passwd(const char* pw);
int cryptfs_verify_passwd(const char* pw);
int cryptfs_restart(void);
int cryptfs_enable(int type, const char* passwd, int no_ui);
int cryptfs_changepw(int type, const char* newpw);
int cryptfs_enable_default(int no_ui);
int cryptfs_setup_ext_volume(const char* label, const char* real_blkdev, const unsigned char* key,
                             char* out_crypto_blkdev);
int cryptfs_revert_ext_volume(const char* label);
int cryptfs_getfield(const char* fieldname, char* value, int len);
int cryptfs_setfield(const char* fieldname, const char* value);
int cryptfs_mount_default_encrypted(void);
int cryptfs_get_password_type(void);
const char* cryptfs_get_password(void);
void cryptfs_clear_password(void);
int cryptfs_isConvertibleToFBE(void);

uint32_t cryptfs_get_keysize();
const char* cryptfs_get_crypto_name();

#endif /* ANDROID_VOLD_CRYPTFS_H */
