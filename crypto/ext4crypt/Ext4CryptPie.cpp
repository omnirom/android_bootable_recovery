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

#include "Ext4CryptPie.h"

#include "Keymaster4.h"
#include "KeyStorage4.h"
#include "KeyUtil.h"
#include "Utils.h"
#include "Decrypt.h"
//#include "VoldUtil.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <selinux/android.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <private/android_filesystem_config.h>

//#include "android/os/IVold.h"

//#include "cryptfs.h"

#define EMULATED_USES_SELINUX 0
#define MANAGE_MISC_DIRS 0

#include <cutils/fs.h>
#include <cutils/properties.h>

#include <ext4_utils/ext4_crypt.h>
#include <keyutils.h>

#include <android-base/file.h>
//#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>

#include <iostream>
#define LOG(x) std::cout
#define PLOG(x) std::cout
#define DATA_MNT_POINT "/data"

using android::base::StringPrintf;
using android::base::WriteStringToFile;
using android::vold::kEmptyAuthentication;
using android::vold::KeyBuffer;
using android::vold::Keymaster;
using android::hardware::keymaster::V4_0::KeyFormat;

// Store main DE raw ref / policy
std::string de_raw_ref;
// Map user ids to key references
std::map<userid_t, std::string> s_de_key_raw_refs;
std::map<userid_t, std::string> s_ce_key_raw_refs;
// TODO abolish this map, per b/26948053
std::map<userid_t, KeyBuffer> s_ce_keys;

namespace {

struct PolicyKeyRef {
    std::string contents_mode;
    std::string filenames_mode;
    std::string key_raw_ref;
};

const std::string device_key_dir = std::string() + DATA_MNT_POINT + e4crypt_unencrypted_folder;
const std::string device_key_path = device_key_dir + "/key";
const std::string device_key_temp = device_key_dir + "/temp";

const std::string user_key_dir = std::string() + DATA_MNT_POINT + "/misc/vold/user_keys";
const std::string user_key_temp = user_key_dir + "/temp";
const std::string prepare_subdirs_path = "/system/bin/vold_prepare_subdirs";

const std::string systemwide_volume_key_dir =
    std::string() + DATA_MNT_POINT + "/misc/vold/volume_keys";

bool s_global_de_initialized = false;

// Some users are ephemeral, don't try to wipe their keys from disk
std::set<userid_t> s_ephemeral_users;

}

static bool e4crypt_is_emulated() {
    return property_get_bool("persist.sys.emulate_fbe", false);
}

/*static const char* escape_empty(const std::string& value) {
    return value.empty() ? "null" : value.c_str();
}*/

static std::string get_de_key_path(userid_t user_id) {
    return StringPrintf("%s/de/%d", user_key_dir.c_str(), user_id);
}

static std::string get_ce_key_directory_path(userid_t user_id) {
    return StringPrintf("%s/ce/%d", user_key_dir.c_str(), user_id);
}

// Returns the keys newest first
static std::vector<std::string> get_ce_key_paths(const std::string& directory_path) {
    auto dirp = std::unique_ptr<DIR, int (*)(DIR*)>(opendir(directory_path.c_str()), closedir);
    if (!dirp) {
        PLOG(ERROR) << "Unable to open ce key directory: " + directory_path << std::endl;
        return std::vector<std::string>();
    }
    std::vector<std::string> result;
    for (;;) {
        errno = 0;
        auto const entry = readdir(dirp.get());
        if (!entry) {
            if (errno) {
                PLOG(ERROR) << "Unable to read ce key directory: " + directory_path << std::endl;
                return std::vector<std::string>();
            }
            break;
        }
        if (entry->d_type != DT_DIR || entry->d_name[0] != 'c') {
            LOG(DEBUG) << "Skipping non-key " << entry->d_name << std::endl;
            continue;
        }
        result.emplace_back(directory_path + "/" + entry->d_name);
    }
    std::sort(result.begin(), result.end());
    std::reverse(result.begin(), result.end());
    return result;
}

static std::string get_ce_key_current_path(const std::string& directory_path) {
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
        LOG(DEBUG) << "Renaming " << to_fix << " to " << current_path << std::endl;
        if (rename(to_fix.c_str(), current_path.c_str()) != 0) {
            PLOG(WARNING) << "Unable to rename " << to_fix << " to " << current_path << std::endl;
        }
    }
}

