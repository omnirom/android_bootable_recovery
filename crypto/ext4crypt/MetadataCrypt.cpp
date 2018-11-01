/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "KeyBuffer.h"
#include "MetadataCrypt.h"

#include <string>
#include <thread>
#include <vector>
#include <algorithm>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/dm-ioctl.h>

//#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/unique_fd.h>
#include <cutils/fs.h>
//#include <fs_mgr.h>

//#include "EncryptInplace.h"
#include "KeyStorage4.h"
#include "KeyUtil.h"
//#include "secontext.h"
#include "Utils.h"
//#include "VoldUtil.h"

#include <iostream>
#define LOG(x) std::cout
#define PLOG(x) std::cout
#include <linux/fs.h>

#define DM_CRYPT_BUF_SIZE 4096
#define TABLE_LOAD_RETRIES 10
#define DEFAULT_KEY_TARGET_TYPE "default-key"

using android::vold::KeyBuffer;

static const std::string kDmNameUserdata = "userdata";

void get_blkdev_size(int fd, unsigned long* nr_sec) {
  if ((ioctl(fd, BLKGETSIZE, nr_sec)) == -1) {
    *nr_sec = 0;
  }
}

static const char* kLookup = "0123456789abcdef";

android::status_t StrToHex(const KeyBuffer& str, KeyBuffer& hex) {
    hex.clear();
    for (size_t i = 0; i < str.size(); i++) {
        hex.push_back(kLookup[(str.data()[i] & 0xF0) >> 4]);
        hex.push_back(kLookup[str.data()[i] & 0x0F]);
    }
    return android::OK;
}

/*static bool mount_via_fs_mgr(const char* mount_point, const char* blk_device) {
    // fs_mgr_do_mount runs fsck. Use setexeccon to run trusted
    // partitions in the fsck domain.
    if (setexeccon(secontextFsck())) {
        PLOG(ERROR) << "Failed to setexeccon";
        return false;
    }
    auto mount_rc = fs_mgr_do_mount(fstab_default, const_cast<char*>(mount_point),
                                    const_cast<char*>(blk_device), nullptr);
    if (setexeccon(nullptr)) {
        PLOG(ERROR) << "Failed to clear setexeccon";
        return false;
    }
    if (mount_rc != 0) {
        LOG(ERROR) << "fs_mgr_do_mount failed with rc " << mount_rc;
        return false;
    }
    LOG(DEBUG) << "Mounted " << mount_point;
    return true;
}*/

static bool read_key(const std::string& key_dir, bool create_if_absent, KeyBuffer* key) {
    /*if (!data_rec->key_dir) {
        LOG(ERROR) << "Failed to get key_dir";
        return false;
    }
    std::string key_dir = data_rec->key_dir;*/
    auto dir = key_dir + "/key";
    LOG(DEBUG) << "key_dir/key: " << dir << "\n";
    /*if (fs_mkdirs(dir.c_str(), 0700)) {
        PLOG(ERROR) << "Creating directories: " << dir;
        return false;
    }*/
    auto temp = key_dir + "/tmp";
    if (!android::vold::retrieveKey(create_if_absent, dir, temp, key)) return false;
    return true;
}

static KeyBuffer default_key_params(const std::string& real_blkdev, const KeyBuffer& key) {
    KeyBuffer hex_key;
    if (/*android::vold::*/StrToHex(key, hex_key) != android::OK) {
        LOG(ERROR) << "Failed to turn key to hex\n";
        return KeyBuffer();
    }
    auto res = KeyBuffer() + "AES-256-XTS " + hex_key + " " + real_blkdev.c_str() + " 0";
    return res;
}

static bool get_number_of_sectors(const std::string& real_blkdev, uint64_t *nr_sec) {
    android::base::unique_fd dev_fd(TEMP_FAILURE_RETRY(open(
        real_blkdev.c_str(), O_RDONLY | O_CLOEXEC, 0)));
    if (dev_fd == -1) {
        PLOG(ERROR) << "Unable to open " << real_blkdev << " to measure size\n";
        return false;
    }
    unsigned long res;
    // TODO: should use BLKGETSIZE64
    get_blkdev_size(dev_fd.get(), &res);
    if (res == 0) {
        PLOG(ERROR) << "Unable to measure size of " << real_blkdev << "\n";
        return false;
    }
    *nr_sec = res;
    return true;
}

static struct dm_ioctl* dm_ioctl_init(char *buffer, size_t buffer_size,
                                      const std::string& dm_name) {
    if (buffer_size < sizeof(dm_ioctl)) {
        LOG(ERROR) << "dm_ioctl buffer too small\n";
        return nullptr;
    }

    memset(buffer, 0, buffer_size);
    struct dm_ioctl* io = (struct dm_ioctl*) buffer;
    io->data_size = buffer_size;
    io->data_start = sizeof(struct dm_ioctl);
    io->version[0] = 4;
    io->version[1] = 0;
    io->version[2] = 0;
    io->flags = 0;
    dm_name.copy(io->name, sizeof(io->name));
    return io;
}

