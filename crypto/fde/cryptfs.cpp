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
//#include <ext4_utils/ext4_crypt.h>
//#include <ext4_utils/ext4_utils.h>
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
#if TW_KEYMASTER_MAX_API == 3
#include "../ext4crypt/Keymaster3.h"
#endif
#if TW_KEYMASTER_MAX_API == 4
#include "../ext4crypt/Keymaster4.h"
#endif
#if TW_KEYMASTER_MAX_API == 0
#include <hardware/keymaster.h>
#else // so far, all trees that have keymaster >= 1 have keymaster 1 support
#include <stdbool.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <hardware/keymaster0.h>
#include <hardware/keymaster1.h>
#endif
//#include "android-base/properties.h"
//#include <bootloader_message/bootloader_message.h>
#ifdef CONFIG_HW_DISK_ENCRYPTION
#include <cryptfs_hw.h>
#endif
extern "C" {
#include <crypto_scrypt.h>
}
#include <string>
#include <vector>

#define ALOGE(...) fprintf(stdout, "E:" __VA_ARGS__)
#define SLOGE(...) fprintf(stdout, "E:" __VA_ARGS__)
#define SLOGW(...) fprintf(stdout, "W:" __VA_ARGS__)
#define SLOGI(...) fprintf(stdout, "I:" __VA_ARGS__)
#define SLOGD(...) fprintf(stdout, "D:" __VA_ARGS__)

#define UNUSED __attribute__((unused))

#define DM_CRYPT_BUF_SIZE 4096

#define HASH_COUNT 2000

#ifndef min /* already defined by windows.h */
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

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

#define TABLE_LOAD_RETRIES 10

#define RSA_KEY_SIZE 2048
#define RSA_KEY_SIZE_BYTES (RSA_KEY_SIZE / 8)
#define RSA_EXPONENT 0x10001
#define KEYMASTER_CRYPTFS_RATE_LIMIT 1  // Maximum one try per second
#define KEY_LEN_BYTES 16

#define RETRY_MOUNT_ATTEMPTS 10
#define RETRY_MOUNT_DELAY_SECONDS 1

#define CREATE_CRYPTO_BLK_DEV_FLAGS_ALLOW_ENCRYPT_OVERRIDE (1)

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