static bool read_and_fixate_user_ce_key(userid_t user_id,
                                        const android::vold::KeyAuthentication& auth,
                                        KeyBuffer *ce_key) {
    auto const directory_path = get_ce_key_directory_path(user_id);
    auto const paths = get_ce_key_paths(directory_path);
    for (auto const ce_key_path: paths) {
        LOG(DEBUG) << "Trying user CE key " << ce_key_path << std::endl;
        if (android::vold::retrieveKey(ce_key_path, auth, ce_key)) {
            LOG(DEBUG) << "Successfully retrieved key" << std::endl;
            fixate_user_ce_key(directory_path, ce_key_path, paths);
            return true;
        }
    }
    LOG(ERROR) << "Failed to find working ce key for user " << user_id << std::endl;
    return false;
}

static bool is_wrapped_key_supported_common(const std::string& mount_point) {
    LOG(DEBUG) << "Determining wrapped-key support for " << mount_point << std::endl;
    std::string wrapped_key_supported = android::base::GetProperty("fbe.data.wrappedkey", "false");
    LOG(DEBUG) << "fbe.data.wrappedkey = " << wrapped_key_supported << std::endl;
    if (mount_point == DATA_MNT_POINT && wrapped_key_supported == "true") {
        LOG(DEBUG) << "Wrapped key supported on " << mount_point << std::endl;
        return true;
    } else {
        return false;
    }
}

bool is_wrapped_key_supported() {
    return is_wrapped_key_supported_common(DATA_MNT_POINT);
}

bool is_wrapped_key_supported_external() {
    return false;
}

static bool read_and_install_user_ce_key(userid_t user_id,
                                         const android::vold::KeyAuthentication& auth) {
    if (s_ce_key_raw_refs.count(user_id) != 0) return true;
    KeyBuffer ce_key;
    if (!read_and_fixate_user_ce_key(user_id, auth, &ce_key)) return false;
    std::string ce_raw_ref;

    if (is_wrapped_key_supported()) {
        KeyBuffer ephemeral_wrapped_key;
        if (!getEphemeralWrappedKey(KeyFormat::RAW, ce_key, &ephemeral_wrapped_key)) {
           LOG(ERROR) << "Failed to export ce key";
           return false;
        }

        ce_key = std::move(ephemeral_wrapped_key);
    }
    if (!android::vold::installKey(ce_key, &ce_raw_ref)) return false;
    s_ce_keys[user_id] = std::move(ce_key);
    s_ce_key_raw_refs[user_id] = ce_raw_ref;
    LOG(DEBUG) << "Installed ce key for user " << user_id << std::endl;
    return true;
}

static bool prepare_dir(const std::string& dir, mode_t mode, uid_t uid, gid_t gid) {
    LOG(DEBUG) << "Preparing: " << dir << std::endl;
    if (fs_prepare_dir(dir.c_str(), mode, uid, gid) != 0) {
        PLOG(ERROR) << "Failed to prepare " << dir << std::endl;
        return false;
    }
    return true;
}

// NB this assumes that there is only one thread listening for crypt commands, because
// it creates keys in a fixed location.
static bool create_and_install_user_keys(userid_t user_id, bool create_ephemeral) {
    /*KeyBuffer de_key, ce_key;
    if (!android::vold::randomKey(&de_key)) return false;
    if (!android::vold::randomKey(&ce_key)) return false;
    if (create_ephemeral) {
        // If the key should be created as ephemeral, don't store it.
        s_ephemeral_users.insert(user_id);
    } else {
        auto const directory_path = get_ce_key_directory_path(user_id);
        if (!prepare_dir(directory_path, 0700, AID_ROOT, AID_ROOT)) return false;
        auto const paths = get_ce_key_paths(directory_path);
        std::string ce_key_path;
        if (!get_ce_key_new_path(directory_path, paths, &ce_key_path)) return false;
        if (!android::vold::storeKeyAtomically(ce_key_path, user_key_temp,
                kEmptyAuthentication, ce_key)) return false;
        fixate_user_ce_key(directory_path, ce_key_path, paths);
        // Write DE key second; once this is written, all is good.
        if (!android::vold::storeKeyAtomically(get_de_key_path(user_id), user_key_temp,
                kEmptyAuthentication, de_key)) return false;
    }
    std::string de_raw_ref;
    if (!android::vold::installKey(de_key, &de_raw_ref)) return false;
    s_de_key_raw_refs[user_id] = de_raw_ref;
    std::string ce_raw_ref;
    if (!android::vold::installKey(ce_key, &ce_raw_ref)) return false;
    s_ce_keys[user_id] = ce_key;
    s_ce_key_raw_refs[user_id] = ce_raw_ref;
    LOG(DEBUG) << "Created keys for user " << user_id;*/
    LOG(DEBUG) << "TWRP not doing create_and_install_user_keys\n";
    return true;
}

