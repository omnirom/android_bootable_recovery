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
#include <openssl/sha.h>
#include <errno.h>
#include <ext4_utils/ext4_crypt.h>
#include <ext4_utils/ext4_utils.h>
#include <linux/kdev_t.h>
//#include <fs_mgr.h>
#include <time.h>
#include <math.h>
//#include <selinux/selinux.h>
#include "cryptfs.h"
//#include "secontext.h"
#define LOG_TAG "Cryptfs"
//#include "cutils/log.h"
#include "cutils/properties.h"
//#include "cutils/android_reboot.h"
//#include "hardware_legacy/power.h"
//#include <logwrap/logwrap.h>
//#include "ScryptParameters.h"
//#include "VolumeManager.h"
//#include "VoldUtil.h"
//#include "Ext4Crypt.h"
//#include "f2fs_sparseblock.h"
//#include "EncryptInplace.h"
//#include "Process.h"
#include "Keymaster3.h"
#include "android-base/properties.h"
//#include <bootloader_message/bootloader_message.h>
#ifdef CONFIG_HW_DISK_ENCRYPTION
#include <cryptfs_hw.h>
#endif
extern "C" {
#include <crypto_scrypt.h>
}

#define ALOGE(...) fprintf(stdout, "E:" __VA_ARGS__)
#define SLOGE(...) fprintf(stdout, "E:" __VA_ARGS__)
#define SLOGW(...) fprintf(stdout, "W:" __VA_ARGS__)
#define SLOGI(...) fprintf(stdout, "I:" __VA_ARGS__)
#define SLOGD(...) fprintf(stdout, "D:" __VA_ARGS__)

#define UNUSED __attribute__((unused))

#define DM_CRYPT_BUF_SIZE 4096

#define HASH_COUNT 2000

constexpr size_t INTERMEDIATE_KEY_LEN_BYTES = 16;
constexpr size_t INTERMEDIATE_IV_LEN_BYTES = 16;
constexpr size_t INTERMEDIATE_BUF_SIZE =
    (INTERMEDIATE_KEY_LEN_BYTES + INTERMEDIATE_IV_LEN_BYTES);

// SCRYPT_LEN is used by struct crypt_mnt_ftr for its intermediate key.
static_assert(INTERMEDIATE_BUF_SIZE == SCRYPT_LEN,
              "Mismatch of intermediate key sizes");

#define KEY_IN_FOOTER  "footer"

#define DEFAULT_HEX_PASSWORD "64656661756c745f70617373776f7264"
#define DEFAULT_PASSWORD "default_password"

#define CRYPTO_BLOCK_DEVICE "userdata"

#define BREADCRUMB_FILE "/data/misc/vold/convert_fde"

#define EXT4_FS 1
#define F2FS_FS 2

#define TABLE_LOAD_RETRIES 10

#define RSA_KEY_SIZE 2048
#define RSA_KEY_SIZE_BYTES (RSA_KEY_SIZE / 8)
#define RSA_EXPONENT 0x10001
#define KEYMASTER_CRYPTFS_RATE_LIMIT 1  // Maximum one try per second
#define KEY_LEN_BYTES 16

#define RETRY_MOUNT_ATTEMPTS 10
#define RETRY_MOUNT_DELAY_SECONDS 1

#define CREATE_CRYPTO_BLK_DEV_FLAGS_ALLOW_ENCRYPT_OVERRIDE (1)

static int put_crypt_ftr_and_key(struct crypt_mnt_ftr* crypt_ftr);

static unsigned char saved_master_key[MAX_KEY_LEN];
static char *saved_mount_point;
static int  master_key_saved = 0;
static struct crypt_persist_data *persist_data = NULL;

static int previous_type;

static char key_fname[PROPERTY_VALUE_MAX] = "";
static char real_blkdev[PROPERTY_VALUE_MAX] = "";
static char file_system[PROPERTY_VALUE_MAX] = "";

static void get_blkdev_size(int fd, unsigned long *nr_sec)
{
  if ( (ioctl(fd, BLKGETSIZE, nr_sec)) == -1) {
    *nr_sec = 0;
  }
}

#ifdef CONFIG_HW_DISK_ENCRYPTION
static int scrypt_keymaster(const char *passwd, const unsigned char *salt,
                            unsigned char *ikey, void *params);
static void convert_key_to_hex_ascii(const unsigned char *master_key,
                                     unsigned int keysize, char *master_key_ascii);
static int test_mount_hw_encrypted_fs(struct crypt_mnt_ftr* crypt_ftr,
                const char *passwd, const char *mount_point, const char *label);
int cryptfs_check_passwd_hw(char *passwd);
int cryptfs_get_master_key(struct crypt_mnt_ftr* ftr, const char* password,
                                   unsigned char* master_key);

static void convert_key_to_hex_ascii_for_upgrade(const unsigned char *master_key,
                                     unsigned int keysize, char *master_key_ascii)
{
    unsigned int i, a;
    unsigned char nibble;

    for (i = 0, a = 0; i < keysize; i++, a += 2) {
        /* For each byte, write out two ascii hex digits */
        nibble = (master_key[i] >> 4) & 0xf;
        master_key_ascii[a] = nibble + (nibble > 9 ? 0x57 : 0x30);

        nibble = master_key[i] & 0xf;
        master_key_ascii[a + 1] = nibble + (nibble > 9 ? 0x57 : 0x30);
    }

    /* Add the null termination */
    master_key_ascii[a] = '\0';
}

static int get_keymaster_hw_fde_passwd(const char* passwd, unsigned char* newpw,
                                  unsigned char* salt,
                                  const struct crypt_mnt_ftr *ftr)
{
    /* if newpw updated, return 0
     * if newpw not updated return -1
     */
    int rc = -1;

    if (should_use_keymaster()) {
        if (scrypt_keymaster(passwd, salt, newpw, (void*)ftr)) {
            SLOGE("scrypt failed");
        } else {
            rc = 0;
        }
    }

    return rc;
}

static int verify_hw_fde_passwd(const char *passwd, struct crypt_mnt_ftr* crypt_ftr)
{
    unsigned char newpw[32] = {0};
    int key_index;
    if (get_keymaster_hw_fde_passwd(passwd, newpw, crypt_ftr->salt, crypt_ftr))
        key_index = set_hw_device_encryption_key(passwd,
                                           (char*) crypt_ftr->crypto_type_name);
    else
        key_index = set_hw_device_encryption_key((const char*)newpw,
                                           (char*) crypt_ftr->crypto_type_name);
    return key_index;
}

static int verify_and_update_hw_fde_passwd(const char *passwd,
                                           struct crypt_mnt_ftr* crypt_ftr)
{
    return verify_hw_fde_passwd(passwd, crypt_ftr);
}
#endif

void set_partition_data(const char* block_device, const char* key_location, const char* fs)
{
  strcpy(key_fname, key_location);
  strcpy(real_blkdev, block_device);
  strcpy(file_system, fs);
}

/* Should we use keymaster? */
static int keymaster_check_compatibility()
{
    return keymaster_compatibility_cryptfs_scrypt();
}

/* Create a new keymaster key and store it in this footer */
static int keymaster_create_key(struct crypt_mnt_ftr *ftr)
{
    /*if (ftr->keymaster_blob_size) {
        SLOGI("Already have key");
        return 0;
    }

    int rc = keymaster_create_key_for_cryptfs_scrypt(RSA_KEY_SIZE, RSA_EXPONENT,
            KEYMASTER_CRYPTFS_RATE_LIMIT, ftr->keymaster_blob, KEYMASTER_BLOB_SIZE,
            &ftr->keymaster_blob_size);
    if (rc) {
        if (ftr->keymaster_blob_size > KEYMASTER_BLOB_SIZE) {
            SLOGE("Keymaster key blob too large");
            ftr->keymaster_blob_size = 0;
        }
        SLOGE("Failed to generate keypair");
        return -1;
    }
    return 0;*/
    return -1;
}

/* This signs the given object using the keymaster key. */
static int keymaster_sign_object(struct crypt_mnt_ftr *ftr,
                                 const unsigned char *object,
                                 const size_t object_size,
                                 unsigned char **signature,
                                 size_t *signature_size)
{
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
        case KDF_SCRYPT_KEYMASTER:
            // This ensures the most significant byte of the signed message
            // is zero.  We could have zero-padded to the left instead, but
            // this approach is slightly more robust against changes in
            // object size.  However, it's still broken (but not unusably
            // so) because we really should be using a proper deterministic
            // RSA padding function, such as PKCS1.
            memcpy(to_sign + 1, object, std::min((size_t)RSA_KEY_SIZE_BYTES - 1, object_size));
            SLOGI("Signing safely-padded object");
            break;
        default:
            SLOGE("Unknown KDF type %d", ftr->kdf_type);
            return -1;
    }
    for (;;) {
        auto result = keymaster_sign_object_for_cryptfs_scrypt(
            ftr->keymaster_blob, ftr->keymaster_blob_size, KEYMASTER_CRYPTFS_RATE_LIMIT, to_sign,
            to_sign_size, signature, signature_size);
        if (result == -1) {
			SLOGE("keymaster error!\n");
			return -1;
		}
        SLOGD("NOT Upgrading key %i\n", result);
        return 0;
        /*if (keymaster_upgrade_key_for_cryptfs_scrypt(
                RSA_KEY_SIZE, RSA_EXPONENT, KEYMASTER_CRYPTFS_RATE_LIMIT, ftr->keymaster_blob,
                ftr->keymaster_blob_size, ftr->keymaster_blob, KEYMASTER_BLOB_SIZE,
                &ftr->keymaster_blob_size) != 0) {
            SLOGE("Failed to upgrade key");
            return -1;
        }
        if (put_crypt_ftr_and_key(ftr) != 0) {
            SLOGE("Failed to write upgraded key to disk");
        }
        SLOGD("Key upgraded successfully");*/
    }
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

enum class RebootType {reboot, recovery, shutdown};
static void cryptfs_reboot(RebootType rt)
{
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
        strlcpy(io->name, name, sizeof(io->name));
    }
}

