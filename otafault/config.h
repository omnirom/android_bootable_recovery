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

/*
 * Read configuration files in the OTA package to determine which files, if any, will trigger errors.
 *
 * OTA packages can be modified to trigger errors by adding a top-level
 * directory called .libotafault, which may optionally contain up to three
 * files called READ, WRITE, and FSYNC. Each one of these optional files
 * contains the name of a single file on the device disk which will cause
 * an IO error on the first call of the appropriate I/O action to that file.
 *
 * Example:
 * ota.zip
 *   <normal package contents>
 *   .libotafault
 *     WRITE
 *
 * If the contents of the file WRITE were /system/build.prop, the first write
 * action to /system/build.prop would fail with EIO. Note that READ and
 * FSYNC files are absent, so these actions will not cause an error.
 */

#ifndef _UPDATER_OTA_IO_CFG_H_
#define _UPDATER_OTA_IO_CFG_H_

#include <string>

#include <stdbool.h>

#include "minzip/Zip.h"

#define OTAIO_BASE_DIR ".libotafault"
#define OTAIO_READ "READ"
#define OTAIO_WRITE "WRITE"
#define OTAIO_FSYNC "FSYNC"
#define OTAIO_CACHE "CACHE"

/*
 * Initialize libotafault by providing a reference to the OTA package.
 */
void ota_io_init(ZipArchive* za);

/*
 * Return true if a config file is present for the given IO type.
 */
bool should_fault_inject(const char* io_type);

/*
 * Return true if an EIO should occur on the next hit to /cache/saved.file
 * instead of the next hit to the specified file.
 */
bool should_hit_cache();

/*
 * Return the name of the file that should cause an error for the
 * given IO type.
 */
std::string fault_fname(const char* io_type);

#endif
