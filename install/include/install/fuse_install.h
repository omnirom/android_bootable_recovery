/*
 * Copyright (C) 2019 The Android Open Source Project
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

#pragma once

#include <string_view>

#include "install/install.h"
#include "recovery_ui/device.h"
#include "recovery_ui/ui.h"

// Starts FUSE with the package from |path| as the data source. And installs the package from
// |FUSE_SIDELOAD_HOST_PATHNAME|. The |path| can point to the location of a package zip file or a
// block map file with the prefix '@'; e.g. /sdcard/package.zip, @/cache/recovery/block.map.
InstallResult InstallWithFuseFromPath(std::string_view path, RecoveryUI* ui);

InstallResult ApplyFromSdcard(Device* device);
