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

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <ziparchive/zip_archive.h>

#include "verifier.h"

enum class PackageType {
  kMemory,
  kFile,
};

// This class serves as a wrapper for an OTA update package. It aims to provide the common
// interface for both packages loaded in memory and packages read from fd.
class Package : public VerifierInterface {
 public:
  static std::unique_ptr<Package> CreateMemoryPackage(
      const std::string& path, const std::function<void(float)>& set_progress);
  static std::unique_ptr<Package> CreateMemoryPackage(
      std::vector<uint8_t> content, const std::function<void(float)>& set_progress);
  static std::unique_ptr<Package> CreateFilePackage(const std::string& path,
                                                    const std::function<void(float)>& set_progress);

  virtual ~Package() = default;

  virtual PackageType GetType() const = 0;

  virtual std::string GetPath() const = 0;

  // Opens the package as a zip file and returns the ZipArchiveHandle.
  virtual ZipArchiveHandle GetZipArchiveHandle() = 0;

  // Updates the progress in fraction during package verification.
  void SetProgress(float progress) override;

 protected:
  // An optional function to update the progress.
  std::function<void(float)> set_progress_;
};