bool lookup_key_ref(const std::map<userid_t, std::string>& key_map, userid_t user_id,
                           std::string* raw_ref) {
    auto refi = key_map.find(user_id);
    if (refi == key_map.end()) {
        LOG(ERROR) << "Cannot find key for " << user_id << std::endl;
        return false;
    }
    *raw_ref = refi->second;
    return true;
}

static void get_data_file_encryption_modes(PolicyKeyRef* key_ref) {
    /*struct fstab_rec* rec = fs_mgr_get_entry_for_mount_point(fstab_default, DATA_MNT_POINT);
    char const* contents_mode = strdup("ice");
    char const* filenames_mode = strdup("aes-256-heh");
    fs_mgr_get_file_encryption_modes(rec, &contents_mode, &filenames_mode);
    key_ref->contents_mode = contents_mode;
    key_ref->filenames_mode = filenames_mode;*/
    LOG(INFO) << "contents mode '" << android::base::GetProperty("fbe.contents", "aes-256-xts") << "' filenames '" << android::base::GetProperty("fbe.filenames", "aes-256-heh") << "'\n";
    key_ref->contents_mode =
        android::base::GetProperty("fbe.contents", "aes-256-xts");
    key_ref->filenames_mode =
        android::base::GetProperty("fbe.filenames", "aes-256-heh");
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
        PLOG(ERROR) << "Unable to read de key directory" << std::endl;
        return false;
    }
    for (;;) {
        errno = 0;
        auto entry = readdir(dirp.get());
        if (!entry) {
            if (errno) {
                PLOG(ERROR) << "Unable to read de key directory" << std::endl;
                return false;
            }
            break;
        }
        if (entry->d_type != DT_DIR || !is_numeric(entry->d_name)) {
            LOG(DEBUG) << "Skipping non-de-key " << entry->d_name << std::endl;
            continue;
        }
        userid_t user_id = std::stoi(entry->d_name);
        if (s_de_key_raw_refs.count(user_id) == 0) {
            auto key_path = de_dir + "/" + entry->d_name;
            KeyBuffer key;
            if (!android::vold::retrieveKey(key_path, kEmptyAuthentication, &key)) return false;
            std::string raw_ref;
            if (is_wrapped_key_supported()) {
                KeyBuffer ephemeral_wrapped_key;
                if (!getEphemeralWrappedKey(KeyFormat::RAW, key, &ephemeral_wrapped_key)) {
                   LOG(ERROR) << "Failed to export de_key in create_and_install_user_keys";
                   return false;
                }
                key = std::move(ephemeral_wrapped_key);
            }
            if (!android::vold::installKey(key, &raw_ref)) return false;
            s_de_key_raw_refs[user_id] = raw_ref;
            LOG(DEBUG) << "Installed de key for user " << user_id << std::endl;

            std::string user_prop = "twrp.user." + std::to_string(user_id) + ".decrypt";
            property_set(user_prop.c_str(), "0");
        }
    }
    // ext4enc:TODO: go through all DE directories, ensure that all user dirs have the
    // correct policy set on them, and that no rogue ones exist.
    return true;
}