namespace {

struct CryptoType;

// Use to get the CryptoType in use on this device.
const CryptoType &get_crypto_type();

struct CryptoType {
    // We should only be constructing CryptoTypes as part of
    // supported_crypto_types[].  We do it via this pseudo-builder pattern,
    // which isn't pure or fully protected as a concession to being able to
    // do it all at compile time.  Add new CryptoTypes in
    // supported_crypto_types[] below.
    constexpr CryptoType() : CryptoType(nullptr, nullptr, 0xFFFFFFFF) {}
    constexpr CryptoType set_keysize(uint32_t size) const {
        return CryptoType(this->property_name, this->crypto_name, size);
    }
    constexpr CryptoType set_property_name(const char *property) const {
        return CryptoType(property, this->crypto_name, this->keysize);
    }
    constexpr CryptoType set_crypto_name(const char *crypto) const {
        return CryptoType(this->property_name, crypto, this->keysize);
    }

    constexpr const char *get_property_name() const { return property_name; }
    constexpr const char *get_crypto_name() const { return crypto_name; }
    constexpr uint32_t get_keysize() const { return keysize; }

 private:
    const char *property_name;
    const char *crypto_name;
    uint32_t keysize;

    constexpr CryptoType(const char *property, const char *crypto,
                         uint32_t ksize)
        : property_name(property), crypto_name(crypto), keysize(ksize) {}
    friend const CryptoType &get_crypto_type();
    static const CryptoType &get_device_crypto_algorithm();
};

// We only want to parse this read-only property once.  But we need to wait
// until the system is initialized before we can read it.  So we use a static
// scoped within this function to get it only once.
const CryptoType &get_crypto_type() {
    static CryptoType crypto_type = CryptoType::get_device_crypto_algorithm();
    return crypto_type;
}

constexpr CryptoType default_crypto_type = CryptoType()
    .set_property_name("AES-128-CBC")
    .set_crypto_name("aes-cbc-essiv:sha256")
    .set_keysize(16);

constexpr CryptoType supported_crypto_types[] = {
    default_crypto_type,
    CryptoType()
        .set_property_name("Speck128/128-XTS")
        .set_crypto_name("speck128-xts-plain64")
        .set_keysize(32),
    // Add new CryptoTypes here.  Order is not important.
};


// ---------- START COMPILE-TIME SANITY CHECK BLOCK -------------------------
// We confirm all supported_crypto_types have a small enough keysize and
// had both set_property_name() and set_crypto_name() called.

template <typename T, size_t N>
constexpr size_t array_length(T (&)[N]) { return N; }

constexpr bool indexOutOfBoundsForCryptoTypes(size_t index) {
    return (index >= array_length(supported_crypto_types));
}

constexpr bool isValidCryptoType(const CryptoType &crypto_type) {
    return ((crypto_type.get_property_name() != nullptr) &&
            (crypto_type.get_crypto_name() != nullptr) &&
            (crypto_type.get_keysize() <= MAX_KEY_LEN));
}

// Note in C++11 that constexpr functions can only have a single line.
// So our code is a bit convoluted (using recursion instead of a loop),
// but it's asserting at compile time that all of our key lengths are valid.
constexpr bool validateSupportedCryptoTypes(size_t index) {
    return indexOutOfBoundsForCryptoTypes(index) ||
        (isValidCryptoType(supported_crypto_types[index]) &&
         validateSupportedCryptoTypes(index + 1));
}

static_assert(validateSupportedCryptoTypes(0),
              "We have a CryptoType with keysize > MAX_KEY_LEN or which was "
              "incompletely constructed.");
//  ---------- END COMPILE-TIME SANITY CHECK BLOCK -------------------------


// Don't call this directly, use get_crypto_type(), which caches this result.
const CryptoType &CryptoType::get_device_crypto_algorithm() {
    constexpr char CRYPT_ALGO_PROP[] = "ro.crypto.fde_algorithm";
    char paramstr[PROPERTY_VALUE_MAX];

    property_get(CRYPT_ALGO_PROP, paramstr,
                 default_crypto_type.get_property_name());
    for (auto const &ctype : supported_crypto_types) {
        if (strcmp(paramstr, ctype.get_property_name()) == 0) {
            return ctype;
        }
    }
    ALOGE("Invalid name (%s) for %s.  Defaulting to %s\n", paramstr,
          CRYPT_ALGO_PROP, default_crypto_type.get_property_name());
    return default_crypto_type;
}

}  // namespace

#define SCRYPT_PROP "ro.crypto.scrypt_params"
#define SCRYPT_DEFAULTS "15:3:1"

bool parse_scrypt_parameters(const char* paramstr, int *Nf, int *rf, int *pf) {
    int params[3] = {};
    char *token;
    char *saveptr;
    int i;

    /*
     * The token we're looking for should be three integers separated by
     * colons (e.g., "12:8:1"). Scan the property to make sure it matches.
     */
    for (i = 0, token = strtok_r(const_cast<char *>(paramstr), ":", &saveptr);
            token != nullptr && i < 3;
            i++, token = strtok_r(nullptr, ":", &saveptr)) {
        char *endptr;
        params[i] = strtol(token, &endptr, 10);

        /*
         * Check that there was a valid number and it's 8-bit.
         */
        if ((*token == '\0') || (*endptr != '\0') || params[i] < 0 || params[i] > 255) {
            return false;
        }
    }
    if (token != nullptr) {
        return false;
    }
    *Nf = params[0]; *rf = params[1]; *pf = params[2];
    return true;
}

/**
 * Gets the default device scrypt parameters for key derivation time tuning.
 * The parameters should lead to about one second derivation time for the
 * given device.
 */
static void get_device_scrypt_params(struct crypt_mnt_ftr *ftr) {
    char paramstr[PROPERTY_VALUE_MAX];
    int Nf, rf, pf;

    property_get(SCRYPT_PROP, paramstr, SCRYPT_DEFAULTS);
    if (!parse_scrypt_parameters(paramstr, &Nf, &rf, &pf)) {
        SLOGW("bad scrypt parameters '%s' should be like '12:8:1'; using defaults", paramstr);
        parse_scrypt_parameters(SCRYPT_DEFAULTS, &Nf, &rf, &pf);
    }
    ftr->N_factor = Nf;
    ftr->r_factor = rf;
    ftr->p_factor = pf;
}

uint32_t cryptfs_get_keysize() {
    return get_crypto_type().get_keysize();
}

const char *cryptfs_get_crypto_name() {
    return get_crypto_type().get_crypto_name();
}

static unsigned int get_fs_size(char *dev)
{
    int fd, block_size;
    struct ext4_super_block sb;
    off64_t len;

    if ((fd = open(dev, O_RDONLY|O_CLOEXEC)) < 0) {
        SLOGE("Cannot open device to get filesystem size ");
        return 0;
    }

    if (lseek64(fd, 1024, SEEK_SET) < 0) {
        SLOGE("Cannot seek to superblock");
        return 0;
    }

    if (read(fd, &sb, sizeof(sb)) != sizeof(sb)) {
        SLOGE("Cannot read superblock");
        return 0;
    }

    close(fd);

    if (le32_to_cpu(sb.s_magic) != EXT4_SUPER_MAGIC) {
        SLOGE("Not a valid ext4 superblock");
        return 0;
    }
    block_size = 1024 << sb.s_log_block_size;
    /* compute length in bytes */
    len = ( ((off64_t)sb.s_blocks_count_hi << 32) + sb.s_blocks_count_lo) * block_size;

    /* return length in sectors */
    return (unsigned int) (len / 512);
}