#if TW_KEYMASTER_MAX_API == 0
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
#else //TW_KEYMASTER_MAX_API == 0
static int keymaster_init(keymaster0_device_t **keymaster0_dev,
                          keymaster1_device_t **keymaster1_dev)
{
    int rc;

    const hw_module_t* mod;
    rc = hw_get_module_by_class(KEYSTORE_HARDWARE_MODULE_ID, NULL, &mod);
    if (rc) {
        printf("could not find any keystore module\n");
        goto err;
    }

    printf("keymaster module name is %s\n", mod->name);
    printf("keymaster version is %d\n", mod->module_api_version);

    *keymaster0_dev = NULL;
    *keymaster1_dev = NULL;
    if (mod->module_api_version == KEYMASTER_MODULE_API_VERSION_1_0) {
        printf("Found keymaster1 module, using keymaster1 API.\n");
        rc = keymaster1_open(mod, keymaster1_dev);
    } else {
        printf("Found keymaster0 module, using keymaster0 API.\n");
        rc = keymaster0_open(mod, keymaster0_dev);
    }

    if (rc) {
        printf("could not open keymaster device in %s (%s)\n",
              KEYSTORE_HARDWARE_MODULE_ID, strerror(-rc));
        goto err;
    }

    return 0;

err:
    *keymaster0_dev = NULL;
    *keymaster1_dev = NULL;
    return rc;
}
#endif //TW_KEYMASTER_MAX_API == 0

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
            SLOGE("scrypt failed\n");
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
    SLOGI("starting verify_hw_fde_passwd\n");
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
    char* new_passwd = NULL;
    unsigned char newpw[32] = {0};
    int key_index = -1;
    int passwd_updated = -1;
    int ascii_passwd_updated = (crypt_ftr->flags & CRYPT_ASCII_PASSWORD_UPDATED);

    key_index = verify_hw_fde_passwd(passwd, crypt_ftr);
    if (key_index < 0) {
        ++crypt_ftr->failed_decrypt_count;

        if (ascii_passwd_updated) {
            SLOGI("Ascii password was updated\n");
        } else {
            /* Code in else part would execute only once:
             * When device is upgraded from L->M release.
             * Once upgraded, code flow should never come here.
             * L release passed actual password in hex, so try with hex
             * Each nible of passwd was encoded as a byte, so allocate memory
             * twice of password len plus one more byte for null termination
             */
            if (crypt_ftr->crypt_type == CRYPT_TYPE_DEFAULT) {
                new_passwd = (char*)malloc(strlen(DEFAULT_HEX_PASSWORD) + 1);
                if (new_passwd == NULL) {
                    SLOGE("System out of memory. Password verification  incomplete");
                    goto out;
                }
                strlcpy(new_passwd, DEFAULT_HEX_PASSWORD, strlen(DEFAULT_HEX_PASSWORD) + 1);
            } else {
                new_passwd = (char*)malloc(strlen(passwd) * 2 + 1);
                if (new_passwd == NULL) {
                    SLOGE("System out of memory. Password verification  incomplete");
                    goto out;
                }
                convert_key_to_hex_ascii_for_upgrade((const unsigned char*)passwd,
                                       strlen(passwd), new_passwd);
            }
            key_index = set_hw_device_encryption_key((const char*)new_passwd,
                                       (char*) crypt_ftr->crypto_type_name);
            if (key_index >=0) {
                crypt_ftr->failed_decrypt_count = 0;
                SLOGI("Hex password verified...will try to update with Ascii value");
                /* Before updating password, tie that with keymaster to tie with ROT */

                if (get_keymaster_hw_fde_passwd(passwd, newpw,
                                                crypt_ftr->salt, crypt_ftr)) {
                    passwd_updated = update_hw_device_encryption_key(new_passwd,
                                     passwd, (char*)crypt_ftr->crypto_type_name);
                } else {
                    passwd_updated = update_hw_device_encryption_key(new_passwd,
                                     (const char*)newpw, (char*)crypt_ftr->crypto_type_name);
                }

                if (passwd_updated >= 0) {
                    crypt_ftr->flags |= CRYPT_ASCII_PASSWORD_UPDATED;
                    SLOGI("Ascii password recorded and updated");
                } else {
                    SLOGI("Passwd verified, could not update...Will try next time");
                }
            } else {
                ++crypt_ftr->failed_decrypt_count;
            }
            free(new_passwd);
        }
    } else {
        if (!ascii_passwd_updated)
            crypt_ftr->flags |= CRYPT_ASCII_PASSWORD_UPDATED;
    }
out:
    // update footer before leaving
    //put_crypt_ftr_and_key(crypt_ftr);
    return key_index;
}
#endif

void set_partition_data(const char* block_device, const char* key_location, const char* fs)
{
  strcpy(key_fname, key_location);
  strcpy(real_blkdev, block_device);
  strcpy(file_system, fs);
}

/* This signs the given object using the keymaster key. */
static int keymaster_sign_object(struct crypt_mnt_ftr *ftr,
                                 const unsigned char *object,
                                 const size_t object_size,
                                 unsigned char **signature,
                                 size_t *signature_size)
{
    SLOGI("TWRP keymaster max API: %i\n", TW_KEYMASTER_MAX_API);
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
            SLOGI("Signing unpadded object\n");
            break;
        case KDF_SCRYPT_KEYMASTER_BADLY_PADDED:
            // This is broken: Since the value of object is uniformly
            // distributed, it produces a message that is larger than the
            // public modulus with probability 0.25.
            memcpy(to_sign, object, min(RSA_KEY_SIZE_BYTES, object_size));
            SLOGI("Signing end-padded object\n");
            break;
        case KDF_SCRYPT_KEYMASTER:
            // This ensures the most significant byte of the signed message
            // is zero.  We could have zero-padded to the left instead, but
            // this approach is slightly more robust against changes in
            // object size.  However, it's still broken (but not unusably
            // so) because we really should be using a proper deterministic
            // RSA padding function, such as PKCS1.
            memcpy(to_sign + 1, object, min((size_t)RSA_KEY_SIZE_BYTES - 1, object_size));
            SLOGI("Signing safely-padded object\n");
            break;
        default:
            SLOGE("Unknown KDF type %d", ftr->kdf_type);
            return -1;
    }

    int rc = -1;

