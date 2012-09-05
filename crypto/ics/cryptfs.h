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
 * partition that is encrypted.
 * Immediately following this structure is the encrypted key.
 * The keysize field tells how long the key is, in bytes.
 * Then there is 32 bytes of padding,
 * Finally there is the salt used with the user password.
 * The salt is fixed at 16 bytes long.
 * Obviously, the filesystem does not include the last 16 kbytes
 * of the partition.
 */

#define CRYPT_FOOTER_OFFSET 0x4000

#define MAX_CRYPTO_TYPE_NAME_LEN 64

#define SALT_LEN 16
#define KEY_TO_SALT_PADDING 32

/* definitions of flags in the structure below */
#define CRYPT_MNT_KEY_UNENCRYPTED 0x1 /* The key for the partition is not encrypted. */
#define CRYPT_ENCRYPTION_IN_PROGRESS 0x2 /* Set when starting encryption,
                                          * clear when done before rebooting */

#define CRYPT_MNT_MAGIC 0xD0B5B1C4

#define __le32 unsigned int
#define __le16 unsigned short int 

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
#define VOL_NONREMOVABLE 0x1
#define VOL_ENCRYPTABLE  0x2

#ifdef __cplusplus
extern "C" {
#endif
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
#ifdef __cplusplus
}
#endif