static bool create_crypto_blk_dev(const std::string& dm_name, uint64_t nr_sec,
                                  const std::string& target_type, const KeyBuffer& crypt_params,
                                  std::string* crypto_blkdev) {
    PLOG(INFO) << "starting create_crypto_blk_dev\n";
    android::base::unique_fd dm_fd(TEMP_FAILURE_RETRY(open(
        "/dev/device-mapper", O_RDWR | O_CLOEXEC, 0)));
    if (dm_fd == -1) {
        PLOG(ERROR) << "Cannot open device-mapper\n";
        return false;
    }
    alignas(struct dm_ioctl) char buffer[DM_CRYPT_BUF_SIZE];
    auto io = dm_ioctl_init(buffer, sizeof(buffer), dm_name);
    if (!io || ioctl(dm_fd.get(), DM_DEV_CREATE, io) != 0) {
        PLOG(ERROR) << "Cannot create dm-crypt device " << dm_name << "\n";
        return false;
    }

    // Get the device status, in particular, the name of its device file
    io = dm_ioctl_init(buffer, sizeof(buffer), dm_name);
    if (ioctl(dm_fd.get(), DM_DEV_STATUS, io) != 0) {
        PLOG(ERROR) << "Cannot retrieve dm-crypt device status " << dm_name << "\n";
        return false;
    }
    *crypto_blkdev = std::string() + "/dev/block/dm-" + std::to_string(
        (io->dev & 0xff) | ((io->dev >> 12) & 0xfff00));

    io = dm_ioctl_init(buffer, sizeof(buffer), dm_name);
    size_t paramix = io->data_start + sizeof(struct dm_target_spec);
    size_t nullix = paramix + crypt_params.size();
    size_t endix = (nullix + 1 + 7) & 8; // Add room for \0 and align to 8 byte boundary

    if (endix > sizeof(buffer)) {
        LOG(ERROR) << "crypt_params too big for DM_CRYPT_BUF_SIZE\n";
        return false;
    }

    io->target_count = 1;
    auto tgt = (struct dm_target_spec *) (buffer + io->data_start);
    tgt->status = 0;
    tgt->sector_start = 0;
    tgt->length = nr_sec;
    target_type.copy(tgt->target_type, sizeof(tgt->target_type));
    memcpy(buffer + paramix, crypt_params.data(),
            std::min(crypt_params.size(), sizeof(buffer) - paramix));
    buffer[nullix] = '\0';
    tgt->next = endix;

    for (int i = 0; ; i++) {
        if (ioctl(dm_fd.get(), DM_TABLE_LOAD, io) == 0) {
            break;
        }
        if (i+1 >= TABLE_LOAD_RETRIES) {
            PLOG(ERROR) << "DM_TABLE_LOAD ioctl failed\n";
            return false;
        }
        PLOG(INFO) << "DM_TABLE_LOAD ioctl failed, retrying\n";
        usleep(500000);
    }

    // Resume this device to activate it
    io = dm_ioctl_init(buffer, sizeof(buffer), dm_name);
    if (ioctl(dm_fd.get(), DM_DEV_SUSPEND, io)) {
        PLOG(ERROR) << "Cannot resume dm-crypt device " << dm_name << "\n";
        return false;
    }
    return true;
}

bool e4crypt_mount_metadata_encrypted(const std::string& mount_point, bool needs_encrypt, const std::string& key_dir, const std::string& blk_device, std::string* crypto_blkdev) {
    LOG(DEBUG) << "e4crypt_mount_metadata_encrypted: " << mount_point << " " << needs_encrypt << "\n";
    /*auto encrypted_state = android::base::GetProperty("ro.crypto.state", "");
    if (encrypted_state != "") {
        LOG(DEBUG) << "e4crypt_enable_crypto got unexpected starting state: " << encrypted_state;
        return false;
    }
    auto data_rec = fs_mgr_get_entry_for_mount_point(fstab_default, mount_point);
    if (!data_rec) {
        LOG(ERROR) << "Failed to get data_rec";
        return false;
    }*/
    KeyBuffer key;
    if (!read_key(key_dir, needs_encrypt, &key)) return false;
    uint64_t nr_sec;
    if (!get_number_of_sectors(blk_device, &nr_sec)) return false;
    //std::string crypto_blkdev;
    if (!create_crypto_blk_dev(kDmNameUserdata, nr_sec, DEFAULT_KEY_TARGET_TYPE,
                               default_key_params(blk_device, key), /*&*/crypto_blkdev))
        return false;
    // FIXME handle the corrupt case
    /*if (needs_encrypt) {
        LOG(INFO) << "Beginning inplace encryption, nr_sec: " << nr_sec;
        off64_t size_already_done = 0;
        auto rc =
            cryptfs_enable_inplace(const_cast<char*>(crypto_blkdev.c_str()), data_rec->blk_device,
                                   nr_sec, &size_already_done, nr_sec, 0, false);
        if (rc != 0) {
            LOG(ERROR) << "Inplace crypto failed with code: " << rc;
            return false;
        }
        if (static_cast<uint64_t>(size_already_done) != nr_sec) {
            LOG(ERROR) << "Inplace crypto only got up to sector: " << size_already_done;
            return false;
        }
        LOG(INFO) << "Inplace encryption complete";
    }

    LOG(DEBUG) << "Mounting metadata-encrypted filesystem:" << mount_point;
    mount_via_fs_mgr(data_rec->mount_point, crypto_blkdev.c_str());*/
    LOG(DEBUG) << "crypto block device '" << *crypto_blkdev << "\n";
    return true;
}
