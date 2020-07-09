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

#ifndef ANDROID_VOLD_UTILS_H
#define ANDROID_VOLD_UTILS_H

#include "KeyBuffer.h"

#include <android-base/macros.h>
#include <cutils/multiuser.h>
#include <selinux/selinux.h>
#include <utils/Errors.h>

#include <chrono>
#include <string>
#include <vector>

struct DIR;

namespace android {
namespace vold {

/* SELinux contexts used depending on the block device type */
extern security_context_t sBlkidContext;
extern security_context_t sBlkidUntrustedContext;
extern security_context_t sFsckContext;
extern security_context_t sFsckUntrustedContext;

// TODO remove this with better solution, b/64143519
extern bool sSleepOnUnmount;

status_t CreateDeviceNode(const std::string& path, dev_t dev);
status_t DestroyDeviceNode(const std::string& path);

/* fs_prepare_dir wrapper that creates with SELinux context */
status_t PrepareDir(const std::string& path, mode_t mode, uid_t uid, gid_t gid);

/* Really unmounts the path, killing active processes along the way */
status_t ForceUnmount(const std::string& path);

/* Kills any processes using given path */
status_t KillProcessesUsingPath(const std::string& path);

/* Creates bind mount from source to target */
status_t BindMount(const std::string& source, const std::string& target);

/** Creates a symbolic link to target */
status_t Symlink(const std::string& target, const std::string& linkpath);

/** Calls unlink(2) at linkpath */
status_t Unlink(const std::string& linkpath);

/** Creates the given directory if it is not already available */
status_t CreateDir(const std::string& dir, mode_t mode);

bool FindValue(const std::string& raw, const std::string& key, std::string* value);

/* Reads filesystem metadata from device at path */
status_t ReadMetadata(const std::string& path, std::string* fsType, std::string* fsUuid,
                      std::string* fsLabel);

/* Reads filesystem metadata from untrusted device at path */
status_t ReadMetadataUntrusted(const std::string& path, std::string* fsType, std::string* fsUuid,
                               std::string* fsLabel);

/* Returns either WEXITSTATUS() status, or a negative errno */
status_t ForkExecvp(const std::vector<std::string>& args, std::vector<std::string>* output = nullptr,
                    security_context_t context = nullptr);

pid_t ForkExecvpAsync(const std::vector<std::string>& args);

/* Gets block device size in bytes */
status_t GetBlockDevSize(int fd, uint64_t* size);
status_t GetBlockDevSize(const std::string& path, uint64_t* size);
/* Gets block device size in 512 byte sectors */
status_t GetBlockDev512Sectors(const std::string& path, uint64_t* nr_sec);

status_t ReadRandomBytes(size_t bytes, std::string& out);
status_t ReadRandomBytes(size_t bytes, char* buffer);
status_t GenerateRandomUuid(std::string& out);

/* Converts hex string to raw bytes, ignoring [ :-] */
status_t HexToStr(const std::string& hex, std::string& str);
/* Converts raw bytes to hex string */
status_t StrToHex(const std::string& str, std::string& hex);
/* Converts raw key bytes to hex string */
status_t StrToHex(const KeyBuffer& str, KeyBuffer& hex);
/* Normalize given hex string into consistent format */
status_t NormalizeHex(const std::string& in, std::string& out);

uint64_t GetFreeBytes(const std::string& path);
uint64_t GetTreeBytes(const std::string& path);

bool IsFilesystemSupported(const std::string& fsType);

/* Wipes contents of block device at given path */
status_t WipeBlockDevice(const std::string& path);

std::string BuildKeyPath(const std::string& partGuid);

std::string BuildDataSystemLegacyPath(userid_t userid);
std::string BuildDataSystemCePath(userid_t userid);
std::string BuildDataSystemDePath(userid_t userid);
std::string BuildDataMiscLegacyPath(userid_t userid);
std::string BuildDataMiscCePath(userid_t userid);
std::string BuildDataMiscDePath(userid_t userid);
std::string BuildDataProfilesDePath(userid_t userid);
std::string BuildDataVendorCePath(userid_t userid);
std::string BuildDataVendorDePath(userid_t userid);

std::string BuildDataPath(const std::string& volumeUuid);
std::string BuildDataMediaCePath(const std::string& volumeUuid, userid_t userid);
std::string BuildDataUserCePath(const std::string& volumeUuid, userid_t userid);
std::string BuildDataUserDePath(const std::string& volumeUuid, userid_t userid);

dev_t GetDevice(const std::string& path);

status_t RestoreconRecursive(const std::string& path);

// TODO: promote to android::base
bool Readlinkat(int dirfd, const std::string& path, std::string* result);

/* Checks if Android is running in QEMU */
bool IsRunningInEmulator();

status_t UnmountTreeWithPrefix(const std::string& prefix);
status_t UnmountTree(const std::string& mountPoint);

status_t DeleteDirContentsAndDir(const std::string& pathname);
status_t DeleteDirContents(const std::string& pathname);

status_t WaitForFile(const char* filename, std::chrono::nanoseconds timeout);

bool FsyncDirectory(const std::string& dirname);

bool writeStringToFile(const std::string& payload, const std::string& filename);
}  // namespace vold
}  // namespace android

#endif
