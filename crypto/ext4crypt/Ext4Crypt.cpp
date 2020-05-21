/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "Ext4Crypt.h"
#include "Decrypt.h"

#ifdef USE_KEYSTORAGE_3
#include "KeyStorage3.h"
#else
#include "KeyStorage.h"
#endif
#include "Utils.h"

#include <algorithm>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/sha.h>
#include <selinux/android.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>

#include <private/android_filesystem_config.h>

#ifdef HAVE_SYNTH_PWD_SUPPORT
#include <ext4_utils/ext4_crypt.h>
#else
#include "ext4_crypt.h"
#endif
#ifndef HAVE_LIBKEYUTILS
#include "key_control.h"
#else
#include <keyutils.h>
#endif

#include <hardware/gatekeeper.h>
#include "HashPassword.h"

#define EMULATED_USES_SELINUX 0
#define MANAGE_MISC_DIRS 0

#include <cutils/fs.h>
#include <cutils/properties.h>

#include <android-base/file.h>
//#include <android-base/logging.h>
#include <android-base/stringprintf.h>

#define LOG(x) std::cout
#define PLOG(x) std::cout
#define DATA_MNT_POINT "/data"

using android::base::StringPrintf;
using android::vold::kEmptyAuthentication;

// NOTE: keep in sync with StorageManager
//static constexpr int FLAG_STORAGE_DE = 1 << 0; // moved to Decrypt.h
//static constexpr int FLAG_STORAGE_CE = 1 << 1;

// Store main DE raw ref / policy
std::string de_raw_ref;
// Map user ids to key references
std::map<userid_t, std::string> s_de_key_raw_refs;
std::map<userid_t, std::string> s_ce_key_raw_refs;

namespace {
const std::string device_key_dir = std::string() + DATA_MNT_POINT + e4crypt_unencrypted_folder;
const std::string device_key_path = device_key_dir + "/key";
const std::string device_key_temp = device_key_dir + "/temp";

const std::string user_key_dir = std::string() + DATA_MNT_POINT + "/misc/vold/user_keys";
const std::string user_key_temp = user_key_dir + "/temp";

bool s_global_de_initialized = false;

// Some users are ephemeral, don't try to wipe their keys from disk
std::set<userid_t> s_ephemeral_users;

// TODO abolish this map. Keys should not be long-lived in user memory, only kernel memory.
// See b/26948053
std::map<userid_t, std::string> s_ce_keys;

// ext4enc:TODO get this const from somewhere good
const int EXT4_KEY_DESCRIPTOR_SIZE = 8;

// ext4enc:TODO Include structure from somewhere sensible
// MUST be in sync with ext4_crypto.c in kernel
constexpr int EXT4_ENCRYPTION_MODE_AES_256_XTS = 1;
constexpr int EXT4_AES_256_XTS_KEY_SIZE = 64;
constexpr int EXT4_MAX_KEY_SIZE = 64;
struct ext4_encryption_key {
    uint32_t mode;
    char raw[EXT4_MAX_KEY_SIZE];
    uint32_t size;
};
}

static bool e4crypt_is_emulated() {
    return false; //property_get_bool("persist.sys.emulate_fbe", false);
}

static const char* escape_null(const char* value) {
    return (value == nullptr) ? "null" : value;
}

// Get raw keyref - used to make keyname and to pass to ioctl
static std::string generate_key_ref(const char* key, int length) {
    SHA512_CTX c;

    SHA512_Init(&c);
    SHA512_Update(&c, key, length);
    unsigned char key_ref1[SHA512_DIGEST_LENGTH];
    SHA512_Final(key_ref1, &c);

    SHA512_Init(&c);
    SHA512_Update(&c, key_ref1, SHA512_DIGEST_LENGTH);
    unsigned char key_ref2[SHA512_DIGEST_LENGTH];
    SHA512_Final(key_ref2, &c);

    static_assert(EXT4_KEY_DESCRIPTOR_SIZE <= SHA512_DIGEST_LENGTH,
                  "Hash too short for descriptor");
    return std::string((char*)key_ref2, EXT4_KEY_DESCRIPTOR_SIZE);
}

