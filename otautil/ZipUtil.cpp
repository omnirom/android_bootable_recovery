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

#include "ZipUtil.h"

#include <errno.h>
#include <fcntl.h>
#include <utime.h>

#include <string>

#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <selinux/label.h>
#include <selinux/selinux.h>
#include <ziparchive/zip_archive.h>

#include "DirUtil.h"

static constexpr mode_t UNZIP_DIRMODE = 0755;
static constexpr mode_t UNZIP_FILEMODE = 0644;

bool ExtractPackageRecursive(ZipArchiveHandle zip, const std::string& zip_path,
                             const std::string& dest_path, const struct utimbuf* timestamp,
                             struct selabel_handle* sehnd) {
    if (!zip_path.empty() && zip_path[0] == '/') {
        LOG(ERROR) << "ExtractPackageRecursive(): zip_path must be a relative path " << zip_path;
        return false;
    }
    if (dest_path.empty() || dest_path[0] != '/') {
        LOG(ERROR) << "ExtractPackageRecursive(): dest_path must be an absolute path " << dest_path;
        return false;
    }

    void* cookie;
    std::string target_dir(dest_path);
    if (dest_path.back() != '/') {
        target_dir += '/';
    }
    std::string prefix_path(zip_path);
    if (!zip_path.empty() && zip_path.back() != '/') {
        prefix_path += '/';
    }
    const ZipString zip_prefix(prefix_path.c_str());

    int ret = StartIteration(zip, &cookie, &zip_prefix, nullptr);
    if (ret != 0) {
        LOG(ERROR) << "failed to start iterating zip entries.";
        return false;
    }

    std::unique_ptr<void, decltype(&EndIteration)> guard(cookie, EndIteration);
    ZipEntry entry;
    ZipString name;
    int extractCount = 0;
    while (Next(cookie, &entry, &name) == 0) {
        std::string entry_name(name.name, name.name + name.name_length);
        CHECK_LE(prefix_path.size(), entry_name.size());
        std::string path = target_dir + entry_name.substr(prefix_path.size());
        // Skip dir.
        if (path.back() == '/') {
            continue;
        }

        if (dirCreateHierarchy(path.c_str(), UNZIP_DIRMODE, timestamp, true, sehnd) != 0) {
            LOG(ERROR) << "failed to create dir for " << path;
            return false;
        }

        char *secontext = NULL;
        if (sehnd) {
            selabel_lookup(sehnd, &secontext, path.c_str(), UNZIP_FILEMODE);
            setfscreatecon(secontext);
        }
        android::base::unique_fd fd(open(path.c_str(), O_CREAT|O_WRONLY|O_TRUNC, UNZIP_FILEMODE));
        if (fd == -1) {
            PLOG(ERROR) << "Can't create target file \"" << path << "\"";
            return false;
        }
        if (secontext) {
            freecon(secontext);
            setfscreatecon(NULL);
        }

        int err = ExtractEntryToFile(zip, &entry, fd);
        if (err != 0) {
            LOG(ERROR) << "Error extracting \"" << path << "\" : " << ErrorCodeString(err);
            return false;
        }

        if (fsync(fd) != 0) {
            PLOG(ERROR) << "Error syncing file descriptor when extracting \"" << path << "\"";
            return false;
        }

        if (timestamp != nullptr && utime(path.c_str(), timestamp)) {
            PLOG(ERROR) << "Error touching \"" << path << "\"";
            return false;
        }

        LOG(INFO) << "Extracted file \"" << path << "\"";
        ++extractCount;
    }

    LOG(INFO) << "Extracted " << extractCount << " file(s)";
    return true;
}