bool e4crypt_initialize_global_de() {
    LOG(INFO) << "e4crypt_initialize_global_de" << std::endl;
    bool wrapped_key_supported = false;

    if (s_global_de_initialized) {
        LOG(INFO) << "Already initialized" << std::endl;
        return true;
    }

    PolicyKeyRef device_ref;
    wrapped_key_supported = is_wrapped_key_supported();
    LOG(INFO) << "calling retrieveAndInstallKey\n";
    if (!android::vold::retrieveAndInstallKey(true, kEmptyAuthentication, device_key_path,
                                              device_key_temp, &device_ref.key_raw_ref, wrapped_key_supported))
        return false;
    get_data_file_encryption_modes(&device_ref);

    std::string modestring = device_ref.contents_mode + ":" + device_ref.filenames_mode;
    std::string mode_filename = std::string("/data") + e4crypt_key_mode;
    if (!android::base::WriteStringToFile(modestring, mode_filename)) {
        PLOG(ERROR) << "Cannot save type" << std::endl;
        return false;
    }

    std::string ref_filename = std::string("/data") + e4crypt_key_ref;
    if (!android::base::WriteStringToFile(device_ref.key_raw_ref, ref_filename)) {
        PLOG(ERROR) << "Cannot save key reference to:" << ref_filename << std::endl;
        return false;
    }
    LOG(INFO) << "Wrote system DE key reference to:" << ref_filename << std::endl;

    s_global_de_initialized = true;
    de_raw_ref = device_ref.key_raw_ref;
    return true;
}

bool e4crypt_init_user0() {
    LOG(DEBUG) << "e4crypt_init_user0\n";
    if (e4crypt_is_native()) {
        if (!prepare_dir(user_key_dir, 0700, AID_ROOT, AID_ROOT)) return false;
        if (!prepare_dir(user_key_dir + "/ce", 0700, AID_ROOT, AID_ROOT)) return false;
        if (!prepare_dir(user_key_dir + "/de", 0700, AID_ROOT, AID_ROOT)) return false;
        if (!android::vold::pathExists(get_de_key_path(0))) {
            if (!create_and_install_user_keys(0, false)) return false;
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

static bool emulated_unlock(const std::string& path, mode_t mode) {
    if (chmod(path.c_str(), mode) != 0) {
        PLOG(ERROR) << "Failed to chmod " << path << std::endl;
        // FIXME temporary workaround for b/26713622
        if (e4crypt_is_emulated()) return false;
    }
#if EMULATED_USES_SELINUX
    if (selinux_android_restorecon(path.c_str(), SELINUX_ANDROID_RESTORECON_FORCE) != 0) {
        PLOG(WARNING) << "Failed to restorecon " << path << std::endl;
        // FIXME temporary workaround for b/26713622
        if (e4crypt_is_emulated()) return false;
    }
#endif
    return true;
}

static bool parse_hex(const std::string& hex, std::string* result) {
    if (hex == "!") {
        *result = "";
        return true;
    }
    if (android::vold::HexToStr(hex, *result) != 0) {
        LOG(ERROR) << "Invalid FBE hex string" << std::endl;  // Don't log the string for security reasons
        return false;
    }
    return true;
}

// TODO: rename to 'install' for consistency, and take flags to know which keys to install
bool e4crypt_unlock_user_key(userid_t user_id, int serial, const std::string& token_hex,
                             const std::string& secret_hex) {
    LOG(DEBUG) << "e4crypt_unlock_user_key " << user_id << " serial=" << serial
               << " token_present=" << (token_hex != "!") << std::endl;
    if (e4crypt_is_native()) {
        if (s_ce_key_raw_refs.count(user_id) != 0) {
            LOG(WARNING) << "Tried to unlock already-unlocked key for user " << user_id << std::endl;
            return true;
        }
        std::string token, secret;
        if (!parse_hex(token_hex, &token)) return false;
        if (!parse_hex(secret_hex, &secret)) return false;
        android::vold::KeyAuthentication auth(token, secret);
        if (!read_and_install_user_ce_key(user_id, auth)) {
            LOG(ERROR) << "Couldn't read key for " << user_id << std::endl;
            return false;
        }
    } else {
        // When in emulation mode, we just use chmod. However, we also
        // unlock directories when not in emulation mode, to bring devices
        // back into a known-good state.
        if (!emulated_unlock(android::vold::BuildDataSystemCePath(user_id), 0771) ||
            !emulated_unlock(android::vold::BuildDataMiscCePath(user_id), 01771) ||
            !emulated_unlock(android::vold::BuildDataMediaCePath("", user_id), 0770) ||
            !emulated_unlock(android::vold::BuildDataUserCePath("", user_id), 0771)) {
            LOG(ERROR) << "Failed to unlock user " << user_id << std::endl;
            return false;
        }
    }
    return true;
}