static bool fill_key(const std::string& key, ext4_encryption_key* ext4_key) {
    if (key.size() != EXT4_AES_256_XTS_KEY_SIZE) {
        LOG(ERROR) << "Wrong size key " << key.size();
        return false;
    }
    static_assert(EXT4_AES_256_XTS_KEY_SIZE <= sizeof(ext4_key->raw), "Key too long!");
    ext4_key->mode = EXT4_ENCRYPTION_MODE_AES_256_XTS;
    ext4_key->size = key.size();
    memset(ext4_key->raw, 0, sizeof(ext4_key->raw));
    memcpy(ext4_key->raw, key.data(), key.size());
    return true;
}

static std::string keyname(const std::string& raw_ref) {
    std::ostringstream o;
    o << "ext4:";
    for (auto i : raw_ref) {
        o << std::hex << std::setw(2) << std::setfill('0') << (int)i;
    }
    LOG(INFO) << "keyname is " << o.str() << "\n";
    return o.str();
}

// Get the keyring we store all keys in
static bool e4crypt_keyring(key_serial_t* device_keyring) {
    *device_keyring = keyctl_search(KEY_SPEC_SESSION_KEYRING, "keyring", "e4crypt", 0);
    if (*device_keyring == -1) {
        PLOG(ERROR) << "Unable to find device keyring\n";
        return false;
    }
    return true;
}

// Install password into global keyring
// Return raw key reference for use in policy
static bool install_key(const std::string& key, std::string* raw_ref) {
    ext4_encryption_key ext4_key;
    if (!fill_key(key, &ext4_key)) return false;
    *raw_ref = generate_key_ref(ext4_key.raw, ext4_key.size);
    auto ref = keyname(*raw_ref);
    key_serial_t device_keyring;
    if (!e4crypt_keyring(&device_keyring)) return false;
    key_serial_t key_id =
        add_key("logon", ref.c_str(), (void*)&ext4_key, sizeof(ext4_key), device_keyring);
    if (key_id == -1) {
        PLOG(ERROR) << "Failed to insert key into keyring " << device_keyring << "\n";
        return false;
    }
    LOG(DEBUG) << "Added key " << key_id << " (" << ref << ") to keyring " << device_keyring
               << " in process " << getpid() << "\n";
    return true;
}

static std::string get_de_key_path(userid_t user_id) {
LOG(INFO) << "get_de_key_path " << user_id << " " << StringPrintf("%s/de/%d", user_key_dir.c_str(), user_id) << "\n";
    return StringPrintf("%s/de/%d", user_key_dir.c_str(), user_id);
}

static std::string get_ce_key_directory_path(userid_t user_id) {
LOG(INFO) << "get_ce_key_directory_path " << user_id << ": " << StringPrintf("%s/ce/%d", user_key_dir.c_str(), user_id) << "\n";
    return StringPrintf("%s/ce/%d", user_key_dir.c_str(), user_id);
}

// Returns the keys newest first
static std::vector<std::string> get_ce_key_paths(const std::string& directory_path) {
    auto dirp = std::unique_ptr<DIR, int (*)(DIR*)>(opendir(directory_path.c_str()), closedir);
    if (!dirp) {
        PLOG(ERROR) << "Unable to open ce key directory: " + directory_path;
        return std::vector<std::string>();
    }
    std::vector<std::string> result;
    for (;;) {
        errno = 0;
        auto const entry = readdir(dirp.get());
        if (!entry) {
            if (errno) {
                PLOG(ERROR) << "Unable to read ce key directory: " + directory_path;
                return std::vector<std::string>();
            }
            break;
        }
        if (entry->d_type != DT_DIR || entry->d_name[0] != 'c') {
            LOG(DEBUG) << "Skipping non-key " << entry->d_name;
            continue;
        }
        result.emplace_back(directory_path + "/" + entry->d_name);
        LOG(INFO) << "get_ce_key_paths adding: " << directory_path + "/" + entry->d_name << "\n";
    }
    std::sort(result.begin(), result.end());
    std::reverse(result.begin(), result.end());
    return result;
}

