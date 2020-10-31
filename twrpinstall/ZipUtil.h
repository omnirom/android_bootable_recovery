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

#ifndef _OTAUTIL_ZIPUTIL_H
#define _OTAUTIL_ZIPUTIL_H

#include <utime.h>

#include <string>

#include <selinux/label.h>
#include <ziparchive/zip_archive.h>

/*
 * Inflate all files under zip_path to the directory specified by
 * dest_path, which must exist and be a writable directory. The zip_path
 * is allowed to be an empty string, in which case the whole package
 * will be extracted.
 *
 * Directory entries are not extracted.
 *
 * The immediate children of zip_path will become the immediate
 * children of dest_path; e.g., if the archive contains the entries
 *
 *     a/b/c/one
 *     a/b/c/two
 *     a/b/c/d/three
 *
 * and ExtractPackageRecursive(a, "a/b/c", "/tmp", ...) is called, the resulting
 * files will be
 *
 *     /tmp/one
 *     /tmp/two
 *     /tmp/d/three
 *
 * If timestamp is non-NULL, file timestamps will be set accordingly.
 *
 * Returns true on success, false on failure.
 */
bool ExtractPackageRecursive(ZipArchiveHandle zip, const std::string& zip_path,
                             const std::string& dest_path, const struct utimbuf* timestamp,
                             struct selabel_handle* sehnd);

#endif // _OTAUTIL_ZIPUTIL_H