static int get_crypt_ftr_info(char **metadata_fname, off64_t *off)
{
  static int cached_data = 0;
  static off64_t cached_off = 0;
  static char cached_metadata_fname[PROPERTY_VALUE_MAX] = "";
  int fd;
  //char key_loc[PROPERTY_VALUE_MAX];
  //char real_blkdev[PROPERTY_VALUE_MAX];
  int rc = -1;

  if (!cached_data) {
    //fs_mgr_get_crypt_info(fstab_default, key_loc, real_blkdev, sizeof(key_loc));

    if (!strcmp(key_fname, KEY_IN_FOOTER)) {
      if ( (fd = open(real_blkdev, O_RDWR|O_CLOEXEC)) < 0) {
        SLOGE("Cannot open real block device %s\n", real_blkdev);
        return -1;
      }

      unsigned long nr_sec = 0;
      get_blkdev_size(fd, &nr_sec);
      if (nr_sec != 0) {
        /* If it's an encrypted Android partition, the last 16 Kbytes contain the
         * encryption info footer and key, and plenty of bytes to spare for future
         * growth.
         */
        strlcpy(cached_metadata_fname, real_blkdev, sizeof(cached_metadata_fname));
        cached_off = ((off64_t)nr_sec * 512) - CRYPT_FOOTER_OFFSET;
        cached_data = 1;
      } else {
        SLOGE("Cannot get size of block device %s\n", real_blkdev);
      }
      close(fd);
    } else {
      strlcpy(cached_metadata_fname, key_fname, sizeof(cached_metadata_fname));
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

/* Set sha256 checksum in structure */
static void set_ftr_sha(struct crypt_mnt_ftr *crypt_ftr)
{
    SHA256_CTX c;
    SHA256_Init(&c);
    memset(crypt_ftr->sha256, 0, sizeof(crypt_ftr->sha256));
    SHA256_Update(&c, crypt_ftr, sizeof(*crypt_ftr));
    SHA256_Final(crypt_ftr->sha256, &c);
}

/* key or salt can be NULL, in which case just skip writing that value.  Useful to
 * update the failed mount count but not change the key.
 */
static int put_crypt_ftr_and_key(struct crypt_mnt_ftr *crypt_ftr)
{
  int fd;
  unsigned int cnt;
  /* starting_off is set to the SEEK_SET offset
   * where the crypto structure starts
   */
  return 0;
  off64_t starting_off;
  int rc = -1;
  char *fname = NULL;
  struct stat statbuf;

  set_ftr_sha(crypt_ftr);

  if (get_crypt_ftr_info(&fname, &starting_off)) {
    SLOGE("Unable to get crypt_ftr_info\n");
    return -1;
  }
  if (fname[0] != '/') {
    SLOGE("Unexpected value for crypto key location\n");
    return -1;
  }
  if ( (fd = open(fname, O_RDWR | O_CREAT|O_CLOEXEC, 0600)) < 0) {
    SLOGE("Cannot open footer file %s for put\n", fname);
    return -1;
  }

  /* Seek to the start of the crypt footer */
  if (lseek64(fd, starting_off, SEEK_SET) == -1) {
    SLOGE("Cannot seek to real block device footer\n");
    goto errout;
  }

  if ((cnt = write(fd, crypt_ftr, sizeof(struct crypt_mnt_ftr))) != sizeof(struct crypt_mnt_ftr)) {
    SLOGE("Cannot write real block device footer\n");
    goto errout;
  }

  fstat(fd, &statbuf);
  /* If the keys are kept on a raw block device, do not try to truncate it. */
  if (S_ISREG(statbuf.st_mode)) {
    if (ftruncate(fd, 0x4000)) {
      SLOGE("Cannot set footer file size\n");
      goto errout;
    }
  }

  /* Success! */
  rc = 0;

errout:
  close(fd);
  return rc;

}

static bool check_ftr_sha(const struct crypt_mnt_ftr *crypt_ftr)
{
    struct crypt_mnt_ftr copy;
    memcpy(&copy, crypt_ftr, sizeof(copy));
    set_ftr_sha(&copy);
    return memcmp(copy.sha256, crypt_ftr->sha256, sizeof(copy.sha256)) == 0;
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
    return;

    if ((crypt_ftr->major_version == 1) && (crypt_ftr->minor_version == 0)) {
        struct crypt_persist_data *pdata;
        off64_t pdata_offset = offset + CRYPT_FOOTER_TO_PERSIST_OFFSET;

        SLOGW("upgrading crypto footer to 1.1");

        pdata = (crypt_persist_data *)malloc(CRYPT_PERSIST_DATA_SIZE);
        if (pdata == NULL) {
            SLOGE("Cannot allocate persisent data\n");
            return;
        }
        memset(pdata, 0, CRYPT_PERSIST_DATA_SIZE);

        /* Need to initialize the persistent data area */
        if (lseek64(fd, pdata_offset, SEEK_SET) == -1) {
            SLOGE("Cannot seek to persisent data offset\n");
            free(pdata);
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
        free(pdata);
    }

    if ((crypt_ftr->major_version == 1) && (crypt_ftr->minor_version == 1)) {
        SLOGW("upgrading crypto footer to 1.2");
        /* But keep the old kdf_type.
         * It will get updated later to KDF_SCRYPT after the password has been verified.
         */
        crypt_ftr->kdf_type = KDF_PBKDF2;
        get_device_scrypt_params(crypt_ftr);
        crypt_ftr->minor_version = 2;
    }

    if ((crypt_ftr->major_version == 1) && (crypt_ftr->minor_version == 2)) {
        SLOGW("upgrading crypto footer to 1.3");
        crypt_ftr->crypt_type = CRYPT_TYPE_PASSWORD;
        crypt_ftr->minor_version = 3;
    }

    if ((orig_major != crypt_ftr->major_version) || (orig_minor != crypt_ftr->minor_version)) {
        if (lseek64(fd, offset, SEEK_SET) == -1) {
            SLOGE("Cannot seek to crypt footer\n");
            return;
        }
        unix_write(fd, crypt_ftr, sizeof(struct crypt_mnt_ftr));
    }
}


static int get_crypt_ftr_and_key(struct crypt_mnt_ftr *crypt_ftr)
{
  int fd;
  unsigned int cnt;
  off64_t starting_off;
  int rc = -1;
  char *fname = NULL;
  struct stat statbuf;

  if (get_crypt_ftr_info(&fname, &starting_off)) {
    SLOGE("Unable to get crypt_ftr_info\n");
    return -1;
  }
  if (fname[0] != '/') {
    SLOGE("Unexpected value for crypto key location\n");
    return -1;
  }
  if ( (fd = open(fname, O_RDWR|O_CLOEXEC)) < 0) {
    SLOGE("Cannot open footer file %s for get\n", fname);
    return -1;
  }

  /* Make sure it's 16 Kbytes in length */
  fstat(fd, &statbuf);
  if (S_ISREG(statbuf.st_mode) && (statbuf.st_size != 0x4000)) {
    SLOGE("footer file %s is not the expected size!\n", fname);
    goto errout;
  }

  /* Seek to the start of the crypt footer */
  if (lseek64(fd, starting_off, SEEK_SET) == -1) {
    SLOGE("Cannot seek to real block device footer\n");
    goto errout;
  }

  if ( (cnt = read(fd, crypt_ftr, sizeof(struct crypt_mnt_ftr))) != sizeof(struct crypt_mnt_ftr)) {
    SLOGE("Cannot read real block device footer\n");
    goto errout;
  }

  if (crypt_ftr->magic != CRYPT_MNT_MAGIC) {
    SLOGE("Bad magic for real block device %s\n", fname);
    goto errout;
  }

  if (crypt_ftr->major_version != CURRENT_MAJOR_VERSION) {
    SLOGE("Cannot understand major version %d real block device footer; expected %d\n",
          crypt_ftr->major_version, CURRENT_MAJOR_VERSION);
    goto errout;
  }

  // We risk buffer overflows with oversized keys, so we just reject them.
  // 0-sized keys are problematic (essentially by-passing encryption), and
  // AES-CBC key wrapping only works for multiples of 16 bytes.
  if ((crypt_ftr->keysize == 0) || ((crypt_ftr->keysize % 16) != 0) ||
      (crypt_ftr->keysize > MAX_KEY_LEN)) {
    SLOGE("Invalid keysize (%u) for block device %s; Must be non-zero, "
          "divisible by 16, and <= %d\n", crypt_ftr->keysize, fname,
          MAX_KEY_LEN);
    goto errout;
  }

  if (crypt_ftr->minor_version > CURRENT_MINOR_VERSION) {
    SLOGW("Warning: crypto footer minor version %d, expected <= %d, continuing...\n",
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

int cryptfs_check_footer()
{
    int rc = -1;
    struct crypt_mnt_ftr crypt_ftr;

    rc = get_crypt_ftr_and_key(&crypt_ftr);

    return rc;
}

static int validate_persistent_data_storage(struct crypt_mnt_ftr *crypt_ftr)
{
    if (crypt_ftr->persist_data_offset[0] + crypt_ftr->persist_data_size >
        crypt_ftr->persist_data_offset[1]) {
        SLOGE("Crypt_ftr persist data regions overlap");
        return -1;
    }

    if (crypt_ftr->persist_data_offset[0] >= crypt_ftr->persist_data_offset[1]) {
        SLOGE("Crypt_ftr persist data region 0 starts after region 1");
        return -1;
    }

    if (((crypt_ftr->persist_data_offset[1] + crypt_ftr->persist_data_size) -
        (crypt_ftr->persist_data_offset[0] - CRYPT_FOOTER_TO_PERSIST_OFFSET)) >
        CRYPT_FOOTER_OFFSET) {
        SLOGE("Persistent data extends past crypto footer");
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
        pdata = (crypt_persist_data*)malloc(CRYPT_PERSIST_DATA_SIZE);
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
        SLOGE("Crypt_ftr version doesn't support persistent data");
        return -1;
    }

    if (get_crypt_ftr_info(&fname, NULL)) {
        return -1;
    }

    ret = validate_persistent_data_storage(&crypt_ftr);
    if (ret) {
        return -1;
    }

    fd = open(fname, O_RDONLY|O_CLOEXEC);
    if (fd < 0) {
        SLOGE("Cannot open %s metadata file", fname);
        return -1;
    }

    pdata = (crypt_persist_data*)malloc(crypt_ftr.persist_data_size);
    if (pdata == NULL) {
        SLOGE("Cannot allocate memory for persistent data");
        goto err;
    }

    for (i = 0; i < 2; i++) {
        if (lseek64(fd, crypt_ftr.persist_data_offset[i], SEEK_SET) < 0) {
            SLOGE("Cannot seek to read persistent data on %s", fname);
            goto err2;
        }
        if (unix_read(fd, pdata, crypt_ftr.persist_data_size) < 0){
            SLOGE("Error reading persistent data on iteration %d", i);
            goto err2;
        }
        if (pdata->persist_magic == PERSIST_DATA_MAGIC) {
            found = 1;
            break;
        }
    }

    if (!found) {
        SLOGI("Could not find valid persistent data, creating");
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
    int fd;
    int ret;

    if (persist_data == NULL) {
        SLOGE("No persistent data to save");
        return -1;
    }

    if(get_crypt_ftr_and_key(&crypt_ftr)) {
        return -1;
    }

    if ((crypt_ftr.major_version < 1)
        || (crypt_ftr.major_version == 1 && crypt_ftr.minor_version < 1)) {
        SLOGE("Crypt_ftr version doesn't support persistent data");
        return -1;
    }

    ret = validate_persistent_data_storage(&crypt_ftr);
    if (ret) {
        return -1;
    }

    if (get_crypt_ftr_info(&fname, NULL)) {
        return -1;
    }

    fd = open(fname, O_RDWR|O_CLOEXEC);
    if (fd < 0) {
        SLOGE("Cannot open %s metadata file", fname);
        return -1;
    }

    pdata = (crypt_persist_data*)malloc(crypt_ftr.persist_data_size);
    if (pdata == NULL) {
        SLOGE("Cannot allocate persistant data");
        goto err;
    }

    if (lseek64(fd, crypt_ftr.persist_data_offset[0], SEEK_SET) < 0) {
        SLOGE("Cannot seek to read persistent data on %s", fname);
        goto err2;
    }

    if (unix_read(fd, pdata, crypt_ftr.persist_data_size) < 0) {
            SLOGE("Error reading persistent data before save");
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
    if (lseek64(fd, write_offset, SEEK_SET) < 0) {
        SLOGE("Cannot seek to write persistent data");
        goto err2;
    }
    if (unix_write(fd, persist_data, crypt_ftr.persist_data_size) ==
        (int) crypt_ftr.persist_data_size) {
        if (lseek64(fd, erase_offset, SEEK_SET) < 0) {
            SLOGE("Cannot seek to erase previous persistent data");
            goto err2;
        }
        fsync(fd);
        memset(pdata, 0, crypt_ftr.persist_data_size);
        if (unix_write(fd, pdata, crypt_ftr.persist_data_size) !=
            (int) crypt_ftr.persist_data_size) {
            SLOGE("Cannot write to erase previous persistent data");
            goto err2;
        }
        fsync(fd);
    } else {
        SLOGE("Cannot write to save persistent data");
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

/* Convert a binary key of specified length into an ascii hex string equivalent,
 * without the leading 0x and with null termination
 */
static void convert_key_to_hex_ascii(const unsigned char *master_key,
                                     unsigned int keysize, char *master_key_ascii) {
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

static int load_crypto_mapping_table(struct crypt_mnt_ftr *crypt_ftr,
        const unsigned char *master_key, const char *real_blk_name,
        const char *name, int fd, const char *extra_params) {
  alignas(struct dm_ioctl) char buffer[DM_CRYPT_BUF_SIZE];
  struct dm_ioctl *io;
  struct dm_target_spec *tgt;
  char *crypt_params;
  // We need two ASCII characters to represent each byte, and need space for
  // the '\0' terminator.
  char master_key_ascii[MAX_KEY_LEN * 2 + 1];
  size_t buff_offset;
  int i;

  io = (struct dm_ioctl *) buffer;

  /* Load the mapping table for this device */
  tgt = (struct dm_target_spec *) &buffer[sizeof(struct dm_ioctl)];

  ioctl_init(io, DM_CRYPT_BUF_SIZE, name, 0);
  io->target_count = 1;
  tgt->status = 0;
  tgt->sector_start = 0;
  tgt->length = crypt_ftr->fs_size;
  crypt_params = buffer + sizeof(struct dm_ioctl) + sizeof(struct dm_target_spec);
  buff_offset = crypt_params - buffer;
  SLOGI("Extra parameters for dm_crypt: %s\n", extra_params);

#ifdef CONFIG_HW_DISK_ENCRYPTION
  if(is_hw_disk_encryption((char*)crypt_ftr->crypto_type_name)) {
    strlcpy(tgt->target_type, "req-crypt",DM_MAX_TYPE_NAME);
    if (is_ice_enabled())
      convert_key_to_hex_ascii(master_key, sizeof(int), master_key_ascii);
    else
      convert_key_to_hex_ascii(master_key, crypt_ftr->keysize, master_key_ascii);
  }
  else {
    convert_key_to_hex_ascii(master_key, crypt_ftr->keysize, master_key_ascii);
    strlcpy(tgt->target_type, "crypt", DM_MAX_TYPE_NAME);
  }
  snprintf(crypt_params, sizeof(buffer) - buff_offset, "%s %s 0 %s 0 %s 0",
           crypt_ftr->crypto_type_name, master_key_ascii,
           real_blk_name, extra_params);

  SLOGI("target_type = %s", tgt->target_type);
  SLOGI("real_blk_name = %s, extra_params = %s", real_blk_name, extra_params);
#else
  convert_key_to_hex_ascii(master_key, crypt_ftr->keysize, master_key_ascii);
  strlcpy(tgt->target_type, "crypt", DM_MAX_TYPE_NAME);
  snprintf(crypt_params, sizeof(buffer) - buff_offset, "%s %s 0 %s 0 %s",
           crypt_ftr->crypto_type_name, master_key_ascii, real_blk_name,
           extra_params);
#endif

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
#ifdef CONFIG_HW_DISK_ENCRYPTION
        if (! strcmp(v->name, "crypt") || ! strcmp(v->name, "req-crypt")) {
#else
        if (! strcmp(v->name, "crypt")) {
#endif
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

#ifndef CONFIG_HW_DISK_ENCRYPTION
static std::string extra_params_as_string(const std::vector<std::string>& extra_params_vec) {
    if (extra_params_vec.empty()) return "";
    std::string extra_params = std::to_string(extra_params_vec.size());
    for (const auto& p : extra_params_vec) {
        extra_params.append(" ");
        extra_params.append(p);
    }
    return extra_params;
}
#endif

static int create_crypto_blk_dev(struct crypt_mnt_ftr* crypt_ftr, const unsigned char* master_key,
                                 const char* real_blk_name, char* crypto_blk_name, const char* name,
                                 uint32_t flags) {
    char buffer[DM_CRYPT_BUF_SIZE];
    struct dm_ioctl* io;
    unsigned int minor;
    int fd = 0;
    int err;
    int retval = -1;
    int version[3];
    int load_count;
#ifdef CONFIG_HW_DISK_ENCRYPTION
    char encrypted_state[PROPERTY_VALUE_MAX] = {0};
    char progress[PROPERTY_VALUE_MAX] = {0};
    const char *extra_params;
#else
    std::vector<std::string> extra_params_vec;
#endif

    if ((fd = open("/dev/device-mapper", O_RDWR | O_CLOEXEC)) < 0) {
        SLOGE("Cannot open device-mapper\n");
        goto errout;
    }

    io = (struct dm_ioctl*)buffer;

    ioctl_init(io, DM_CRYPT_BUF_SIZE, name, 0);
    err = ioctl(fd, DM_DEV_CREATE, io);
    if (err) {
        SLOGE("Cannot create dm-crypt device %s: %s\n", name, strerror(errno));
        goto errout;
    }

    /* Get the device status, in particular, the name of it's device file */
    ioctl_init(io, DM_CRYPT_BUF_SIZE, name, 0);
    if (ioctl(fd, DM_DEV_STATUS, io)) {
        SLOGE("Cannot retrieve dm-crypt device status\n");
        goto errout;
    }
    minor = (io->dev & 0xff) | ((io->dev >> 12) & 0xfff00);
    snprintf(crypto_blk_name, MAXPATHLEN, "/dev/block/dm-%u", minor);

#ifdef CONFIG_HW_DISK_ENCRYPTION
    if(is_hw_disk_encryption((char*)crypt_ftr->crypto_type_name)) {
      /* Set fde_enabled if either FDE completed or in-progress */
      property_get("ro.crypto.state", encrypted_state, ""); /* FDE completed */
      property_get("vold.encrypt_progress", progress, ""); /* FDE in progress */
      if (!strcmp(encrypted_state, "encrypted") || strcmp(progress, "")) {
        if (is_ice_enabled()) {
          if (flags & CREATE_CRYPTO_BLK_DEV_FLAGS_ALLOW_ENCRYPT_OVERRIDE)
            extra_params = "fde_enabled ice allow_encrypt_override";
          else
            extra_params = "fde_enabled ice";
        } else {
          if (flags & CREATE_CRYPTO_BLK_DEV_FLAGS_ALLOW_ENCRYPT_OVERRIDE)
            extra_params = "fde_enabled allow_encrypt_override";
          else
            extra_params = "fde_enabled";
        }
      } else {
          if (flags & CREATE_CRYPTO_BLK_DEV_FLAGS_ALLOW_ENCRYPT_OVERRIDE)
            extra_params = "fde_enabled allow_encrypt_override";
          else
            extra_params = "fde_enabled";
      }
    } else {
      extra_params = "";
      if (! get_dm_crypt_version(fd, name, version)) {
        /* Support for allow_discards was added in version 1.11.0 */
        if ((version[0] >= 2) || ((version[0] == 1) && (version[1] >= 11))) {
          if (flags & CREATE_CRYPTO_BLK_DEV_FLAGS_ALLOW_ENCRYPT_OVERRIDE)
            extra_params = "2 allow_discards allow_encrypt_override";
          else
            extra_params = "1 allow_discards";
          SLOGI("Enabling support for allow_discards in dmcrypt.\n");
        }
      }
    }
    load_count = load_crypto_mapping_table(crypt_ftr, master_key, real_blk_name, name, fd,
                                           extra_params);
#else
    if (!get_dm_crypt_version(fd, name, version)) {
        /* Support for allow_discards was added in version 1.11.0 */
        if ((version[0] >= 2) || ((version[0] == 1) && (version[1] >= 11))) {
            extra_params_vec.emplace_back("allow_discards");
        }
    }
    if (flags & CREATE_CRYPTO_BLK_DEV_FLAGS_ALLOW_ENCRYPT_OVERRIDE) {
        extra_params_vec.emplace_back("allow_encrypt_override");
    }
    load_count = load_crypto_mapping_table(crypt_ftr, master_key, real_blk_name, name, fd,
                                           extra_params_as_string(extra_params_vec).c_str());
#endif
    if (load_count < 0) {
        SLOGE("Cannot load dm-crypt mapping table.\n");
        goto errout;
    } else if (load_count > 1) {
        SLOGI("Took %d tries to load dmcrypt table.\n", load_count);
    }

    /* Resume this device to activate it */
    ioctl_init(io, DM_CRYPT_BUF_SIZE, name, 0);

    if (ioctl(fd, DM_DEV_SUSPEND, io)) {
        SLOGE("Cannot resume the dm-crypt device\n");
        goto errout;
    }

    /* We made it here with no errors.  Woot! */
    retval = 0;

errout:
  close(fd);   /* If fd is <0 from a failed open call, it's safe to just ignore the close error */

  return retval;
}

int delete_crypto_blk_dev(const char *name)
{
  int fd;
  char buffer[DM_CRYPT_BUF_SIZE];
  struct dm_ioctl *io;
  int retval = -1;

  if ((fd = open("/dev/device-mapper", O_RDWR|O_CLOEXEC)) < 0 ) {
    SLOGE("Cannot open device-mapper\n");
    goto errout;
  }

  io = (struct dm_ioctl *) buffer;

  ioctl_init(io, DM_CRYPT_BUF_SIZE, name, 0);
  if (ioctl(fd, DM_DEV_REMOVE, io)) {
    SLOGE("Cannot remove dm-crypt device\n");
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
    SLOGI("Using pbkdf2 for cryptfs KDF");

    /* Turn the password into a key and IV that can decrypt the master key */
    return PKCS5_PBKDF2_HMAC_SHA1(passwd, strlen(passwd), salt, SALT_LEN,
                                  HASH_COUNT, INTERMEDIATE_BUF_SIZE,
                                  ikey) != 1;
}

static int scrypt(const char *passwd, const unsigned char *salt,
                  unsigned char *ikey, void *params)
{
    SLOGI("Using scrypt for cryptfs KDF");

    struct crypt_mnt_ftr *ftr = (struct crypt_mnt_ftr *) params;

    int N = 1 << ftr->N_factor;
    int r = 1 << ftr->r_factor;
    int p = 1 << ftr->p_factor;

    /* Turn the password into a key and IV that can decrypt the master key */
    crypto_scrypt((const uint8_t*)passwd, strlen(passwd),
                  salt, SALT_LEN, N, r, p, ikey,
                  INTERMEDIATE_BUF_SIZE);

   return 0;
}

static int scrypt_keymaster(const char *passwd, const unsigned char *salt,
                            unsigned char *ikey, void *params)
{
    SLOGI("Using scrypt with keymaster for cryptfs KDF");

    int rc;
    size_t signature_size;
    unsigned char* signature;
    struct crypt_mnt_ftr *ftr = (struct crypt_mnt_ftr *) params;

    int N = 1 << ftr->N_factor;
    int r = 1 << ftr->r_factor;
    int p = 1 << ftr->p_factor;

    rc = crypto_scrypt((const uint8_t*)passwd, strlen(passwd),
                       salt, SALT_LEN, N, r, p, ikey,
                       INTERMEDIATE_BUF_SIZE);

    if (rc) {
        SLOGE("scrypt failed");
        return -1;
    }

    if (keymaster_sign_object(ftr, ikey, INTERMEDIATE_BUF_SIZE,
                              &signature, &signature_size)) {
        SLOGE("Signing failed");
        return -1;
    }

    rc = crypto_scrypt(signature, signature_size, salt, SALT_LEN,
                       N, r, p, ikey, INTERMEDIATE_BUF_SIZE);
    free(signature);

    if (rc) {
        SLOGE("scrypt failed");
        return -1;
    }

    return 0;
}

static int encrypt_master_key(const char *passwd, const unsigned char *salt,
                              const unsigned char *decrypted_master_key,
                              unsigned char *encrypted_master_key,
                              struct crypt_mnt_ftr *crypt_ftr,
                              bool create_keymaster_key)
{
    unsigned char ikey[INTERMEDIATE_BUF_SIZE] = { 0 };
    EVP_CIPHER_CTX e_ctx;
    int encrypted_len, final_len;
    int rc = 0;

    /* Turn the password into an intermediate key and IV that can decrypt the master key */
    get_device_scrypt_params(crypt_ftr);

    switch (crypt_ftr->kdf_type) {
    case KDF_SCRYPT_KEYMASTER:
        if (create_keymaster_key && keymaster_create_key(crypt_ftr)) {
            SLOGE("keymaster_create_key failed");
            return -1;
        }

        if (scrypt_keymaster(passwd, salt, ikey, crypt_ftr)) {
            SLOGE("scrypt failed");
            return -1;
        }
        break;

    case KDF_SCRYPT:
        if (scrypt(passwd, salt, ikey, crypt_ftr)) {
            SLOGE("scrypt failed");
            return -1;
        }
        break;

    default:
        SLOGE("Invalid kdf_type");
        return -1;
    }

    /* Initialize the decryption engine */
    EVP_CIPHER_CTX_init(&e_ctx);
    if (! EVP_EncryptInit_ex(&e_ctx, EVP_aes_128_cbc(), NULL, ikey,
                             ikey+INTERMEDIATE_KEY_LEN_BYTES)) {
        SLOGE("EVP_EncryptInit failed\n");
        return -1;
    }
    EVP_CIPHER_CTX_set_padding(&e_ctx, 0); /* Turn off padding as our data is block aligned */

    /* Encrypt the master key */
    if (! EVP_EncryptUpdate(&e_ctx, encrypted_master_key, &encrypted_len,
                            decrypted_master_key, crypt_ftr->keysize)) {
        SLOGE("EVP_EncryptUpdate failed\n");
        return -1;
    }
    if (! EVP_EncryptFinal_ex(&e_ctx, encrypted_master_key + encrypted_len, &final_len)) {
        SLOGE("EVP_EncryptFinal failed\n");
        return -1;
    }

    if (encrypted_len + final_len != static_cast<int>(crypt_ftr->keysize)) {
        SLOGE("EVP_Encryption length check failed with %d, %d bytes\n", encrypted_len, final_len);
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

    rc = crypto_scrypt(ikey, INTERMEDIATE_KEY_LEN_BYTES,
                       crypt_ftr->salt, sizeof(crypt_ftr->salt), N, r, p,
                       crypt_ftr->scrypted_intermediate_key,
                       sizeof(crypt_ftr->scrypted_intermediate_key));

    if (rc) {
      SLOGE("encrypt_master_key: crypto_scrypt failed");
    }

    EVP_CIPHER_CTX_cleanup(&e_ctx);

    return 0;
}

static int decrypt_master_key_aux(const char *passwd, unsigned char *salt,
                                  const unsigned char *encrypted_master_key,
                                  size_t keysize,
                                  unsigned char *decrypted_master_key,
                                  kdf_func kdf, void *kdf_params,
                                  unsigned char** intermediate_key,
                                  size_t* intermediate_key_size)
{
  unsigned char ikey[INTERMEDIATE_BUF_SIZE] = { 0 };
  EVP_CIPHER_CTX d_ctx;
  int decrypted_len, final_len;

  /* Turn the password into an intermediate key and IV that can decrypt the
     master key */
  if (kdf(passwd, salt, ikey, kdf_params)) {
    SLOGE("kdf failed");
    return -1;
  }

  /* Initialize the decryption engine */
  EVP_CIPHER_CTX_init(&d_ctx);
  if (! EVP_DecryptInit_ex(&d_ctx, EVP_aes_128_cbc(), NULL, ikey, ikey+INTERMEDIATE_KEY_LEN_BYTES)) {
    return -1;
  }
  EVP_CIPHER_CTX_set_padding(&d_ctx, 0); /* Turn off padding as our data is block aligned */
  /* Decrypt the master key */
  if (! EVP_DecryptUpdate(&d_ctx, decrypted_master_key, &decrypted_len,
                            encrypted_master_key, keysize)) {
    return -1;
  }
  if (! EVP_DecryptFinal_ex(&d_ctx, decrypted_master_key + decrypted_len, &final_len)) {
    return -1;
  }

  if (decrypted_len + final_len != static_cast<int>(keysize)) {
    return -1;
  }

  /* Copy intermediate key if needed by params */
  if (intermediate_key && intermediate_key_size) {
    *intermediate_key = (unsigned char*) malloc(INTERMEDIATE_KEY_LEN_BYTES);
    if (*intermediate_key) {
      memcpy(*intermediate_key, ikey, INTERMEDIATE_KEY_LEN_BYTES);
      *intermediate_key_size = INTERMEDIATE_KEY_LEN_BYTES;
    }
  }

  EVP_CIPHER_CTX_cleanup(&d_ctx);

  return 0;
}

static void get_kdf_func(struct crypt_mnt_ftr *ftr, kdf_func *kdf, void** kdf_params)
{
    if (ftr->kdf_type == KDF_SCRYPT_KEYMASTER) {
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

static int decrypt_master_key(const char *passwd, unsigned char *decrypted_master_key,
                              struct crypt_mnt_ftr *crypt_ftr,
                              unsigned char** intermediate_key,
                              size_t* intermediate_key_size)
{
    kdf_func kdf;
    void *kdf_params;
    int ret;

    get_kdf_func(crypt_ftr, &kdf, &kdf_params);
    ret = decrypt_master_key_aux(passwd, crypt_ftr->salt, crypt_ftr->master_key,
                                 crypt_ftr->keysize,
                                 decrypted_master_key, kdf, kdf_params,
                                 intermediate_key, intermediate_key_size);
    if (ret != 0) {
        SLOGW("failure decrypting master key");
    }

    return ret;
}

static int create_encrypted_random_key(const char *passwd, unsigned char *master_key, unsigned char *salt,
        struct crypt_mnt_ftr *crypt_ftr) {
    int fd;
    unsigned char key_buf[MAX_KEY_LEN];

    /* Get some random bits for a key */
    fd = open("/dev/urandom", O_RDONLY|O_CLOEXEC);
    read(fd, key_buf, sizeof(key_buf));
    read(fd, salt, SALT_LEN);
    close(fd);

    /* Now encrypt it with the password */
    return encrypt_master_key(passwd, salt, key_buf, master_key, crypt_ftr, true);
}

int wait_and_unmount(const char *mountpoint, bool kill)
{
    int i, err, rc;
#define WAIT_UNMOUNT_COUNT 200

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

        /* If allowed, be increasingly aggressive before the last 2 seconds */
        if (kill) {
            if (i == (WAIT_UNMOUNT_COUNT - 30)) {
                SLOGW("sending SIGHUP to processes with open files\n");
                //android::vold::KillProcessesWithOpenFiles(mountpoint, SIGTERM);
            } else if (i == (WAIT_UNMOUNT_COUNT - 20)) {
                SLOGW("sending SIGKILL to processes with open files\n");
                //android::vold::KillProcessesWithOpenFiles(mountpoint, SIGKILL);
            }
        }

        usleep(100000);
    }

    if (i < WAIT_UNMOUNT_COUNT) {
      SLOGD("unmounting %s succeeded\n", mountpoint);
      rc = 0;
    } else {
      //android::vold::KillProcessesWithOpenFiles(mountpoint, 0);
      SLOGE("unmounting %s failed: %s\n", mountpoint, strerror(err));
      rc = -1;
    }

    return rc;
}

static void prep_data_fs(void)
{
    // NOTE: post_fs_data results in init calling back around to vold, so all
    // callers to this method must be async

    /* Do the prep of the /data filesystem */
    property_set("vold.post_fs_data_done", "0");
    property_set("vold.decrypt", "trigger_post_fs_data");
    SLOGD("Just triggered post_fs_data");

    /* Wait a max of 50 seconds, hopefully it takes much less */
    while (!android::base::WaitForProperty("vold.post_fs_data_done",
                                        "1",
                                        std::chrono::seconds(15))) {
        /* We timed out to prep /data in time.  Continue wait. */
        SLOGE("waited 15s for vold.post_fs_data_done, still waiting...");
    }
    SLOGD("post_fs_data done");
}

static void cryptfs_set_corrupt()
{
    // Mark the footer as bad
    struct crypt_mnt_ftr crypt_ftr;
    if (get_crypt_ftr_and_key(&crypt_ftr)) {
        SLOGE("Failed to get crypto footer - panic");
        return;
    }

    crypt_ftr.flags |= CRYPT_DATA_CORRUPT;
    if (put_crypt_ftr_and_key(&crypt_ftr)) {
        SLOGE("Failed to set crypto footer - panic");
        return;
    }
}

static void cryptfs_trigger_restart_min_framework()
{
    /*if (fs_mgr_do_tmpfs_mount(DATA_MNT_POINT)) {
      SLOGE("Failed to mount tmpfs on data - panic");
      return;
    }

    if (property_set("vold.decrypt", "trigger_post_fs_data")) {
        SLOGE("Failed to trigger post fs data - panic");
        return;
    }

    if (property_set("vold.decrypt", "trigger_restart_min_framework")) {
        SLOGE("Failed to trigger restart min framework - panic");
        return;
    }*/
}

/* returns < 0 on failure */
static int cryptfs_restart_internal(int restart_main)
{
    return -1;
}

int cryptfs_restart(void)
{
    SLOGI("cryptfs_restart");
    /*if (e4crypt_is_native()) {
        SLOGE("cryptfs_restart not valid for file encryption:");
        return -1;
    }*/

    /* Call internal implementation forcing a restart of main service group */
    return cryptfs_restart_internal(1);
}

static int do_crypto_complete(const char *mount_point)
{
  return CRYPTO_COMPLETE_ENCRYPTED;
}

#ifdef CONFIG_HW_DISK_ENCRYPTION
static int test_mount_hw_encrypted_fs(struct crypt_mnt_ftr* crypt_ftr,
             const char *passwd, const char *mount_point, const char *label)
{
  /* Allocate enough space for a 256 bit key, but we may use less */
  unsigned char decrypted_master_key[32];
  char crypto_blkdev[MAXPATHLEN];
  //char real_blkdev[MAXPATHLEN];
  unsigned int orig_failed_decrypt_count;
  int rc = 0;

  SLOGD("crypt_ftr->fs_size = %lld\n", crypt_ftr->fs_size);
  orig_failed_decrypt_count = crypt_ftr->failed_decrypt_count;

  //fs_mgr_get_crypt_info(fstab_default, 0, real_blkdev, sizeof(real_blkdev));

  int key_index = 0;
  if(is_hw_disk_encryption((char*)crypt_ftr->crypto_type_name)) {
    key_index = verify_and_update_hw_fde_passwd(passwd, crypt_ftr);
    if (key_index < 0) {
      rc = crypt_ftr->failed_decrypt_count;
      goto errout;
    }
    else {
      if (is_ice_enabled()) {
#ifndef CONFIG_HW_DISK_ENCRYPT_PERF
        if (create_crypto_blk_dev(crypt_ftr, (unsigned char*)&key_index,
                            real_blkdev, crypto_blkdev, label, 0)) {
          SLOGE("Error creating decrypted block device");
          rc = -1;
          goto errout;
        }
#endif
      } else {
        if (create_crypto_blk_dev(crypt_ftr, decrypted_master_key,
                            real_blkdev, crypto_blkdev, label, 0)) {
          SLOGE("Error creating decrypted block device");
          rc = -1;
          goto errout;
        }
      }
    }
  }

  if (rc == 0) {
    crypt_ftr->failed_decrypt_count = 0;
    if (orig_failed_decrypt_count != 0) {
      put_crypt_ftr_and_key(crypt_ftr);
    }

    /* Save the name of the crypto block device
     * so we can mount it when restarting the framework. */
#ifdef CONFIG_HW_DISK_ENCRYPT_PERF
    if (!is_ice_enabled())
#endif
    property_set("ro.crypto.fs_crypto_blkdev", crypto_blkdev);
    master_key_saved = 1;
  }

 errout:
  return rc;
}
#endif

static int try_mount_multiple_fs(const char *crypto_blkdev,
                                 const char *mount_point,
                                 const char *file_system)
{
    if (!mount(crypto_blkdev, mount_point, file_system, 0, NULL))
        return 0;
    if (strcmp(file_system, "ext4") &&
        !mount(crypto_blkdev, mount_point, "ext4", 0, NULL))
        return 0;
    if (strcmp(file_system, "f2fs") &&
        !mount(crypto_blkdev, mount_point, "f2fs", 0, NULL))
        return 0;
    return 1;
}

static int test_mount_encrypted_fs(struct crypt_mnt_ftr* crypt_ftr,
                                   const char *passwd, const char *mount_point, const char *label)
{
  unsigned char decrypted_master_key[MAX_KEY_LEN];
  char crypto_blkdev[MAXPATHLEN];
  //char real_blkdev[MAXPATHLEN];
  char tmp_mount_point[64];
  unsigned int orig_failed_decrypt_count;
  int rc;
  int use_keymaster = 0;
  int upgrade = 0;
  unsigned char* intermediate_key = 0;
  size_t intermediate_key_size = 0;
  int N = 1 << crypt_ftr->N_factor;
  int r = 1 << crypt_ftr->r_factor;
  int p = 1 << crypt_ftr->p_factor;

  SLOGD("crypt_ftr->fs_size = %lld\n", crypt_ftr->fs_size);
  orig_failed_decrypt_count = crypt_ftr->failed_decrypt_count;

  if (! (crypt_ftr->flags & CRYPT_MNT_KEY_UNENCRYPTED) ) {
    if (decrypt_master_key(passwd, decrypted_master_key, crypt_ftr,
                           &intermediate_key, &intermediate_key_size)) {
      SLOGE("Failed to decrypt master key\n");
      rc = -1;
      goto errout;
    }
  }

  //fs_mgr_get_crypt_info(fstab_default, 0, real_blkdev, sizeof(real_blkdev));

  // Create crypto block device - all (non fatal) code paths
  // need it
  if (create_crypto_blk_dev(crypt_ftr, decrypted_master_key, real_blkdev, crypto_blkdev, label, 0)) {
      SLOGE("Error creating decrypted block device\n");
      rc = -1;
      goto errout;
  }

  /* Work out if the problem is the password or the data */
  unsigned char scrypted_intermediate_key[sizeof(crypt_ftr->
                                                 scrypted_intermediate_key)];

  rc = crypto_scrypt(intermediate_key, intermediate_key_size,
                     crypt_ftr->salt, sizeof(crypt_ftr->salt),
                     N, r, p, scrypted_intermediate_key,
                     sizeof(scrypted_intermediate_key));

  // Does the key match the crypto footer?
  if (rc == 0 && memcmp(scrypted_intermediate_key,
                        crypt_ftr->scrypted_intermediate_key,
                        sizeof(scrypted_intermediate_key)) == 0) {
    SLOGI("Password matches");
    rc = 0;
  } else {
    /* Try mounting the file system anyway, just in case the problem's with
     * the footer, not the key. */
    snprintf(tmp_mount_point, sizeof(tmp_mount_point), "%s/tmp_mnt",
             mount_point);
    mkdir(tmp_mount_point, 0755);
    if (try_mount_multiple_fs(crypto_blkdev, tmp_mount_point, file_system)) {
      SLOGE("Error temp mounting decrypted block device\n");
      delete_crypto_blk_dev(label);

      rc = ++crypt_ftr->failed_decrypt_count;
      put_crypt_ftr_and_key(crypt_ftr);
    } else {
      /* Success! */
      SLOGI("Password did not match but decrypted drive mounted - continue");
      umount(tmp_mount_point);
      rc = 0;
    }
  }

  if (rc == 0) {
    crypt_ftr->failed_decrypt_count = 0;
    if (orig_failed_decrypt_count != 0) {
      put_crypt_ftr_and_key(crypt_ftr);
    }

    /* Save the name of the crypto block device
     * so we can mount it when restarting the framework. */
    property_set("ro.crypto.fs_crypto_blkdev", crypto_blkdev);

    /* Also save a the master key so we can reencrypted the key
     * the key when we want to change the password on it. */
    memcpy(saved_master_key, decrypted_master_key, crypt_ftr->keysize);
    saved_mount_point = strdup(mount_point);
    master_key_saved = 1;
    SLOGD("%s(): Master key saved\n", __FUNCTION__);
    rc = 0;

    // Upgrade if we're not using the latest KDF.
    use_keymaster = keymaster_check_compatibility();
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
                                crypt_ftr->master_key, crypt_ftr, true);
        if (!rc) {
            rc = put_crypt_ftr_and_key(crypt_ftr);
        }
        SLOGD("Key Derivation Function upgrade: rc=%d\n", rc);

        // Do not fail even if upgrade failed - machine is bootable
        // Note that if this code is ever hit, there is a *serious* problem
        // since KDFs should never fail. You *must* fix the kdf before
        // proceeding!
        if (rc) {
          SLOGW("Upgrade failed with error %d,"
                " but continuing with previous state",
                rc);
          rc = 0;
        }
    }
  }

 errout:
  if (intermediate_key) {
    memset(intermediate_key, 0, intermediate_key_size);
    free(intermediate_key);
  }
  return rc;
}

/*
 * Called by vold when it's asked to mount an encrypted external
 * storage volume. The incoming partition has no crypto header/footer,
 * as any metadata is been stored in a separate, small partition.  We
 * assume it must be using our same crypt type and keysize.
 *
 * out_crypto_blkdev must be MAXPATHLEN.
 */
int cryptfs_setup_ext_volume(const char* label, const char* real_blkdev,
        const unsigned char* key, int keysize, char* out_crypto_blkdev) {
    int fd = open(real_blkdev, O_RDONLY|O_CLOEXEC);
    if (fd == -1) {
        SLOGE("Failed to open %s: %s", real_blkdev, strerror(errno));
        return -1;
    }

    unsigned long nr_sec = 0;
    get_blkdev_size(fd, &nr_sec);
    close(fd);

    if (nr_sec == 0) {
        SLOGE("Failed to get size of %s: %s", real_blkdev, strerror(errno));
        return -1;
    }

    struct crypt_mnt_ftr ext_crypt_ftr;
    memset(&ext_crypt_ftr, 0, sizeof(ext_crypt_ftr));
    ext_crypt_ftr.fs_size = nr_sec;
    ext_crypt_ftr.keysize = cryptfs_get_keysize();
    strlcpy((char*) ext_crypt_ftr.crypto_type_name, cryptfs_get_crypto_name(),
            MAX_CRYPTO_TYPE_NAME_LEN);
    uint32_t flags = 0;
    /*if (e4crypt_is_native() &&
        android::base::GetBoolProperty("ro.crypto.allow_encrypt_override", false))
        flags |= CREATE_CRYPTO_BLK_DEV_FLAGS_ALLOW_ENCRYPT_OVERRIDE;*/

    return create_crypto_blk_dev(&ext_crypt_ftr, key, real_blkdev, out_crypto_blkdev, label, flags);
}

/*
 * Called by vold when it's asked to unmount an encrypted external
 * storage volume.
 */
int cryptfs_revert_ext_volume(const char* label) {
    return delete_crypto_blk_dev(label);
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
        SLOGE("encrypted fs already validated or not running with encryption,"
              " aborting");
        return -1;
    }

    if (get_crypt_ftr_and_key(crypt_ftr)) {
        SLOGE("Error getting crypt footer and key");
        return -1;
    }

    return 0;
}

#ifdef CONFIG_HW_DISK_ENCRYPTION
int cryptfs_check_passwd_hw(const char* passwd)
{
    struct crypt_mnt_ftr crypt_ftr;
    int rc;
    unsigned char master_key[KEY_LEN_BYTES];

    /* get key */
    if (get_crypt_ftr_and_key(&crypt_ftr)) {
        SLOGE("Error getting crypt footer and key");
        return -1;
    }

    /*
     * in case of manual encryption (from GUI), the encryption is done with
     * default password
     */
    if (crypt_ftr.flags & CRYPT_FORCE_COMPLETE) {
        /* compare scrypted_intermediate_key with stored scrypted_intermediate_key
         * which was created with actual password before reboot.
         */
        rc = cryptfs_get_master_key(&crypt_ftr, passwd, master_key);
        if (rc) {
            SLOGE("password doesn't match");
            rc = ++crypt_ftr.failed_decrypt_count;
            put_crypt_ftr_and_key(&crypt_ftr);
            return rc;
        }

        rc = test_mount_hw_encrypted_fs(&crypt_ftr, DEFAULT_PASSWORD,
            DATA_MNT_POINT, CRYPTO_BLOCK_DEVICE);

        if (rc) {
            SLOGE("Default password did not match on reboot encryption");
            return rc;
        }

        crypt_ftr.flags &= ~CRYPT_FORCE_COMPLETE;
        put_crypt_ftr_and_key(&crypt_ftr);
        rc = cryptfs_changepw(crypt_ftr.crypt_type, DEFAULT_PASSWORD, passwd);
        if (rc) {
            SLOGE("Could not change password on reboot encryption");
            return rc;
        }
    } else
        rc = test_mount_hw_encrypted_fs(&crypt_ftr, passwd,
            DATA_MNT_POINT, CRYPTO_BLOCK_DEVICE);

    if (crypt_ftr.crypt_type != CRYPT_TYPE_DEFAULT) {
        cryptfs_clear_password();
        password = strdup(passwd);
        struct timespec now;
        clock_gettime(CLOCK_BOOTTIME, &now);
        password_expiry_time = now.tv_sec + password_max_age_seconds;
    }

    return rc;
}
#endif

int cryptfs_check_passwd(const char *passwd)
{
    SLOGI("cryptfs_check_passwd");
    /*if (e4crypt_is_native()) {
        SLOGE("cryptfs_check_passwd not valid for file encryption");
        return -1;
    }*/

    struct crypt_mnt_ftr crypt_ftr;
    int rc;

    rc = check_unmounted_and_get_ftr(&crypt_ftr);
    if (rc) {
        SLOGE("Could not get footer");
        return rc;
    }

#ifdef CONFIG_HW_DISK_ENCRYPTION
    if (is_hw_disk_encryption((char*)crypt_ftr.crypto_type_name))
        return cryptfs_check_passwd_hw(passwd);
#endif

    rc = test_mount_encrypted_fs(&crypt_ftr, passwd,
                                 DATA_MNT_POINT, CRYPTO_BLOCK_DEVICE);

    if (rc) {
        SLOGE("Password did not match");
        return rc;
    }

    if (crypt_ftr.flags & CRYPT_FORCE_COMPLETE) {
        // Here we have a default actual password but a real password
        // we must test against the scrypted value
        // First, we must delete the crypto block device that
        // test_mount_encrypted_fs leaves behind as a side effect
        delete_crypto_blk_dev(CRYPTO_BLOCK_DEVICE);
        rc = test_mount_encrypted_fs(&crypt_ftr, DEFAULT_PASSWORD,
                                     DATA_MNT_POINT, CRYPTO_BLOCK_DEVICE);
        if (rc) {
            SLOGE("Default password did not match on reboot encryption");
            return rc;
        }

        crypt_ftr.flags &= ~CRYPT_FORCE_COMPLETE;
        put_crypt_ftr_and_key(&crypt_ftr);
        rc = cryptfs_changepw(crypt_ftr.crypt_type, DEFAULT_PASSWORD, passwd);
        if (rc) {
            SLOGE("Could not change password on reboot encryption");
            return rc;
        }
    }

    if (crypt_ftr.crypt_type != CRYPT_TYPE_DEFAULT) {
        cryptfs_clear_password();
        password = strdup(passwd);
        struct timespec now;
        clock_gettime(CLOCK_BOOTTIME, &now);
        password_expiry_time = now.tv_sec + password_max_age_seconds;
    }

    return rc;
}

int cryptfs_verify_passwd(const char *passwd)
{
    struct crypt_mnt_ftr crypt_ftr;
    unsigned char decrypted_master_key[MAX_KEY_LEN];
    char encrypted_state[PROPERTY_VALUE_MAX];
    int rc;

    property_get("ro.crypto.state", encrypted_state, "");
    if (strcmp(encrypted_state, "encrypted") ) {
        SLOGE("device not encrypted, aborting");
        return -2;
    }

    if (!master_key_saved) {
        SLOGE("encrypted fs not yet mounted, aborting");
        return -1;
    }

    if (!saved_mount_point) {
        SLOGE("encrypted fs failed to save mount point, aborting");
        return -1;
    }

    if (get_crypt_ftr_and_key(&crypt_ftr)) {
        SLOGE("Error getting crypt footer and key\n");
        return -1;
    }

    if (crypt_ftr.flags & CRYPT_MNT_KEY_UNENCRYPTED) {
        /* If the device has no password, then just say the password is valid */
        rc = 0;
    } else {
#ifdef CONFIG_HW_DISK_ENCRYPTION
        if(is_hw_disk_encryption((char*)crypt_ftr.crypto_type_name)) {
            if (verify_hw_fde_passwd(passwd, &crypt_ftr) >= 0)
              rc = 0;
            else
              rc = -1;
        } else {
            decrypt_master_key(passwd, decrypted_master_key, &crypt_ftr, 0, 0);
            if (!memcmp(decrypted_master_key, saved_master_key, crypt_ftr.keysize)) {
                /* They match, the password is correct */
                rc = 0;
            } else {
              /* If incorrect, sleep for a bit to prevent dictionary attacks */
                sleep(1);
                rc = 1;
            }
        }
#else
        decrypt_master_key(passwd, decrypted_master_key, &crypt_ftr, 0, 0);
        if (!memcmp(decrypted_master_key, saved_master_key, crypt_ftr.keysize)) {
            /* They match, the password is correct */
            rc = 0;
        } else {
            /* If incorrect, sleep for a bit to prevent dictionary attacks */
            sleep(1);
            rc = 1;
        }
#endif
    }

    return rc;
}

/* Initialize a crypt_mnt_ftr structure.  The keysize is
 * defaulted to cryptfs_get_keysize() bytes, and the filesystem size to 0.
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
    ftr->keysize = cryptfs_get_keysize();

    switch (keymaster_check_compatibility()) {
    case 1:
        ftr->kdf_type = KDF_SCRYPT_KEYMASTER;
        break;

    case 0:
        ftr->kdf_type = KDF_SCRYPT;
        break;

    default:
        SLOGE("keymaster_check_compatibility failed");
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

#define FRAMEWORK_BOOT_WAIT 60

static int cryptfs_enable_all_volumes(struct crypt_mnt_ftr* crypt_ftr, char* crypto_blkdev,
                                      char* real_blkdev, int previously_encrypted_upto) {
    return -1;
}

int cryptfs_enable_internal(int crypt_type, const char* passwd, int no_ui) {

    return -1;
}

int cryptfs_enable(int type, const char* passwd, int no_ui) {
    return cryptfs_enable_internal(type, passwd, no_ui);
}

int cryptfs_enable_default(int no_ui) {
    return cryptfs_enable_internal(CRYPT_TYPE_DEFAULT, DEFAULT_PASSWORD, no_ui);
}

int cryptfs_changepw(int crypt_type, const char *currentpw, const char *newpw)
{
    return -1;
}

#ifdef CONFIG_HW_DISK_ENCRYPTION
int cryptfs_changepw_hw_fde(int crypt_type, const char *currentpw, const char *newpw)
{
    return -1;
}
#endif

static unsigned int persist_get_max_entries(int encrypted) {
    struct crypt_mnt_ftr crypt_ftr;
    unsigned int dsize;
    unsigned int max_persistent_entries;

    /* If encrypted, use the values from the crypt_ftr, otherwise
     * use the values for the current spec.
     */
    if (encrypted) {
        if (get_crypt_ftr_and_key(&crypt_ftr)) {
            return -1;
        }
        dsize = crypt_ftr.persist_data_size;
    } else {
        dsize = CRYPT_PERSIST_DATA_SIZE;
    }

    max_persistent_entries = (dsize - sizeof(struct crypt_persist_data)) /
        sizeof(struct crypt_persist_entry);

    return max_persistent_entries;
}

static int persist_get_key(const char *fieldname, char *value)
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

static int persist_set_key(const char *fieldname, const char *value, int encrypted)
{
    unsigned int i;
    unsigned int num;
    unsigned int max_persistent_entries;

    if (persist_data == NULL) {
        return -1;
    }

    max_persistent_entries = persist_get_max_entries(encrypted);

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

/**
 * Test if key is part of the multi-entry (field, index) sequence. Return non-zero if key is in the
 * sequence and its index is greater than or equal to index. Return 0 otherwise.
 */
int match_multi_entry(const char *key, const char *field, unsigned index) {
    std::string key_ = key;
    std::string field_ = field;

    std::string parsed_field;
    unsigned parsed_index;

    std::string::size_type split = key_.find_last_of('_');
    if (split == std::string::npos) {
        parsed_field = key_;
        parsed_index = 0;
    } else {
        parsed_field = key_.substr(0, split);
        parsed_index = std::stoi(key_.substr(split + 1));
    }

    return parsed_field == field_ && parsed_index >= index;
}

/*
 * Delete entry/entries from persist_data. If the entries are part of a multi-segment field, all
 * remaining entries starting from index will be deleted.
 * returns PERSIST_DEL_KEY_OK if deletion succeeds,
 * PERSIST_DEL_KEY_ERROR_NO_FIELD if the field does not exist,
 * and PERSIST_DEL_KEY_ERROR_OTHER if error occurs.
 *
 */
static int persist_del_keys(const char *fieldname, unsigned index)
{
    unsigned int i;
    unsigned int j;
    unsigned int num;

    if (persist_data == NULL) {
        return PERSIST_DEL_KEY_ERROR_OTHER;
    }

    num = persist_data->persist_valid_entries;

    j = 0; // points to the end of non-deleted entries.
    // Filter out to-be-deleted entries in place.
    for (i = 0; i < num; i++) {
        if (!match_multi_entry(persist_data->persist_entry[i].key, fieldname, index)) {
            persist_data->persist_entry[j] = persist_data->persist_entry[i];
            j++;
        }
    }

    if (j < num) {
        persist_data->persist_valid_entries = j;
        // Zeroise the remaining entries
        memset(&persist_data->persist_entry[j], 0, (num - j) * sizeof(struct crypt_persist_entry));
        return PERSIST_DEL_KEY_OK;
    } else {
        // Did not find an entry matching the given fieldname
        return PERSIST_DEL_KEY_ERROR_NO_FIELD;
    }
}

static int persist_count_keys(const char *fieldname)
{
    unsigned int i;
    unsigned int count;

    if (persist_data == NULL) {
        return -1;
    }

    count = 0;
    for (i = 0; i < persist_data->persist_valid_entries; i++) {
        if (match_multi_entry(persist_data->persist_entry[i].key, fieldname, 0)) {
            count++;
        }
    }

    return count;
}

/* Return the value of the specified field. */
int cryptfs_getfield(const char *fieldname, char *value, int len)
{
    /*if (e4crypt_is_native()) {
        SLOGE("Cannot get field when file encrypted");
        return -1;
    }*/

    char temp_value[PROPERTY_VALUE_MAX];
    /* CRYPTO_GETFIELD_OK is success,
     * CRYPTO_GETFIELD_ERROR_NO_FIELD is value not set,
     * CRYPTO_GETFIELD_ERROR_BUF_TOO_SMALL is buffer (as given by len) too small,
     * CRYPTO_GETFIELD_ERROR_OTHER is any other error
     */
    int rc = CRYPTO_GETFIELD_ERROR_OTHER;
    int i;
    char temp_field[PROPERTY_KEY_MAX];

    if (persist_data == NULL) {
        load_persistent_data();
        if (persist_data == NULL) {
            SLOGE("Getfield error, cannot load persistent data");
            goto out;
        }
    }

    // Read value from persistent entries. If the original value is split into multiple entries,
    // stitch them back together.
    if (!persist_get_key(fieldname, temp_value)) {
        // We found it, copy it to the caller's buffer and keep going until all entries are read.
        if (strlcpy(value, temp_value, len) >= (unsigned) len) {
            // value too small
            rc = CRYPTO_GETFIELD_ERROR_BUF_TOO_SMALL;
            goto out;
        }
        rc = CRYPTO_GETFIELD_OK;

        for (i = 1; /* break explicitly */; i++) {
            if (snprintf(temp_field, sizeof(temp_field), "%s_%d", fieldname, i) >=
                    (int) sizeof(temp_field)) {
                // If the fieldname is very long, we stop as soon as it begins to overflow the
                // maximum field length. At this point we have in fact fully read out the original
                // value because cryptfs_setfield would not allow fields with longer names to be
                // written in the first place.
                break;
            }
            if (!persist_get_key(temp_field, temp_value)) {
                  if (strlcat(value, temp_value, len) >= (unsigned)len) {
                      // value too small.
                      rc = CRYPTO_GETFIELD_ERROR_BUF_TOO_SMALL;
                      goto out;
                  }
            } else {
                // Exhaust all entries.
                break;
            }
        }
    } else {
        /* Sadness, it's not there.  Return the error */
        rc = CRYPTO_GETFIELD_ERROR_NO_FIELD;
    }

out:
    return rc;
}

/* Set the value of the specified field. */
int cryptfs_setfield(const char *fieldname, const char *value)
{
    if (e4crypt_is_native()) {
        SLOGE("Cannot set field when file encrypted");
        return -1;
    }

    char encrypted_state[PROPERTY_VALUE_MAX];
    /* 0 is success, negative values are error */
    int rc = CRYPTO_SETFIELD_ERROR_OTHER;
    int encrypted = 0;
    unsigned int field_id;
    char temp_field[PROPERTY_KEY_MAX];
    unsigned int num_entries;
    unsigned int max_keylen;

    if (persist_data == NULL) {
        load_persistent_data();
        if (persist_data == NULL) {
            SLOGE("Setfield error, cannot load persistent data");
            goto out;
        }
    }

    property_get("ro.crypto.state", encrypted_state, "");
    if (!strcmp(encrypted_state, "encrypted") ) {
        encrypted = 1;
    }

    // Compute the number of entries required to store value, each entry can store up to
    // (PROPERTY_VALUE_MAX - 1) chars
    if (strlen(value) == 0) {
        // Empty value also needs one entry to store.
        num_entries = 1;
    } else {
        num_entries = (strlen(value) + (PROPERTY_VALUE_MAX - 1) - 1) / (PROPERTY_VALUE_MAX - 1);
    }

    max_keylen = strlen(fieldname);
    if (num_entries > 1) {
        // Need an extra "_%d" suffix.
        max_keylen += 1 + log10(num_entries);
    }
    if (max_keylen > PROPERTY_KEY_MAX - 1) {
        rc = CRYPTO_SETFIELD_ERROR_FIELD_TOO_LONG;
        goto out;
    }

    // Make sure we have enough space to write the new value
    if (persist_data->persist_valid_entries + num_entries - persist_count_keys(fieldname) >
        persist_get_max_entries(encrypted)) {
        rc = CRYPTO_SETFIELD_ERROR_VALUE_TOO_LONG;
        goto out;
    }

    // Now that we know persist_data has enough space for value, let's delete the old field first
    // to make up space.
    persist_del_keys(fieldname, 0);

    if (persist_set_key(fieldname, value, encrypted)) {
        // fail to set key, should not happen as we have already checked the available space
        SLOGE("persist_set_key() error during setfield()");
        goto out;
    }

    for (field_id = 1; field_id < num_entries; field_id++) {
        snprintf(temp_field, sizeof(temp_field), "%s_%u", fieldname, field_id);

        if (persist_set_key(temp_field, value + field_id * (PROPERTY_VALUE_MAX - 1), encrypted)) {
            // fail to set key, should not happen as we have already checked the available space.
            SLOGE("persist_set_key() error during setfield()");
            goto out;
        }
    }

    /* If we are running encrypted, save the persistent data now */
    if (encrypted) {
        if (save_persistent_data()) {
            SLOGE("Setfield error, cannot save persistent data");
            goto out;
        }
    }

    rc = CRYPTO_SETFIELD_OK;

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
    int crypt_type = cryptfs_get_password_type();
    if (crypt_type < 0 || crypt_type > CRYPT_TYPE_MAX_TYPE) {
        SLOGE("Bad crypt type - error");
    } else if (crypt_type != CRYPT_TYPE_DEFAULT) {
        SLOGD("Password is not default - "
              "starting min framework to prompt");
        property_set("vold.decrypt", "trigger_restart_min_framework");
        return 0;
    } else if (cryptfs_check_passwd(DEFAULT_PASSWORD) == 0) {
        SLOGD("Password is default - restarting filesystem");
        cryptfs_restart_internal(0);
        return 0;
    } else {
        SLOGE("Encrypted, default crypt type but can't decrypt");
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
    /*if (e4crypt_is_native()) {
        SLOGE("cryptfs_get_password_type not valid for file encryption");
        return -1;
    }*/

    struct crypt_mnt_ftr crypt_ftr;

    if (get_crypt_ftr_and_key(&crypt_ftr)) {
        SLOGE("Error getting crypt footer and key\n");
        return -1;
    }

    if (crypt_ftr.flags & CRYPT_INCONSISTENT_STATE) {
        return -1;
    }

    return crypt_ftr.crypt_type;
}

const char* cryptfs_get_password()
{
    /*if (e4crypt_is_native()) {
        SLOGE("cryptfs_get_password not valid for file encryption");
        return 0;
    }*/

    struct timespec now;
    clock_gettime(CLOCK_BOOTTIME, &now);
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

int cryptfs_isConvertibleToFBE()
{
    return 0;
}

int cryptfs_create_default_ftr(struct crypt_mnt_ftr* crypt_ftr, __attribute__((unused))int key_length)
{
    if (cryptfs_init_crypt_mnt_ftr(crypt_ftr)) {
        SLOGE("Failed to initialize crypt_ftr");
        return -1;
    }

    if (create_encrypted_random_key(DEFAULT_PASSWORD, crypt_ftr->master_key,
                                    crypt_ftr->salt, crypt_ftr)) {
        SLOGE("Cannot create encrypted master key\n");
        return -1;
    }

    //crypt_ftr->keysize = key_length / 8;
    return 0;
}

int cryptfs_get_master_key(struct crypt_mnt_ftr* ftr, const char* password,
                           unsigned char* master_key)
{
    int rc;

    unsigned char* intermediate_key = 0;
    size_t intermediate_key_size = 0;

    if (password == 0 || *password == 0) {
        password = DEFAULT_PASSWORD;
    }

    rc = decrypt_master_key(password, master_key, ftr, &intermediate_key,
                            &intermediate_key_size);

    if (rc) {
        SLOGE("Can't calculate intermediate key");
        return rc;
    }

    int N = 1 << ftr->N_factor;
    int r = 1 << ftr->r_factor;
    int p = 1 << ftr->p_factor;

    unsigned char scrypted_intermediate_key[sizeof(ftr->scrypted_intermediate_key)];

    rc = crypto_scrypt(intermediate_key, intermediate_key_size,
                       ftr->salt, sizeof(ftr->salt), N, r, p,
                       scrypted_intermediate_key,
                       sizeof(scrypted_intermediate_key));

    free(intermediate_key);

    if (rc) {
        SLOGE("Can't scrypt intermediate key");
        return rc;
    }

    return memcmp(scrypted_intermediate_key, ftr->scrypted_intermediate_key,
                  intermediate_key_size);
}