#if TW_KEYMASTER_MAX_API >= 1
    keymaster0_device_t *keymaster0_dev = 0;
    keymaster1_device_t *keymaster1_dev = 0;
    if (keymaster_init(&keymaster0_dev, &keymaster1_dev)) {
#else
    keymaster_device_t *keymaster0_dev = 0;
    if (keymaster_init(&keymaster0_dev)) {
#endif
        printf("Failed to init keymaster 0/1\n");
        goto initfail;
    }
    if (keymaster0_dev) {
        keymaster_rsa_sign_params_t params;
        params.digest_type = DIGEST_NONE;
        params.padding_type = PADDING_NONE;

        rc = keymaster0_dev->sign_data(keymaster0_dev,
                                      &params,
                                      ftr->keymaster_blob,
                                      ftr->keymaster_blob_size,
                                      to_sign,
                                      to_sign_size,
                                      signature,
                                      signature_size);
        goto out;
    }
#if TW_KEYMASTER_MAX_API >= 1
    else if (keymaster1_dev) {
        keymaster_key_blob_t key = { ftr->keymaster_blob, ftr->keymaster_blob_size };
        keymaster_key_param_t params[] = {
            keymaster_param_enum(KM_TAG_PADDING, KM_PAD_NONE),
            keymaster_param_enum(KM_TAG_DIGEST, KM_DIGEST_NONE),
        };
        keymaster_key_param_set_t param_set = { params, sizeof(params)/sizeof(*params) };
        keymaster_operation_handle_t op_handle;
        keymaster_error_t error = keymaster1_dev->begin(keymaster1_dev, KM_PURPOSE_SIGN, &key,
                                                        &param_set, NULL /* out_params */,
                                                        &op_handle);
        if (error == KM_ERROR_KEY_RATE_LIMIT_EXCEEDED) {
            // Key usage has been rate-limited.  Wait a bit and try again.
            sleep(KEYMASTER_CRYPTFS_RATE_LIMIT);
            error = keymaster1_dev->begin(keymaster1_dev, KM_PURPOSE_SIGN, &key,
                                          &param_set, NULL /* out_params */,
                                          &op_handle);
        }
        if (error != KM_ERROR_OK) {
            printf("Error starting keymaster signature transaction: %d\n", error);
            rc = -1;
            goto out;
        }

        keymaster_blob_t input = { to_sign, to_sign_size };
        size_t input_consumed;
        error = keymaster1_dev->update(keymaster1_dev, op_handle, NULL /* in_params */,
                                       &input, &input_consumed, NULL /* out_params */,
                                       NULL /* output */);
        if (error != KM_ERROR_OK) {
            printf("Error sending data to keymaster signature transaction: %d\n", error);
            rc = -1;
            goto out;
        }
        if (input_consumed != to_sign_size) {
            // This should never happen.  If it does, it's a bug in the keymaster implementation.
            printf("Keymaster update() did not consume all data.\n");
            keymaster1_dev->abort(keymaster1_dev, op_handle);
            rc = -1;
            goto out;
        }

        keymaster_blob_t tmp_sig;
        error = keymaster1_dev->finish(keymaster1_dev, op_handle, NULL /* in_params */,
                                       NULL /* verify signature */, NULL /* out_params */,
                                       &tmp_sig);
        if (error != KM_ERROR_OK) {
            printf("Error finishing keymaster signature transaction: %d\n", error);
            rc = -1;
            goto out;
        }

        *signature = (uint8_t*)tmp_sig.data;
        *signature_size = tmp_sig.data_length;
        rc = 0;
    }
#endif // TW_KEYMASTER_API >= 1

    out:
#if TW_KEYMASTER_MAX_API >= 1
        if (keymaster1_dev)
            keymaster1_close(keymaster1_dev);
#endif
        if (keymaster0_dev)
#if TW_KEYMASTER_MAX_API >= 1
            keymaster0_close(keymaster0_dev);
#else
            keymaster_close(keymaster0_dev);
#endif

        if (rc == 0)
            return 0; // otherwise we'll try for a newer keymaster API

initfail:
#if TW_KEYMASTER_MAX_API == 3
    return keymaster_sign_object_for_cryptfs_scrypt(ftr->keymaster_blob, ftr->keymaster_blob_size,
            KEYMASTER_CRYPTFS_RATE_LIMIT, to_sign, to_sign_size, signature, signature_size,
            ftr->keymaster_blob, KEYMASTER_BLOB_SIZE, &ftr->keymaster_blob_size);
#endif //TW_KEYMASTER_MAX_API == 3
#if TW_KEYMASTER_MAX_API >= 4
    //for (;;) {
        auto result = keymaster_sign_object_for_cryptfs_scrypt(
            ftr->keymaster_blob, ftr->keymaster_blob_size, KEYMASTER_CRYPTFS_RATE_LIMIT, to_sign,
            to_sign_size, signature, signature_size);
        switch (result) {
            case KeymasterSignResult::ok:
                return 0;
            case KeymasterSignResult::upgrade:
                break;
            default:
                return -1;
        }
        SLOGD("Upgrading key\n");
        if (keymaster_upgrade_key_for_cryptfs_scrypt(
                RSA_KEY_SIZE, RSA_EXPONENT, KEYMASTER_CRYPTFS_RATE_LIMIT, ftr->keymaster_blob,
                ftr->keymaster_blob_size, ftr->keymaster_blob, KEYMASTER_BLOB_SIZE,
                &ftr->keymaster_blob_size) != 0) {
            SLOGE("Failed to upgrade key\n");
            return -1;
        }
        /*if (put_crypt_ftr_and_key(ftr) != 0) {
            SLOGE("Failed to write upgraded key to disk");
        }*/
        SLOGD("Key upgraded successfully\n");
        return 0;
    //}
#endif
    return -1;
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

uint32_t cryptfs_get_keysize() {
    return get_crypto_type().get_keysize();
}

const char *cryptfs_get_crypto_name() {
    return get_crypto_type().get_crypto_name();
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

  SLOGI("target_type = %s\n", tgt->target_type);
  SLOGI("real_blk_name = %s, extra_params = %s\n", real_blk_name, extra_params);
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
    char temp[10];
    snprintf(temp, sizeof(temp), "%zd", extra_params_vec.size());
    std::string extra_params = temp; //std::to_string(extra_params_vec.size());
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
            extra_params_vec.push_back(std::string("allow_discards")); // Used to be extra_params_vec.emplace_back("allow_discards"); but this won't compile in 5.1 trees
        }
    }
    if (flags & CREATE_CRYPTO_BLK_DEV_FLAGS_ALLOW_ENCRYPT_OVERRIDE) {
        extra_params_vec.push_back(std::string("allow_encrypt_override")); // Used to be extra_params_vec.emplace_back("allow_encrypt_override"); but this won't compile in 5.1 trees
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
    SLOGI("Using pbkdf2 for cryptfs KDF\n");

    /* Turn the password into a key and IV that can decrypt the master key */
    return PKCS5_PBKDF2_HMAC_SHA1(passwd, strlen(passwd), salt, SALT_LEN,
                                  HASH_COUNT, INTERMEDIATE_BUF_SIZE,
                                  ikey) != 1;
}

