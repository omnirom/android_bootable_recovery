/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include "applypatch/applypatch.h"

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <openssl/sha.h>

#include "bmlutils/bmlutils.h"
#include "mtdutils/mtdutils.h"

#include "edify/expr.h"
#include "otautil/paths.h"
#include "otautil/print_sha1.h"

using namespace std::string_literals;

<<<<<<< HEAD
static bool mtd_partitions_scanned = false;

// Read a file into memory; store the file contents and associated metadata in *file.
// Return 0 on success.
int LoadFileContents(const char* filename, FileContents* file) {
  // A special 'filename' beginning with "MTD:" or "EMMC:" means to
  // load the contents of a partition.
  if (strncmp(filename, "MTD:", 4) == 0 ||
    strncmp(filename, "EMMC:", 5) == 0 ||
    strncmp(filename, "BML:", 4) == 0) {
    return LoadPartitionContents(filename, file);
  }
=======
static bool GenerateTarget(const Partition& target, const FileContents& source_file,
                           const Value& patch, const Value* bonus_data);
>>>>>>> android-10.0.0_r25

bool LoadFileContents(const std::string& filename, FileContents* file) {
  // No longer allow loading contents from eMMC partitions.
  if (android::base::StartsWith(filename, "EMMC:")) {
    return false;
  }

  std::string data;
  if (!android::base::ReadFileToString(filename, &data)) {
    PLOG(ERROR) << "Failed to read \"" << filename << "\"";
    return false;
  }

  file->data = std::vector<unsigned char>(data.begin(), data.end());
  SHA1(file->data.data(), file->data.size(), file->sha1);
  return true;
}

<<<<<<< HEAD
// Load the contents of an EMMC partition into the provided
// FileContents.  filename should be a string of the form
// "EMMC:<partition_device>:...".  The smallest size_n bytes for
// which that prefix of the partition contents has the corresponding
// sha1 hash will be loaded.  It is acceptable for a size value to be
// repeated with different sha1s.  Will return 0 on success.
//
// This complexity is needed because if an OTA installation is
// interrupted, the partition might contain either the source or the
// target data, which might be of different lengths.  We need to know
// the length in order to read from a partition (there is no
// "end-of-file" marker), so the caller must specify the possible
// lengths and the hash of the data, and we'll do the load expecting
// to find one of those hashes.
enum PartitionType { MTD, EMMC };

static int LoadPartitionContents(const std::string& filename, FileContents* file) {
  std::vector<std::string> pieces = android::base::Split(filename, ":");
  if (pieces.size() < 4 || pieces.size() % 2 != 0) {
    printf("LoadPartitionContents called with bad filename \"%s\"\n", filename.c_str());
    return -1;
  }

  enum PartitionType type;
  if (pieces[0] == "MTD") {
    type = MTD;
  } else if (pieces[0] == "EMMC") {
    type = EMMC;
  } else if (pieces[0] == "BML") {
    type = EMMC;
  } else {
    printf("LoadPartitionContents called with bad filename (%s)\n", filename.c_str());
    return -1;
  }

  size_t pair_count = (pieces.size() - 2) / 2;  // # of (size, sha1) pairs in filename
  std::vector<std::pair<size_t, std::string>> pairs;
  for (size_t i = 0; i < pair_count; ++i) {
    size_t size;
    if (!android::base::ParseUint(pieces[i * 2 + 2], &size) || size == 0) {
      printf("LoadPartitionContents called with bad size \"%s\"\n", pieces[i * 2 + 2].c_str());
      return -1;
    }
    pairs.push_back({ size, pieces[i * 2 + 3] });
  }

  // Sort the pairs array so that they are in order of increasing size.
  std::sort(pairs.begin(), pairs.end());

  const char* partition = pieces[1].c_str();
  unique_file dev(ota_fopen(partition, "rb"));
  if (!dev) {
    printf("failed to open emmc partition \"%s\": %s\n", partition, strerror(errno));
    return -1;
=======
// Reads the contents of a Partition to the given FileContents buffer.
static bool ReadPartitionToBuffer(const Partition& partition, FileContents* out,
                                  bool check_backup) {
  uint8_t expected_sha1[SHA_DIGEST_LENGTH];
  if (ParseSha1(partition.hash, expected_sha1) != 0) {
    LOG(ERROR) << "Failed to parse target hash \"" << partition.hash << "\"";
    return false;
>>>>>>> android-10.0.0_r25
  }

  android::base::unique_fd dev(open(partition.name.c_str(), O_RDONLY));
  if (dev == -1) {
    PLOG(ERROR) << "Failed to open eMMC partition \"" << partition << "\"";
  } else {
    std::vector<unsigned char> buffer(partition.size);
    if (!android::base::ReadFully(dev, buffer.data(), buffer.size())) {
      PLOG(ERROR) << "Failed to read " << buffer.size() << " bytes of data for partition "
                  << partition;
    } else {
      SHA1(buffer.data(), buffer.size(), out->sha1);
      if (memcmp(out->sha1, expected_sha1, SHA_DIGEST_LENGTH) == 0) {
        out->data = std::move(buffer);
        return true;
      }
<<<<<<< HEAD
      SHA1_Update(&sha_ctx, buffer_ptr, read);
      buffer_size += read;
      buffer_ptr += read;
    }

    if (pieces[0] == "BML") {
      if (strcmp(partition, "boot") == 0) {
        partition = BOARD_BML_BOOT;
      } else if (strcmp(partition, "recovery") == 0) {
        partition = BOARD_BML_RECOVERY;
      }
    }

    // Duplicate the SHA context and finalize the duplicate so we can
    // check it against this pair's expected hash.
    SHA_CTX temp_ctx;
    memcpy(&temp_ctx, &sha_ctx, sizeof(SHA_CTX));
    uint8_t sha_so_far[SHA_DIGEST_LENGTH];
    SHA1_Final(sha_so_far, &temp_ctx);

    uint8_t parsed_sha[SHA_DIGEST_LENGTH];
    if (ParseSha1(current_sha1.c_str(), parsed_sha) != 0) {
      printf("failed to parse SHA-1 %s in %s\n", current_sha1.c_str(), filename.c_str());
      return -1;
    }

    if (memcmp(sha_so_far, parsed_sha, SHA_DIGEST_LENGTH) == 0) {
      // We have a match. Stop reading the partition; we'll return the data we've read so far.
      printf("partition read matched size %zu SHA-1 %s\n", current_size, current_sha1.c_str());
      found = true;
      break;
=======
>>>>>>> android-10.0.0_r25
    }
  }

  if (!check_backup) {
    LOG(ERROR) << "Partition contents don't have the expected checksum";
    return false;
  }

  if (LoadFileContents(Paths::Get().cache_temp_source(), out) &&
      memcmp(out->sha1, expected_sha1, SHA_DIGEST_LENGTH) == 0) {
    return true;
  }

  LOG(ERROR) << "Both of partition contents and backup don't have the expected checksum";
  return false;
}

bool SaveFileContents(const std::string& filename, const FileContents* file) {
  android::base::unique_fd fd(
      open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, S_IRUSR | S_IWUSR));
  if (fd == -1) {
    PLOG(ERROR) << "Failed to open \"" << filename << "\" for write";
    return false;
  }

  if (!android::base::WriteFully(fd, file->data.data(), file->data.size())) {
    PLOG(ERROR) << "Failed to write " << file->data.size() << " bytes of data to " << filename;
    return false;
  }

<<<<<<< HEAD
  return 0;
}

// Write a memory buffer to 'target' partition, a string of the form
// "EMMC:<partition_device>[:...]". The target name
// might contain multiple colons, but WriteToPartition() only uses the first
// two and ignores the rest. Return 0 on success.
int WriteToPartition(const unsigned char* data, size_t len, const std::string& target) {
    std::string copy(target);
    std::vector<std::string> pieces = android::base::Split(copy, ":");

    if (pieces.size() < 2) {
        printf("WriteToPartition called with bad target (%s)\n", target.c_str());
        return -1;
    }

    enum PartitionType type;
    if (pieces[0] == "MTD") {
        type = MTD;
    } else if (pieces[0] == "EMMC") {
        type = EMMC;
    } else if (pieces[0] == "BML") {
        type = EMMC;
    } else {
        printf("WriteToPartition called with bad target (%s)\n", target.c_str());
        return -1;
=======
  if (fsync(fd) != 0) {
    PLOG(ERROR) << "Failed to fsync \"" << filename << "\"";
    return false;
  }

  if (close(fd.release()) != 0) {
    PLOG(ERROR) << "Failed to close \"" << filename << "\"";
    return false;
  }

  return true;
}

// Writes a memory buffer to 'target' Partition.
static bool WriteBufferToPartition(const FileContents& file_contents, const Partition& partition) {
  const unsigned char* data = file_contents.data.data();
  size_t len = file_contents.data.size();
  size_t start = 0;
  bool success = false;
  for (size_t attempt = 0; attempt < 2; ++attempt) {
    android::base::unique_fd fd(open(partition.name.c_str(), O_RDWR));
    if (fd == -1) {
      PLOG(ERROR) << "Failed to open \"" << partition << "\"";
      return false;
    }

    if (TEMP_FAILURE_RETRY(lseek(fd, start, SEEK_SET)) == -1) {
      PLOG(ERROR) << "Failed to seek to " << start << " on \"" << partition << "\"";
      return false;
    }

    if (!android::base::WriteFully(fd, data + start, len - start)) {
      PLOG(ERROR) << "Failed to write " << len - start << " bytes to \"" << partition << "\"";
      return false;
    }

    if (fsync(fd) != 0) {
      PLOG(ERROR) << "Failed to sync \"" << partition << "\"";
      return false;
    }
    if (close(fd.release()) != 0) {
      PLOG(ERROR) << "Failed to close \"" << partition << "\"";
      return false;
    }

    fd.reset(open(partition.name.c_str(), O_RDONLY));
    if (fd == -1) {
      PLOG(ERROR) << "Failed to reopen \"" << partition << "\" for verification";
      return false;
    }

    // Drop caches so our subsequent verification read won't just be reading the cache.
    sync();
    std::string drop_cache = "/proc/sys/vm/drop_caches";
    if (!android::base::WriteStringToFile("3\n", drop_cache)) {
      PLOG(ERROR) << "Failed to write to " << drop_cache;
    } else {
      LOG(INFO) << "  caches dropped";
    }
    sleep(1);

    // Verify.
    if (TEMP_FAILURE_RETRY(lseek(fd, 0, SEEK_SET)) == -1) {
      PLOG(ERROR) << "Failed to seek to 0 on " << partition;
      return false;
>>>>>>> android-10.0.0_r25
    }

    const char* partition = pieces[1].c_str();

<<<<<<< HEAD
    if (pieces[0] == "BML") {
        if (strcmp(partition, "boot") == 0) {
            partition = BOARD_BML_BOOT;
        } else if (strcmp(partition, "recovery") == 0) {
            partition = BOARD_BML_RECOVERY;
        }

        int bmlpartition = open(partition, O_RDWR | O_LARGEFILE);
        if (bmlpartition < 0)
            return -1;
        if (ioctl(bmlpartition, BML_UNLOCK_ALL, 0)) {
            printf("failed to unlock BML partition: (%s)\n", partition);
            return -1;
        }
        close(bmlpartition);
    }

    if (partition == NULL) {
        printf("bad partition target name \"%s\"\n", target.c_str());
        return -1;
    }

    switch (type) {
        case MTD: {
            if (!mtd_partitions_scanned) {
                mtd_scan_partitions();
                mtd_partitions_scanned = true;
            }

            const MtdPartition* mtd = mtd_find_partition_by_name(partition);
            if (mtd == NULL) {
                printf("mtd partition \"%s\" not found for writing\n", partition);
                return -1;
            }

            MtdWriteContext* ctx = mtd_write_partition(mtd);
            if (ctx == NULL) {
                printf("failed to init mtd partition \"%s\" for writing\n", partition);
                return -1;
            }

            size_t written = mtd_write_data(ctx, reinterpret_cast<const char*>(data), len);
            if (written != len) {
                printf("only wrote %zu of %zu bytes to MTD %s\n", written, len, partition);
                mtd_write_close(ctx);
                return -1;
            }

            if (mtd_erase_blocks(ctx, -1) < 0) {
                printf("error finishing mtd write of %s\n", partition);
                mtd_write_close(ctx);
                return -1;
            }

            if (mtd_write_close(ctx)) {
                printf("error closing mtd write of %s\n", partition);
                return -1;
            }
            break;
        }

        case EMMC: {
            size_t start = 0;
            bool success = false;
            unique_fd fd(ota_open(partition, O_RDWR | O_SYNC));
            if (fd < 0) {
                printf("failed to open %s: %s\n", partition, strerror(errno));
                return -1;
            }

            for (size_t attempt = 0; attempt < 2; ++attempt) {
                if (TEMP_FAILURE_RETRY(lseek(fd, start, SEEK_SET)) == -1) {
                    printf("failed seek on %s: %s\n", partition, strerror(errno));
                    return -1;
                }
                while (start < len) {
                    size_t to_write = len - start;
                    if (to_write > 1<<20) to_write = 1<<20;

                    ssize_t written = TEMP_FAILURE_RETRY(ota_write(fd, data+start, to_write));
                    if (written == -1) {
                        printf("failed write writing to %s: %s\n", partition, strerror(errno));
                        return -1;
                    }
                    start += written;
                }
                if (ota_fsync(fd) != 0) {
                   printf("failed to sync to %s (%s)\n", partition, strerror(errno));
                   return -1;
                }
                if (ota_close(fd) != 0) {
                   printf("failed to close %s (%s)\n", partition, strerror(errno));
                   return -1;
                }
                unique_fd fd(ota_open(partition, O_RDONLY));
                if (fd < 0) {
                   printf("failed to reopen %s for verify (%s)\n", partition, strerror(errno));
                   return -1;
                }

                // Drop caches so our subsequent verification read
                // won't just be reading the cache.
                sync();
                unique_fd dc(ota_open("/proc/sys/vm/drop_caches", O_WRONLY));
                if (TEMP_FAILURE_RETRY(ota_write(dc, "3\n", 2)) == -1) {
                    printf("write to /proc/sys/vm/drop_caches failed: %s\n", strerror(errno));
                } else {
                    printf("  caches dropped\n");
                }
                ota_close(dc);
                sleep(1);

                // verify
                if (TEMP_FAILURE_RETRY(lseek(fd, 0, SEEK_SET)) == -1) {
                    printf("failed to seek back to beginning of %s: %s\n",
                           partition, strerror(errno));
                    return -1;
                }
                unsigned char buffer[4096];
                start = len;
                for (size_t p = 0; p < len; p += sizeof(buffer)) {
                    size_t to_read = len - p;
                    if (to_read > sizeof(buffer)) {
                        to_read = sizeof(buffer);
                    }

                    size_t so_far = 0;
                    while (so_far < to_read) {
                        ssize_t read_count =
                                TEMP_FAILURE_RETRY(ota_read(fd, buffer+so_far, to_read-so_far));
                        if (read_count == -1) {
                            printf("verify read error %s at %zu: %s\n",
                                   partition, p, strerror(errno));
                            return -1;
                        }
                        if (static_cast<size_t>(read_count) < to_read) {
                            printf("short verify read %s at %zu: %zd %zu %s\n",
                                   partition, p, read_count, to_read, strerror(errno));
                        }
                        so_far += read_count;
                    }

                    if (memcmp(buffer, data+p, to_read) != 0) {
                        printf("verification failed starting at %zu\n", p);
                        start = p;
                        break;
                    }
                }

                if (start == len) {
                    printf("verification read succeeded (attempt %zu)\n", attempt+1);
                    success = true;
                    break;
                }
            }

            if (!success) {
                printf("failed to verify after all attempts\n");
                return -1;
            }

            if (ota_close(fd) != 0) {
                printf("error closing %s (%s)\n", partition, strerror(errno));
                return -1;
            }
            sync();
            break;
        }
    }

    return 0;
}

// Take a string 'str' of 40 hex digits and parse it into the 20
// byte array 'digest'.  'str' may contain only the digest or be of
// the form "<digest>:<anything>".  Return 0 on success, -1 on any
// error.
int ParseSha1(const char* str, uint8_t* digest) {
    const char* ps = str;
    uint8_t* pd = digest;
    for (int i = 0; i < SHA_DIGEST_LENGTH * 2; ++i, ++ps) {
        int digit;
        if (*ps >= '0' && *ps <= '9') {
            digit = *ps - '0';
        } else if (*ps >= 'a' && *ps <= 'f') {
            digit = *ps - 'a' + 10;
        } else if (*ps >= 'A' && *ps <= 'F') {
            digit = *ps - 'A' + 10;
        } else {
            return -1;
        }
        if (i % 2 == 0) {
            *pd = digit << 4;
        } else {
            *pd |= digit;
            ++pd;
        }
    }
    if (*ps != '\0') return -1;
    return 0;
}

// Search an array of sha1 strings for one matching the given sha1.
// Return the index of the match on success, or -1 if no match is
// found.
static int FindMatchingPatch(uint8_t* sha1, const std::vector<std::string>& patch_sha1_str) {
  for (size_t i = 0; i < patch_sha1_str.size(); ++i) {
    uint8_t patch_sha1[SHA_DIGEST_LENGTH];
    if (ParseSha1(patch_sha1_str[i].c_str(), patch_sha1) == 0 &&
        memcmp(patch_sha1, sha1, SHA_DIGEST_LENGTH) == 0) {
      return i;
    }
  }
  return -1;
=======
      if (!android::base::ReadFully(fd, buffer, to_read)) {
        PLOG(ERROR) << "Failed to verify-read " << partition << " at " << p;
        return false;
      }

      if (memcmp(buffer, data + p, to_read) != 0) {
        LOG(ERROR) << "Verification failed starting at " << p;
        start = p;
        break;
      }
    }

    if (start == len) {
      LOG(INFO) << "Verification read succeeded (attempt " << attempt + 1 << ")";
      success = true;
      break;
    }

    if (close(fd.release()) != 0) {
      PLOG(ERROR) << "Failed to close " << partition;
      return false;
    }
  }

  if (!success) {
    LOG(ERROR) << "Failed to verify after all attempts";
    return false;
  }

  sync();

  return true;
>>>>>>> android-10.0.0_r25
}

int ParseSha1(const std::string& str, uint8_t* digest) {
  const char* ps = str.c_str();
  uint8_t* pd = digest;
  for (int i = 0; i < SHA_DIGEST_LENGTH * 2; ++i, ++ps) {
    int digit;
    if (*ps >= '0' && *ps <= '9') {
      digit = *ps - '0';
    } else if (*ps >= 'a' && *ps <= 'f') {
      digit = *ps - 'a' + 10;
    } else if (*ps >= 'A' && *ps <= 'F') {
      digit = *ps - 'A' + 10;
    } else {
      return -1;
    }
    if (i % 2 == 0) {
      *pd = digit << 4;
    } else {
      *pd |= digit;
      ++pd;
    }
  }
  if (*ps != '\0') return -1;
  return 0;
}

bool PatchPartitionCheck(const Partition& target, const Partition& source) {
  FileContents target_file;
  FileContents source_file;
  return (ReadPartitionToBuffer(target, &target_file, false) ||
          ReadPartitionToBuffer(source, &source_file, true));
}

int ShowLicenses() {
  ShowBSDiffLicense();
  return 0;
}

bool PatchPartition(const Partition& target, const Partition& source, const Value& patch,
                    const Value* bonus) {
  LOG(INFO) << "Patching " << target.name;

  // We try to load and check against the target hash first.
  FileContents target_file;
  if (ReadPartitionToBuffer(target, &target_file, false)) {
    // The early-exit case: the patch was already applied, this file has the desired hash, nothing
    // for us to do.
    LOG(INFO) << "  already " << target.hash.substr(0, 8);
    return true;
  }

  FileContents source_file;
  if (ReadPartitionToBuffer(source, &source_file, true)) {
    return GenerateTarget(target, source_file, patch, bonus);
  }

  LOG(ERROR) << "Failed to find any match";
  return false;
}

bool FlashPartition(const Partition& partition, const std::string& source_filename) {
  LOG(INFO) << "Flashing " << partition;

  // We try to load and check against the target hash first.
  FileContents target_file;
  if (ReadPartitionToBuffer(partition, &target_file, false)) {
    // The early-exit case: the patch was already applied, this file has the desired hash, nothing
    // for us to do.
    LOG(INFO) << "  already " << partition.hash.substr(0, 8);
    return true;
  }

  FileContents source_file;
  if (!LoadFileContents(source_filename, &source_file)) {
    LOG(ERROR) << "Failed to load source file";
    return false;
  }

  uint8_t expected_sha1[SHA_DIGEST_LENGTH];
  if (ParseSha1(partition.hash, expected_sha1) != 0) {
    LOG(ERROR) << "Failed to parse source hash \"" << partition.hash << "\"";
    return false;
  }

  if (memcmp(source_file.sha1, expected_sha1, SHA_DIGEST_LENGTH) != 0) {
    // The source doesn't have desired checksum.
    LOG(ERROR) << "source \"" << source_filename << "\" doesn't have expected SHA-1 sum";
    LOG(ERROR) << "expected: " << partition.hash.substr(0, 8)
               << ", found: " << short_sha1(source_file.sha1);
    return false;
  }
  if (!WriteBufferToPartition(source_file, partition)) {
    LOG(ERROR) << "Failed to write to " << partition;
    return false;
  }
  return true;
}

static bool GenerateTarget(const Partition& target, const FileContents& source_file,
                           const Value& patch, const Value* bonus_data) {
  uint8_t expected_sha1[SHA_DIGEST_LENGTH];
  if (ParseSha1(target.hash, expected_sha1) != 0) {
    LOG(ERROR) << "Failed to parse target hash \"" << target.hash << "\"";
    return false;
  }

  if (patch.type != Value::Type::BLOB) {
    LOG(ERROR) << "patch is not a blob";
    return false;
  }

  const char* header = patch.data.data();
  size_t header_bytes_read = patch.data.size();
  bool use_bsdiff = false;
  if (header_bytes_read >= 8 && memcmp(header, "BSDIFF40", 8) == 0) {
    use_bsdiff = true;
  } else if (header_bytes_read >= 8 && memcmp(header, "IMGDIFF2", 8) == 0) {
    use_bsdiff = false;
  } else {
    LOG(ERROR) << "Unknown patch file format";
    return false;
  }

  // We write the original source to cache, in case the partition write is interrupted.
  if (!CheckAndFreeSpaceOnCache(source_file.data.size())) {
    LOG(ERROR) << "Not enough free space on /cache";
    return false;
  }
  if (!SaveFileContents(Paths::Get().cache_temp_source(), &source_file)) {
    LOG(ERROR) << "Failed to back up source file";
    return false;
  }

  // We store the decoded output in memory.
  FileContents patched;
  SHA_CTX ctx;
  SHA1_Init(&ctx);
  SinkFn sink = [&patched, &ctx](const unsigned char* data, size_t len) {
    SHA1_Update(&ctx, data, len);
    patched.data.insert(patched.data.end(), data, data + len);
    return len;
  };

  int result;
  if (use_bsdiff) {
    result = ApplyBSDiffPatch(source_file.data.data(), source_file.data.size(), patch, 0, sink);
  } else {
    result =
        ApplyImagePatch(source_file.data.data(), source_file.data.size(), patch, sink, bonus_data);
  }

  if (result != 0) {
    LOG(ERROR) << "Failed to apply the patch: " << result;
    return false;
  }

  SHA1_Final(patched.sha1, &ctx);
  if (memcmp(patched.sha1, expected_sha1, SHA_DIGEST_LENGTH) != 0) {
    LOG(ERROR) << "Patching did not produce the expected SHA-1 of " << short_sha1(expected_sha1);

    LOG(ERROR) << "target size " << patched.data.size() << " SHA-1 " << short_sha1(patched.sha1);
    LOG(ERROR) << "source size " << source_file.data.size() << " SHA-1 "
               << short_sha1(source_file.sha1);

    uint8_t patch_digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const uint8_t*>(patch.data.data()), patch.data.size(), patch_digest);
    LOG(ERROR) << "patch size " << patch.data.size() << " SHA-1 " << short_sha1(patch_digest);

    if (bonus_data != nullptr) {
      uint8_t bonus_digest[SHA_DIGEST_LENGTH];
      SHA1(reinterpret_cast<const uint8_t*>(bonus_data->data.data()), bonus_data->data.size(),
           bonus_digest);
      LOG(ERROR) << "bonus size " << bonus_data->data.size() << " SHA-1 "
                 << short_sha1(bonus_digest);
    }

    return false;
  }

  LOG(INFO) << "  now " << short_sha1(expected_sha1);

  // Write back the temp file to the partition.
  if (!WriteBufferToPartition(patched, target)) {
    LOG(ERROR) << "Failed to write patched data to " << target.name;
    return false;
  }

  // Delete the backup copy of the source.
  unlink(Paths::Get().cache_temp_source().c_str());

  // Success!
  return true;
}

bool CheckPartition(const Partition& partition) {
  FileContents target_file;
  return ReadPartitionToBuffer(partition, &target_file, false);
}

Partition Partition::Parse(const std::string& input_str, std::string* err) {
  std::vector<std::string> pieces = android::base::Split(input_str, ":");
  if (pieces.size() != 4 || pieces[0] != "EMMC") {
    *err = "Invalid number of tokens or non-eMMC target";
    return {};
  }

  size_t size;
  if (!android::base::ParseUint(pieces[2], &size) || size == 0) {
    *err = "Failed to parse \"" + pieces[2] + "\" as byte count";
    return {};
  }

  return Partition(pieces[1], size, pieces[3]);
}

std::string Partition::ToString() const {
  if (*this) {
    return "EMMC:"s + name + ":" + std::to_string(size) + ":" + hash;
  }
  return "<invalid-partition>";
}

std::ostream& operator<<(std::ostream& os, const Partition& partition) {
  os << partition.ToString();
  return os;
}
