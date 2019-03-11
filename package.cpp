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

#include "package.h"

#include <string.h>

#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <openssl/sha.h>

#include "otautil/error_code.h"
#include "otautil/sysutil.h"

// This class wraps the package in memory, i.e. a memory mapped package, or a package loaded
// to a string/vector.
class MemoryPackage : public Package {
 public:
  // Constructs the class from a file. We will memory maps the file later.
  MemoryPackage(const std::string& path, std::unique_ptr<MemMapping> map,
                const std::function<void(float)>& set_progress);

  // Constructs the class from the package bytes in |content|.
  MemoryPackage(std::vector<uint8_t> content, const std::function<void(float)>& set_progress);

  ~MemoryPackage() override;

  // Memory maps the package file if necessary. Initializes the start address and size of the
  // package.
  uint64_t GetPackageSize() const override {
    return package_size_;
  }

  bool ReadFullyAtOffset(uint8_t* buffer, uint64_t byte_count, uint64_t offset) override;

  ZipArchiveHandle GetZipArchiveHandle() override;

  bool UpdateHashAtOffset(const std::vector<HasherUpdateCallback>& hashers, uint64_t start,
                          uint64_t length) override;

 private:
  const uint8_t* addr_;    // Start address of the package in memory.
  uint64_t package_size_;  // Package size in bytes.

  // The memory mapped package.
  std::unique_ptr<MemMapping> map_;
  // A copy of the package content, valid only if we create the class with the exact bytes of
  // the package.
  std::vector<uint8_t> package_content_;
  // The physical path to the package, empty if we create the class with the package content.
  std::string path_;

  // The ZipArchiveHandle of the package.
  ZipArchiveHandle zip_handle_;
};

// TODO(xunchang) Implement the PackageFromFd.

void Package::SetProgress(float progress) {
  if (set_progress_) {
    set_progress_(progress);
  }
}

std::unique_ptr<Package> Package::CreateMemoryPackage(
    const std::string& path, const std::function<void(float)>& set_progress) {
  std::unique_ptr<MemMapping> mmap = std::make_unique<MemMapping>();
  if (!mmap->MapFile(path)) {
    LOG(ERROR) << "failed to map file";
    return nullptr;
  }

  return std::make_unique<MemoryPackage>(path, std::move(mmap), set_progress);
}

std::unique_ptr<Package> Package::CreateMemoryPackage(
    std::vector<uint8_t> content, const std::function<void(float)>& set_progress) {
  return std::make_unique<MemoryPackage>(std::move(content), set_progress);
}

MemoryPackage::MemoryPackage(const std::string& path, std::unique_ptr<MemMapping> map,
                             const std::function<void(float)>& set_progress)
    : map_(std::move(map)), path_(path), zip_handle_(nullptr) {
  addr_ = map_->addr;
  package_size_ = map_->length;
  set_progress_ = set_progress;
}

MemoryPackage::MemoryPackage(std::vector<uint8_t> content,
                             const std::function<void(float)>& set_progress)
    : package_content_(std::move(content)), zip_handle_(nullptr) {
  CHECK(!package_content_.empty());
  addr_ = package_content_.data();
  package_size_ = package_content_.size();
  set_progress_ = set_progress;
}

MemoryPackage::~MemoryPackage() {
  if (zip_handle_) {
    CloseArchive(zip_handle_);
  }
}

bool MemoryPackage::ReadFullyAtOffset(uint8_t* buffer, uint64_t byte_count, uint64_t offset) {
  if (byte_count > package_size_ || offset > package_size_ - byte_count) {
    LOG(ERROR) << "Out of bound read, offset: " << offset << ", size: " << byte_count
               << ", total package_size: " << package_size_;
    return false;
  }
  memcpy(buffer, addr_ + offset, byte_count);
  return true;
}

bool MemoryPackage::UpdateHashAtOffset(const std::vector<HasherUpdateCallback>& hashers,
                                       uint64_t start, uint64_t length) {
  if (length > package_size_ || start > package_size_ - length) {
    LOG(ERROR) << "Out of bound read, offset: " << start << ", size: " << length
               << ", total package_size: " << package_size_;
    return false;
  }

  for (const auto& hasher : hashers) {
    hasher(addr_ + start, length);
  }
  return true;
}

ZipArchiveHandle MemoryPackage::GetZipArchiveHandle() {
  if (zip_handle_) {
    return zip_handle_;
  }

  if (auto err = OpenArchiveFromMemory(const_cast<uint8_t*>(addr_), package_size_, path_.c_str(),
                                       &zip_handle_);
      err != 0) {
    LOG(ERROR) << "Can't open package" << path_ << " : " << ErrorCodeString(err);
    return nullptr;
  }

  return zip_handle_;
}