static int scrypt(const char *passwd, const unsigned char *salt,
                  unsigned char *ikey, void *params)
{
    SLOGI("Using scrypt for cryptfs KDF\n");

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
    SLOGI("Using scrypt with keymaster for cryptfs KDF\n");

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
        SLOGE("Keymaster signing failed");
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
      rc = -1;
      goto errout;
    }
    else {
      if (is_ice_enabled()) {
#ifndef CONFIG_HW_DISK_ENCRYPT_PERF
        if (create_crypto_blk_dev(crypt_ftr, (unsigned char*)&key_index,
                            real_blkdev, crypto_blkdev, label, 0)) {
          SLOGE("Error creating decrypted block device\n");
          rc = -1;
          goto errout;
        }
#endif
      } else {
        if (create_crypto_blk_dev(crypt_ftr, decrypted_master_key,
                            real_blkdev, crypto_blkdev, label, 0)) {
          SLOGE("Error creating decrypted block device\n");
          rc = -1;
          goto errout;
        }
      }
    }
  }

  if (rc == 0) {
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
    SLOGI("Password matches\n");
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

      rc = -1;
    } else {
      /* Success! */
      SLOGI("Password did not match but decrypted drive mounted - continue\n");
      umount(tmp_mount_point);
      rc = 0;
    }
  }

  if (rc == 0) {
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
            return rc;
        }

        rc = test_mount_hw_encrypted_fs(&crypt_ftr, DEFAULT_PASSWORD,
            DATA_MNT_POINT, CRYPTO_BLOCK_DEVICE);

        if (rc) {
            SLOGE("Default password did not match on reboot encryption");
            return rc;
        }
    } else {
        rc = test_mount_hw_encrypted_fs(&crypt_ftr, passwd,
            DATA_MNT_POINT, CRYPTO_BLOCK_DEVICE);
        SLOGE("test mount returned %i\n", rc);
    }

    return rc;
}
#endif

int cryptfs_check_passwd(const char *passwd)
{
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

/* Returns type of the password, default, pattern, pin or password.
 */
int cryptfs_get_password_type(void)
{
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

