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

#include <stdint.h>

#include <string>
#include <string_view>

struct ZipArchive;
typedef ZipArchive* ZipArchiveHandle;

class UpdaterRuntimeInterface;

class UpdaterInterface {
 public:
  virtual ~UpdaterInterface() = default;

  // Writes the message to command pipe, adds a new line in the end.
  virtual void WriteToCommandPipe(const std::string_view message, bool flush = false) const = 0;

  // Sends over the message to recovery to print it on the screen.
  virtual void UiPrint(const std::string_view message) const = 0;

  // Given the name of the block device, returns |name| for updates on the device; or the file path
  // to the fake block device for simulations.
  virtual std::string FindBlockDeviceName(const std::string_view name) const = 0;

  virtual UpdaterRuntimeInterface* GetRuntime() const = 0;
  virtual ZipArchiveHandle GetPackageHandle() const = 0;
  virtual std::string GetResult() const = 0;
  virtual uint8_t* GetMappedPackageAddress() const = 0;
  virtual size_t GetMappedPackageLength() const = 0;
};