static std::string get_ce_key_current_path(const std::string& directory_path) {
LOG(INFO) << "get_ce_key_current_path: " << directory_path + "/current\n";
    return directory_path + "/current";
}

// Discard all keys but the named one; rename it to canonical name.
// No point in acting on errors in this; ignore them.
static void fixate_user_ce_key(const std::string& directory_path, const std::string &to_fix,
                               const std::vector<std::string>& paths) {
    for (auto const other_path: paths) {
        if (other_path != to_fix) {
            android::vold::destroyKey(other_path);
        }
    }
    auto const current_path = get_ce_key_current_path(directory_path);
    if (to_fix != current_path) {
        LOG(DEBUG) << "Renaming " << to_fix << " to " << current_path;
        if (rename(to_fix.c_str(), current_path.c_str()) != 0) {
            PLOG(WARNING) << "Unable to rename " << to_fix << " to " << current_path;
        }
    }
}

static bool read_and_fixate_user_ce_key(userid_t user_id,
                                        const android::vold::KeyAuthentication& auth,
                                        std::string *ce_key) {
    auto const directory_path = get_ce_key_directory_path(user_id);
    auto const paths = get_ce_key_paths(directory_path);
    for (auto const ce_key_path: paths) {
        LOG(DEBUG) << "Trying user CE key " << ce_key_path;
        if (android::vold::retrieveKey(ce_key_path, auth, ce_key)) {
            LOG(DEBUG) << "Successfully retrieved key";
            fixate_user_ce_key(directory_path, ce_key_path, paths);
            return true;
        }
    }
    LOG(ERROR) << "Failed to find working ce key for user " << user_id;
    return false;
}

static bool read_and_install_user_ce_key(userid_t user_id,
                                         const android::vold::KeyAuthentication& auth) {
    if (s_ce_key_raw_refs.count(user_id) != 0) return true;
    std::string ce_key;
    if (!read_and_fixate_user_ce_key(user_id, auth, &ce_key)) return false;
    std::string ce_raw_ref;
    if (!install_key(ce_key, &ce_raw_ref)) return false;
    s_ce_keys[user_id] = ce_key;
    s_ce_key_raw_refs[user_id] = ce_raw_ref;
    LOG(DEBUG) << "Installed ce key for user " << user_id;
    return true;
}

static bool prepare_dir(const std::string& dir, mode_t mode, uid_t uid, gid_t gid) {
    LOG(DEBUG) << "Preparing: " << dir << "\n";
    return true;
    return access(dir.c_str(), F_OK) == 0; // we don't want recovery creating directories or changing permissions at this point, so we will just return true if the path already exists
    if (fs_prepare_dir(dir.c_str(), mode, uid, gid) != 0) {
        PLOG(ERROR) << "Failed to prepare " << dir;
        return false;
    }
    return true;
}

static bool path_exists(const std::string& path) {
    return access(path.c_str(), F_OK) == 0;
}

bool lookup_key_ref(const std::map<userid_t, std::string>& key_map, userid_t user_id,
                           std::string* raw_ref) {
    auto refi = key_map.find(user_id);
    if (refi == key_map.end()) {
        LOG(ERROR) << "Cannot find key for " << user_id;
        return false;
    }
    *raw_ref = refi->second;
    return true;
}

static bool is_numeric(const char* name) {
    for (const char* p = name; *p != '\0'; p++) {
        if (!isdigit(*p)) return false;
    }
    return true;
}

static bool load_all_de_keys() {
    auto de_dir = user_key_dir + "/de";
    auto dirp = std::unique_ptr<DIR, int (*)(DIR*)>(opendir(de_dir.c_str()), closedir);
    if (!dirp) {
        PLOG(ERROR) << "Unable to read de key directory";
        return false;
    }
    for (;;) {
        errno = 0;
        auto entry = readdir(dirp.get());
        if (!entry) {
            if (errno) {
                PLOG(ERROR) << "Unable to read de key directory";
                return false;
            }
            break;
        }
        if (entry->d_type != DT_DIR || !is_numeric(entry->d_name)) {
            LOG(DEBUG) << "Skipping non-de-key " << entry->d_name;
            continue;
        }
        userid_t user_id = atoi(entry->d_name);
        if (s_de_key_raw_refs.count(user_id) == 0) {
            auto key_path = de_dir + "/" + entry->d_name;
            std::string key;
            if (!android::vold::retrieveKey(key_path, kEmptyAuthentication, &key)) return false;
            std::string raw_ref;
            if (!install_key(key, &raw_ref)) return false;
            s_de_key_raw_refs[user_id] = raw_ref;
            LOG(DEBUG) << "Installed de key for user " << user_id;

            std::string user_prop = "twrp.user." + std::to_string(user_id) + ".decrypt";
            property_set(user_prop.c_str(), "0");
        }
    }
    // ext4enc:TODO: go through all DE directories, ensure that all user dirs have the
    // correct policy set on them, and that no rogue ones exist.
    return true;
}

bool e4crypt_initialize_global_de() {

    if (s_global_de_initialized) {
        LOG(INFO) << "Already initialized\n";
        return true;
    }

    std::string device_key;
    if (path_exists(device_key_path)) {
        if (!android::vold::retrieveKey(device_key_path,
                kEmptyAuthentication, &device_key)) return false;
    } else {
        LOG(INFO) << "NOT Creating new key\n";
        return false;
    }

    std::string device_key_ref;
    if (!install_key(device_key, &device_key_ref)) {
        LOG(ERROR) << "Failed to install device key\n";
        return false;
    }

    s_global_de_initialized = true;
    de_raw_ref = device_key_ref;
    return true;
}

bool e4crypt_init_user0() {
    if (e4crypt_is_native()) {
        if (!prepare_dir(user_key_dir, 0700, AID_ROOT, AID_ROOT)) return false;
        if (!prepare_dir(user_key_dir + "/ce", 0700, AID_ROOT, AID_ROOT)) return false;
        if (!prepare_dir(user_key_dir + "/de", 0700, AID_ROOT, AID_ROOT)) return false;
        if (!path_exists(get_de_key_path(0))) {
            //if (!create_and_install_user_keys(0, false)) return false;
            printf("de key path not found\n");
            return false;
        }
        // TODO: switch to loading only DE_0 here once framework makes
        // explicit calls to install DE keys for secondary users
        if (!load_all_de_keys()) return false;
    }

    // If this is a non-FBE device that recently left an emulated mode,
    // restore user data directories to known-good state.
    if (!e4crypt_is_native() && !e4crypt_is_emulated()) {
        e4crypt_unlock_user_key(0, 0, "!", "!");
    }

    return true;
}

static bool parse_hex(const char* hex, std::string* result) {
    if (strcmp("!", hex) == 0) {
        *result = "";
        return true;
    }
    if (android::vold::HexToStr(hex, *result) != 0) {
        LOG(ERROR) << "Invalid FBE hex string";  // Don't log the string for security reasons
        return false;
    }
    return true;
}

// TODO: rename to 'install' for consistency, and take flags to know which keys to install
bool e4crypt_unlock_user_key(userid_t user_id, int serial __unused, const char* token_hex,
                             const char* secret_hex) {
    if (e4crypt_is_native()) {
        if (s_ce_key_raw_refs.count(user_id) != 0) {
            LOG(WARNING) << "Tried to unlock already-unlocked key for user " << user_id;
            return true;
        }
        std::string token, secret;
        if (!parse_hex(token_hex, &token)) return false;
        if (!parse_hex(secret_hex, &secret)) return false;
        android::vold::KeyAuthentication auth(token, secret);
        if (!read_and_install_user_ce_key(user_id, auth)) {
            LOG(ERROR) << "Couldn't read key for " << user_id;
            return false;
        }
    } else {
		printf("Emulation mode not supported in TWRP\n");
    }
    return true;
}
