/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/fs.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <fec/io.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <applypatch/applypatch.h>
#include <brotli/decode.h>
#include <openssl/sha.h>
#include <private/android_filesystem_config.h>
#include <ziparchive/zip_archive.h>

#include "edify/expr.h"
#include "error_code.h"
#include "ota_io.h"
#include "print_sha1.h"
#include "updater/install.h"
#include "updater/rangeset.h"
#include "updater/updater.h"

// Set this to 0 to interpret 'erase' transfers to mean do a
// BLKDISCARD ioctl (the normal behavior).  Set to 1 to interpret
// erase to mean fill the region with zeroes.
#define DEBUG_ERASE  0

static constexpr size_t BLOCKSIZE = 4096;
static constexpr const char* STASH_DIRECTORY_BASE = "/cache/recovery";
static constexpr mode_t STASH_DIRECTORY_MODE = 0700;
static constexpr mode_t STASH_FILE_MODE = 0600;

static CauseCode failure_type = kNoCause;
static bool is_retry = false;
static std::unordered_map<std::string, RangeSet> stash_map;

static int read_all(int fd, uint8_t* data, size_t size) {
    size_t so_far = 0;
    while (so_far < size) {
        ssize_t r = TEMP_FAILURE_RETRY(ota_read(fd, data+so_far, size-so_far));
        if (r == -1) {
            failure_type = kFreadFailure;
            PLOG(ERROR) << "read failed";
            return -1;
        } else if (r == 0) {
            failure_type = kFreadFailure;
            LOG(ERROR) << "read reached unexpected EOF.";
            return -1;
        }
        so_far += r;
    }
    return 0;
}

static int read_all(int fd, std::vector<uint8_t>& buffer, size_t size) {
    return read_all(fd, buffer.data(), size);
}

static int write_all(int fd, const uint8_t* data, size_t size) {
    size_t written = 0;
    while (written < size) {
        ssize_t w = TEMP_FAILURE_RETRY(ota_write(fd, data+written, size-written));
        if (w == -1) {
            failure_type = kFwriteFailure;
            PLOG(ERROR) << "write failed";
            return -1;
        }
        written += w;
    }

    return 0;
}

static int write_all(int fd, const std::vector<uint8_t>& buffer, size_t size) {
    return write_all(fd, buffer.data(), size);
}

static bool discard_blocks(int fd, off64_t offset, uint64_t size) {
  // Don't discard blocks unless the update is a retry run.
  if (!is_retry) {
    return true;
  }

  uint64_t args[2] = { static_cast<uint64_t>(offset), size };
  if (ioctl(fd, BLKDISCARD, &args) == -1) {
    PLOG(ERROR) << "BLKDISCARD ioctl failed";
    return false;
  }
  return true;
}

static bool check_lseek(int fd, off64_t offset, int whence) {
    off64_t rc = TEMP_FAILURE_RETRY(lseek64(fd, offset, whence));
    if (rc == -1) {
        failure_type = kLseekFailure;
        PLOG(ERROR) << "lseek64 failed";
        return false;
    }
    return true;
}

static void allocate(size_t size, std::vector<uint8_t>& buffer) {
    // if the buffer's big enough, reuse it.
    if (size <= buffer.size()) return;

    buffer.resize(size);
}

/**
 * RangeSinkWriter reads data from the given FD, and writes them to the destination specified by the
 * given RangeSet.
 */
class RangeSinkWriter {
 public:
  RangeSinkWriter(int fd, const RangeSet& tgt)
      : fd_(fd),
        tgt_(tgt),
        next_range_(0),
        current_range_left_(0),
        bytes_written_(0) {
    CHECK_NE(tgt.size(), static_cast<size_t>(0));
  };

  bool Finished() const {
    return next_range_ == tgt_.size() && current_range_left_ == 0;
  }

  size_t AvailableSpace() const {
    return tgt_.blocks() * BLOCKSIZE - bytes_written_;
  }

  // Return number of bytes written; and 0 indicates a writing failure.
  size_t Write(const uint8_t* data, size_t size) {
    if (Finished()) {
      LOG(ERROR) << "range sink write overrun; can't write " << size << " bytes";
      return 0;
    }

    size_t written = 0;
    while (size > 0) {
      // Move to the next range as needed.
      if (!SeekToOutputRange()) {
        break;
      }

      size_t write_now = size;
      if (current_range_left_ < write_now) {
        write_now = current_range_left_;
      }

      if (write_all(fd_, data, write_now) == -1) {
        break;
      }

      data += write_now;
      size -= write_now;

      current_range_left_ -= write_now;
      written += write_now;
    }

    bytes_written_ += written;
    return written;
  }

  size_t BytesWritten() const {
    return bytes_written_;
  }

 private:
  // Set up the output cursor, move to next range if needed.
  bool SeekToOutputRange() {
    // We haven't finished the current range yet.
    if (current_range_left_ != 0) {
      return true;
    }
    // We can't write any more; let the write function return how many bytes have been written
    // so far.
    if (next_range_ >= tgt_.size()) {
      return false;
    }

    const Range& range = tgt_[next_range_];
    off64_t offset = static_cast<off64_t>(range.first) * BLOCKSIZE;
    current_range_left_ = (range.second - range.first) * BLOCKSIZE;
    next_range_++;

    if (!discard_blocks(fd_, offset, current_range_left_)) {
      return false;
    }
    if (!check_lseek(fd_, offset, SEEK_SET)) {
      return false;
    }
    return true;
  }

  // The output file descriptor.
  int fd_;
  // The destination ranges for the data.
  const RangeSet& tgt_;
  // The next range that we should write to.
  size_t next_range_;
  // The number of bytes to write before moving to the next range.
  size_t current_range_left_;
  // Total bytes written by the writer.
  size_t bytes_written_;
};

/**
 * All of the data for all the 'new' transfers is contained in one file in the update package,
 * concatenated together in the order in which transfers.list will need it. We want to stream it out
 * of the archive (it's compressed) without writing it to a temp file, but we can't write each
 * section until it's that transfer's turn to go.
 *
 * To achieve this, we expand the new data from the archive in a background thread, and block that
 * threads 'receive uncompressed data' function until the main thread has reached a point where we
 * want some new data to be written. We signal the background thread with the destination for the
 * data and block the main thread, waiting for the background thread to complete writing that
 * section. Then it signals the main thread to wake up and goes back to blocking waiting for a
 * transfer.
 *
 * NewThreadInfo is the struct used to pass information back and forth between the two threads. When
 * the main thread wants some data written, it sets writer to the destination location and signals
 * the condition. When the background thread is done writing, it clears writer and signals the
 * condition again.
 */
struct NewThreadInfo {
  ZipArchiveHandle za;
  ZipEntry entry;
  bool brotli_compressed;

  std::unique_ptr<RangeSinkWriter> writer;
  BrotliDecoderState* brotli_decoder_state;
  bool receiver_available;

  pthread_mutex_t mu;
  pthread_cond_t cv;
};

static bool receive_new_data(const uint8_t* data, size_t size, void* cookie) {
  NewThreadInfo* nti = static_cast<NewThreadInfo*>(cookie);

  while (size > 0) {
    // Wait for nti->writer to be non-null, indicating some of this data is wanted.
    pthread_mutex_lock(&nti->mu);
    while (nti->writer == nullptr) {
      pthread_cond_wait(&nti->cv, &nti->mu);
    }
    pthread_mutex_unlock(&nti->mu);

    // At this point nti->writer is set, and we own it. The main thread is waiting for it to
    // disappear from nti.
    size_t write_now = std::min(size, nti->writer->AvailableSpace());
    if (nti->writer->Write(data, write_now) != write_now) {
      LOG(ERROR) << "Failed to write " << write_now << " bytes.";
      return false;
    }

    data += write_now;
    size -= write_now;

    if (nti->writer->Finished()) {
      // We have written all the bytes desired by this writer.

      pthread_mutex_lock(&nti->mu);
      nti->writer = nullptr;
      pthread_cond_broadcast(&nti->cv);
      pthread_mutex_unlock(&nti->mu);
    }
  }

  return true;
}

static bool receive_brotli_new_data(const uint8_t* data, size_t size, void* cookie) {
  NewThreadInfo* nti = static_cast<NewThreadInfo*>(cookie);

  while (size > 0 || BrotliDecoderHasMoreOutput(nti->brotli_decoder_state)) {
    // Wait for nti->writer to be non-null, indicating some of this data is wanted.
    pthread_mutex_lock(&nti->mu);
    while (nti->writer == nullptr) {
      pthread_cond_wait(&nti->cv, &nti->mu);
    }
    pthread_mutex_unlock(&nti->mu);

    // At this point nti->writer is set, and we own it. The main thread is waiting for it to
    // disappear from nti.

    size_t buffer_size = std::min<size_t>(32768, nti->writer->AvailableSpace());
    if (buffer_size == 0) {
      LOG(ERROR) << "No space left in output range";
      return false;
    }
    uint8_t buffer[buffer_size];
    size_t available_in = size;
    size_t available_out = buffer_size;
    uint8_t* next_out = buffer;

    // The brotli decoder will update |data|, |available_in|, |next_out| and |available_out|.
    BrotliDecoderResult result = BrotliDecoderDecompressStream(
        nti->brotli_decoder_state, &available_in, &data, &available_out, &next_out, nullptr);

    if (result == BROTLI_DECODER_RESULT_ERROR) {
      LOG(ERROR) << "Decompression failed with "
                 << BrotliDecoderErrorString(BrotliDecoderGetErrorCode(nti->brotli_decoder_state));
      return false;
    }

    LOG(DEBUG) << "bytes to write: " << buffer_size - available_out << ", bytes consumed "
               << size - available_in << ", decoder status " << result;

    size_t write_now = buffer_size - available_out;
    if (nti->writer->Write(buffer, write_now) != write_now) {
      LOG(ERROR) << "Failed to write " << write_now << " bytes.";
      return false;
    }

    // Update the remaining size. The input data ptr is already updated by brotli decoder function.
    size = available_in;

    if (nti->writer->Finished()) {
      // We have written all the bytes desired by this writer.

      pthread_mutex_lock(&nti->mu);
      nti->writer = nullptr;
      pthread_cond_broadcast(&nti->cv);
      pthread_mutex_unlock(&nti->mu);
    }
  }

  return true;
}

static void* unzip_new_data(void* cookie) {
  NewThreadInfo* nti = static_cast<NewThreadInfo*>(cookie);
  if (nti->brotli_compressed) {
    ProcessZipEntryContents(nti->za, &nti->entry, receive_brotli_new_data, nti);
  } else {
    ProcessZipEntryContents(nti->za, &nti->entry, receive_new_data, nti);
  }
  pthread_mutex_lock(&nti->mu);
  nti->receiver_available = false;
  if (nti->writer != nullptr) {
    pthread_cond_broadcast(&nti->cv);
  }
  pthread_mutex_unlock(&nti->mu);
  return nullptr;
}

static int ReadBlocks(const RangeSet& src, std::vector<uint8_t>& buffer, int fd) {
  size_t p = 0;
  for (const auto& range : src) {
    if (!check_lseek(fd, static_cast<off64_t>(range.first) * BLOCKSIZE, SEEK_SET)) {
      return -1;
    }

    size_t size = (range.second - range.first) * BLOCKSIZE;
    if (read_all(fd, buffer.data() + p, size) == -1) {
      return -1;
    }

    p += size;
  }

  return 0;
}

static int WriteBlocks(const RangeSet& tgt, const std::vector<uint8_t>& buffer, int fd) {
  size_t written = 0;
  for (const auto& range : tgt) {
    off64_t offset = static_cast<off64_t>(range.first) * BLOCKSIZE;
    size_t size = (range.second - range.first) * BLOCKSIZE;
    if (!discard_blocks(fd, offset, size)) {
      return -1;
    }

    if (!check_lseek(fd, offset, SEEK_SET)) {
      return -1;
    }

    if (write_all(fd, buffer.data() + written, size) == -1) {
      return -1;
    }

    written += size;
  }

  return 0;
}

// Parameters for transfer list command functions
struct CommandParameters {
    std::vector<std::string> tokens;
    size_t cpos;
    const char* cmdname;
    const char* cmdline;
    std::string freestash;
    std::string stashbase;
    bool canwrite;
    int createdstash;
    android::base::unique_fd fd;
    bool foundwrites;
    bool isunresumable;
    int version;
    size_t written;
    size_t stashed;
    NewThreadInfo nti;
    pthread_t thread;
    std::vector<uint8_t> buffer;
    uint8_t* patch_start;
};

// Print the hash in hex for corrupted source blocks (excluding the stashed blocks which is
// handled separately).
static void PrintHashForCorruptedSourceBlocks(const CommandParameters& params,
                                              const std::vector<uint8_t>& buffer) {
  LOG(INFO) << "unexpected contents of source blocks in cmd:\n" << params.cmdline;
  CHECK(params.tokens[0] == "move" || params.tokens[0] == "bsdiff" ||
        params.tokens[0] == "imgdiff");

  size_t pos = 0;
  // Command example:
  // move <onehash> <tgt_range> <src_blk_count> <src_range> [<loc_range> <stashed_blocks>]
  // bsdiff <offset> <len> <src_hash> <tgt_hash> <tgt_range> <src_blk_count> <src_range>
  //        [<loc_range> <stashed_blocks>]
  if (params.tokens[0] == "move") {
    // src_range for move starts at the 4th position.
    if (params.tokens.size() < 5) {
      LOG(ERROR) << "failed to parse source range in cmd:\n" << params.cmdline;
      return;
    }
    pos = 4;
  } else {
    // src_range for diff starts at the 7th position.
    if (params.tokens.size() < 8) {
      LOG(ERROR) << "failed to parse source range in cmd:\n" << params.cmdline;
      return;
    }
    pos = 7;
  }

  // Source blocks in stash only, no work to do.
  if (params.tokens[pos] == "-") {
    return;
  }

  RangeSet src = RangeSet::Parse(params.tokens[pos++]);

  RangeSet locs;
  // If there's no stashed blocks, content in the buffer is consecutive and has the same
  // order as the source blocks.
  if (pos == params.tokens.size()) {
    locs = RangeSet(std::vector<Range>{ Range{ 0, src.blocks() } });
  } else {
    // Otherwise, the next token is the offset of the source blocks in the target range.
    // Example: for the tokens <4,63946,63947,63948,63979> <4,6,7,8,39> <stashed_blocks>;
    // We want to print SHA-1 for the data in buffer[6], buffer[8], buffer[9] ... buffer[38];
    // this corresponds to the 32 src blocks #63946, #63948, #63949 ... #63978.
    locs = RangeSet::Parse(params.tokens[pos++]);
    CHECK_EQ(src.blocks(), locs.blocks());
  }

  LOG(INFO) << "printing hash in hex for " << src.blocks() << " source blocks";
  for (size_t i = 0; i < src.blocks(); i++) {
    size_t block_num = src.GetBlockNumber(i);
    size_t buffer_index = locs.GetBlockNumber(i);
    CHECK_LE((buffer_index + 1) * BLOCKSIZE, buffer.size());

    uint8_t digest[SHA_DIGEST_LENGTH];
    SHA1(buffer.data() + buffer_index * BLOCKSIZE, BLOCKSIZE, digest);
    std::string hexdigest = print_sha1(digest);
    LOG(INFO) << "  block number: " << block_num << ", SHA-1: " << hexdigest;
  }
}

// If the calculated hash for the whole stash doesn't match the stash id, print the SHA-1
// in hex for each block.
static void PrintHashForCorruptedStashedBlocks(const std::string& id,
                                               const std::vector<uint8_t>& buffer,
                                               const RangeSet& src) {
  LOG(INFO) << "printing hash in hex for stash_id: " << id;
  CHECK_EQ(src.blocks() * BLOCKSIZE, buffer.size());

  for (size_t i = 0; i < src.blocks(); i++) {
    size_t block_num = src.GetBlockNumber(i);

    uint8_t digest[SHA_DIGEST_LENGTH];
    SHA1(buffer.data() + i * BLOCKSIZE, BLOCKSIZE, digest);
    std::string hexdigest = print_sha1(digest);
    LOG(INFO) << "  block number: " << block_num << ", SHA-1: " << hexdigest;
  }
}

// If the stash file doesn't exist, read the source blocks this stash contains and print the
// SHA-1 for these blocks.
static void PrintHashForMissingStashedBlocks(const std::string& id, int fd) {
  if (stash_map.find(id) == stash_map.end()) {
    LOG(ERROR) << "No stash saved for id: " << id;
    return;
  }

  LOG(INFO) << "print hash in hex for source blocks in missing stash: " << id;
  const RangeSet& src = stash_map[id];
  std::vector<uint8_t> buffer(src.blocks() * BLOCKSIZE);
  if (ReadBlocks(src, buffer, fd) == -1) {
      LOG(ERROR) << "failed to read source blocks for stash: " << id;
      return;
  }
  PrintHashForCorruptedStashedBlocks(id, buffer, src);
}

static int VerifyBlocks(const std::string& expected, const std::vector<uint8_t>& buffer,
        const size_t blocks, bool printerror) {
    uint8_t digest[SHA_DIGEST_LENGTH];
    const uint8_t* data = buffer.data();

    SHA1(data, blocks * BLOCKSIZE, digest);

    std::string hexdigest = print_sha1(digest);

    if (hexdigest != expected) {
        if (printerror) {
            LOG(ERROR) << "failed to verify blocks (expected " << expected << ", read "
                       << hexdigest << ")";
        }
        return -1;
    }

    return 0;
}

static std::string GetStashFileName(const std::string& base, const std::string& id,
        const std::string& postfix) {
    if (base.empty()) {
        return "";
    }

    std::string fn(STASH_DIRECTORY_BASE);
    fn += "/" + base + "/" + id + postfix;

    return fn;
}

// Does a best effort enumeration of stash files. Ignores possible non-file items in the stash
// directory and continues despite of errors. Calls the 'callback' function for each file.
static void EnumerateStash(const std::string& dirname,
                           const std::function<void(const std::string&)>& callback) {
  if (dirname.empty()) return;

  std::unique_ptr<DIR, decltype(&closedir)> directory(opendir(dirname.c_str()), closedir);

  if (directory == nullptr) {
    if (errno != ENOENT) {
      PLOG(ERROR) << "opendir \"" << dirname << "\" failed";
    }
    return;
  }

  dirent* item;
  while ((item = readdir(directory.get())) != nullptr) {
    if (item->d_type != DT_REG) continue;
    callback(dirname + "/" + item->d_name);
  }
}

// Deletes the stash directory and all files in it. Assumes that it only
// contains files. There is nothing we can do about unlikely, but possible
// errors, so they are merely logged.
static void DeleteFile(const std::string& fn) {
  if (fn.empty()) return;

  LOG(INFO) << "deleting " << fn;

  if (unlink(fn.c_str()) == -1 && errno != ENOENT) {
    PLOG(ERROR) << "unlink \"" << fn << "\" failed";
  }
}

static void DeleteStash(const std::string& base) {
  if (base.empty()) return;

  LOG(INFO) << "deleting stash " << base;

  std::string dirname = GetStashFileName(base, "", "");
  EnumerateStash(dirname, DeleteFile);

  if (rmdir(dirname.c_str()) == -1) {
    if (errno != ENOENT && errno != ENOTDIR) {
      PLOG(ERROR) << "rmdir \"" << dirname << "\" failed";
    }
  }
}

static int LoadStash(CommandParameters& params, const std::string& id, bool verify, size_t* blocks,
                     std::vector<uint8_t>& buffer, bool printnoent) {
  // In verify mode, if source range_set was saved for the given hash, check contents in the source
  // blocks first. If the check fails, search for the stashed files on /cache as usual.
  if (!params.canwrite) {
    if (stash_map.find(id) != stash_map.end()) {
      const RangeSet& src = stash_map[id];
      allocate(src.blocks() * BLOCKSIZE, buffer);

      if (ReadBlocks(src, buffer, params.fd) == -1) {
        LOG(ERROR) << "failed to read source blocks in stash map.";
        return -1;
      }
      if (VerifyBlocks(id, buffer, src.blocks(), true) != 0) {
        LOG(ERROR) << "failed to verify loaded source blocks in stash map.";
        PrintHashForCorruptedStashedBlocks(id, buffer, src);
        return -1;
      }
      return 0;
    }
  }

  size_t blockcount = 0;
  if (!blocks) {
    blocks = &blockcount;
  }

  std::string fn = GetStashFileName(params.stashbase, id, "");

  struct stat sb;
  if (stat(fn.c_str(), &sb) == -1) {
    if (errno != ENOENT || printnoent) {
      PLOG(ERROR) << "stat \"" << fn << "\" failed";
      PrintHashForMissingStashedBlocks(id, params.fd);
    }
    return -1;
  }

  LOG(INFO) << " loading " << fn;

  if ((sb.st_size % BLOCKSIZE) != 0) {
    LOG(ERROR) << fn << " size " << sb.st_size << " not multiple of block size " << BLOCKSIZE;
    return -1;
  }

  android::base::unique_fd fd(TEMP_FAILURE_RETRY(ota_open(fn.c_str(), O_RDONLY)));
  if (fd == -1) {
    PLOG(ERROR) << "open \"" << fn << "\" failed";
    return -1;
  }

  allocate(sb.st_size, buffer);

  if (read_all(fd, buffer, sb.st_size) == -1) {
    return -1;
  }

  *blocks = sb.st_size / BLOCKSIZE;

  if (verify && VerifyBlocks(id, buffer, *blocks, true) != 0) {
    LOG(ERROR) << "unexpected contents in " << fn;
    if (stash_map.find(id) == stash_map.end()) {
      LOG(ERROR) << "failed to find source blocks number for stash " << id
                 << " when executing command: " << params.cmdname;
    } else {
      const RangeSet& src = stash_map[id];
      PrintHashForCorruptedStashedBlocks(id, buffer, src);
    }
    DeleteFile(fn);
    return -1;
  }

  return 0;
}

static int WriteStash(const std::string& base, const std::string& id, int blocks,
                      std::vector<uint8_t>& buffer, bool checkspace, bool* exists) {
    if (base.empty()) {
        return -1;
    }

    if (checkspace && CacheSizeCheck(blocks * BLOCKSIZE) != 0) {
        LOG(ERROR) << "not enough space to write stash";
        return -1;
    }

    std::string fn = GetStashFileName(base, id, ".partial");
    std::string cn = GetStashFileName(base, id, "");

    if (exists) {
        struct stat sb;
        int res = stat(cn.c_str(), &sb);

        if (res == 0) {
            // The file already exists and since the name is the hash of the contents,
            // it's safe to assume the contents are identical (accidental hash collisions
            // are unlikely)
            LOG(INFO) << " skipping " << blocks << " existing blocks in " << cn;
            *exists = true;
            return 0;
        }

        *exists = false;
    }

    LOG(INFO) << " writing " << blocks << " blocks to " << cn;

    android::base::unique_fd fd(
        TEMP_FAILURE_RETRY(ota_open(fn.c_str(), O_WRONLY | O_CREAT | O_TRUNC, STASH_FILE_MODE)));
    if (fd == -1) {
        PLOG(ERROR) << "failed to create \"" << fn << "\"";
        return -1;
    }

    if (fchown(fd, AID_SYSTEM, AID_SYSTEM) != 0) {  // system user
        PLOG(ERROR) << "failed to chown \"" << fn << "\"";
        return -1;
    }

    if (write_all(fd, buffer, blocks * BLOCKSIZE) == -1) {
        return -1;
    }

    if (ota_fsync(fd) == -1) {
        failure_type = kFsyncFailure;
        PLOG(ERROR) << "fsync \"" << fn << "\" failed";
        return -1;
    }

    if (rename(fn.c_str(), cn.c_str()) == -1) {
        PLOG(ERROR) << "rename(\"" << fn << "\", \"" << cn << "\") failed";
        return -1;
    }

    std::string dname = GetStashFileName(base, "", "");
    android::base::unique_fd dfd(TEMP_FAILURE_RETRY(ota_open(dname.c_str(),
                                                             O_RDONLY | O_DIRECTORY)));
    if (dfd == -1) {
        failure_type = kFileOpenFailure;
        PLOG(ERROR) << "failed to open \"" << dname << "\" failed";
        return -1;
    }

    if (ota_fsync(dfd) == -1) {
        failure_type = kFsyncFailure;
        PLOG(ERROR) << "fsync \"" << dname << "\" failed";
        return -1;
    }

    return 0;
}

// Creates a directory for storing stash files and checks if the /cache partition
// hash enough space for the expected amount of blocks we need to store. Returns
// >0 if we created the directory, zero if it existed already, and <0 of failure.

static int CreateStash(State* state, size_t maxblocks, const std::string& blockdev,
                       std::string& base) {
  if (blockdev.empty()) {
    return -1;
  }

  // Stash directory should be different for each partition to avoid conflicts
  // when updating multiple partitions at the same time, so we use the hash of
  // the block device name as the base directory
  uint8_t digest[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const uint8_t*>(blockdev.data()), blockdev.size(), digest);
  base = print_sha1(digest);

  std::string dirname = GetStashFileName(base, "", "");
  struct stat sb;
  int res = stat(dirname.c_str(), &sb);
  size_t max_stash_size = maxblocks * BLOCKSIZE;

  if (res == -1 && errno != ENOENT) {
    ErrorAbort(state, kStashCreationFailure, "stat \"%s\" failed: %s\n", dirname.c_str(),
               strerror(errno));
    return -1;
  } else if (res != 0) {
    LOG(INFO) << "creating stash " << dirname;
    res = mkdir(dirname.c_str(), STASH_DIRECTORY_MODE);

    if (res != 0) {
      ErrorAbort(state, kStashCreationFailure, "mkdir \"%s\" failed: %s\n", dirname.c_str(),
                 strerror(errno));
      return -1;
    }

    if (chown(dirname.c_str(), AID_SYSTEM, AID_SYSTEM) != 0) {  // system user
      ErrorAbort(state, kStashCreationFailure, "chown \"%s\" failed: %s\n", dirname.c_str(),
                 strerror(errno));
      return -1;
    }

    if (CacheSizeCheck(max_stash_size) != 0) {
      ErrorAbort(state, kStashCreationFailure, "not enough space for stash (%zu needed)\n",
                 max_stash_size);
      return -1;
    }

    return 1;  // Created directory
  }

  LOG(INFO) << "using existing stash " << dirname;

  // If the directory already exists, calculate the space already allocated to stash files and check
  // if there's enough for all required blocks. Delete any partially completed stash files first.
  EnumerateStash(dirname, [](const std::string& fn) {
    if (android::base::EndsWith(fn, ".partial")) {
      DeleteFile(fn);
    }
  });

  size_t existing = 0;
  EnumerateStash(dirname, [&existing](const std::string& fn) {
    if (fn.empty()) return;
    struct stat sb;
    if (stat(fn.c_str(), &sb) == -1) {
      PLOG(ERROR) << "stat \"" << fn << "\" failed";
      return;
    }
    existing += static_cast<size_t>(sb.st_size);
  });

  if (max_stash_size > existing) {
    size_t needed = max_stash_size - existing;
    if (CacheSizeCheck(needed) != 0) {
      ErrorAbort(state, kStashCreationFailure, "not enough space for stash (%zu more needed)\n",
                 needed);
      return -1;
    }
  }

  return 0;  // Using existing directory
}

static int FreeStash(const std::string& base, const std::string& id) {
  if (base.empty() || id.empty()) {
    return -1;
  }

  DeleteFile(GetStashFileName(base, id, ""));

  return 0;
}

// Source contains packed data, which we want to move to the locations given in locs in the dest
// buffer. source and dest may be the same buffer.
static void MoveRange(std::vector<uint8_t>& dest, const RangeSet& locs,
                      const std::vector<uint8_t>& source) {
  const uint8_t* from = source.data();
  uint8_t* to = dest.data();
  size_t start = locs.blocks();
  // Must do the movement backward.
  for (auto it = locs.crbegin(); it != locs.crend(); it++) {
    size_t blocks = it->second - it->first;
    start -= blocks;
    memmove(to + (it->first * BLOCKSIZE), from + (start * BLOCKSIZE), blocks * BLOCKSIZE);
  }
}

/**
 * We expect to parse the remainder of the parameter tokens as one of:
 *
 *    <src_block_count> <src_range>
 *        (loads data from source image only)
 *
 *    <src_block_count> - <[stash_id:stash_range] ...>
 *        (loads data from stashes only)
 *
 *    <src_block_count> <src_range> <src_loc> <[stash_id:stash_range] ...>
 *        (loads data from both source image and stashes)
 *
 * On return, params.buffer is filled with the loaded source data (rearranged and combined with
 * stashed data as necessary). buffer may be reallocated if needed to accommodate the source data.
 * tgt is the target RangeSet for detecting overlaps. Any stashes required are loaded using
 * LoadStash.
 */
static int LoadSourceBlocks(CommandParameters& params, const RangeSet& tgt, size_t* src_blocks,
                            bool* overlap) {
  CHECK(src_blocks != nullptr);
  CHECK(overlap != nullptr);

  // <src_block_count>
  const std::string& token = params.tokens[params.cpos++];
  if (!android::base::ParseUint(token, src_blocks)) {
    LOG(ERROR) << "invalid src_block_count \"" << token << "\"";
    return -1;
  }

  allocate(*src_blocks * BLOCKSIZE, params.buffer);

  // "-" or <src_range> [<src_loc>]
  if (params.tokens[params.cpos] == "-") {
    // no source ranges, only stashes
    params.cpos++;
  } else {
    RangeSet src = RangeSet::Parse(params.tokens[params.cpos++]);
    *overlap = src.Overlaps(tgt);

    if (ReadBlocks(src, params.buffer, params.fd) == -1) {
      return -1;
    }

    if (params.cpos >= params.tokens.size()) {
      // no stashes, only source range
      return 0;
    }

    RangeSet locs = RangeSet::Parse(params.tokens[params.cpos++]);
    MoveRange(params.buffer, locs, params.buffer);
  }

  // <[stash_id:stash_range]>
  while (params.cpos < params.tokens.size()) {
    // Each word is a an index into the stash table, a colon, and then a RangeSet describing where
    // in the source block that stashed data should go.
    std::vector<std::string> tokens = android::base::Split(params.tokens[params.cpos++], ":");
    if (tokens.size() != 2) {
      LOG(ERROR) << "invalid parameter";
      return -1;
    }

    std::vector<uint8_t> stash;
    if (LoadStash(params, tokens[0], false, nullptr, stash, true) == -1) {
      // These source blocks will fail verification if used later, but we
      // will let the caller decide if this is a fatal failure
      LOG(ERROR) << "failed to load stash " << tokens[0];
      continue;
    }

    RangeSet locs = RangeSet::Parse(tokens[1]);
    MoveRange(params.buffer, locs, stash);
  }

  return 0;
}

/**
 * Do a source/target load for move/bsdiff/imgdiff in version 3.
 *
 * We expect to parse the remainder of the parameter tokens as one of:
 *
 *    <tgt_range> <src_block_count> <src_range>
 *        (loads data from source image only)
 *
 *    <tgt_range> <src_block_count> - <[stash_id:stash_range] ...>
 *        (loads data from stashes only)
 *
 *    <tgt_range> <src_block_count> <src_range> <src_loc> <[stash_id:stash_range] ...>
 *        (loads data from both source image and stashes)
 *
 * 'onehash' tells whether to expect separate source and targe block hashes, or if they are both the
 * same and only one hash should be expected. params.isunresumable will be set to true if block
 * verification fails in a way that the update cannot be resumed anymore.
 *
 * If the function is unable to load the necessary blocks or their contents don't match the hashes,
 * the return value is -1 and the command should be aborted.
 *
 * If the return value is 1, the command has already been completed according to the contents of the
 * target blocks, and should not be performed again.
 *
 * If the return value is 0, source blocks have expected content and the command can be performed.
 */
static int LoadSrcTgtVersion3(CommandParameters& params, RangeSet& tgt, size_t* src_blocks,
                              bool onehash, bool* overlap) {
  CHECK(src_blocks != nullptr);
  CHECK(overlap != nullptr);

  if (params.cpos >= params.tokens.size()) {
    LOG(ERROR) << "missing source hash";
    return -1;
  }

  std::string srchash = params.tokens[params.cpos++];
  std::string tgthash;

  if (onehash) {
    tgthash = srchash;
  } else {
    if (params.cpos >= params.tokens.size()) {
      LOG(ERROR) << "missing target hash";
      return -1;
    }
    tgthash = params.tokens[params.cpos++];
  }

  // At least it needs to provide three parameters: <tgt_range>, <src_block_count> and
  // "-"/<src_range>.
  if (params.cpos + 2 >= params.tokens.size()) {
    LOG(ERROR) << "invalid parameters";
    return -1;
  }

  // <tgt_range>
  tgt = RangeSet::Parse(params.tokens[params.cpos++]);

  std::vector<uint8_t> tgtbuffer(tgt.blocks() * BLOCKSIZE);
  if (ReadBlocks(tgt, tgtbuffer, params.fd) == -1) {
    return -1;
  }

  // Return now if target blocks already have expected content.
  if (VerifyBlocks(tgthash, tgtbuffer, tgt.blocks(), false) == 0) {
    return 1;
  }

  // Load source blocks.
  if (LoadSourceBlocks(params, tgt, src_blocks, overlap) == -1) {
    return -1;
  }

  if (VerifyBlocks(srchash, params.buffer, *src_blocks, true) == 0) {
    // If source and target blocks overlap, stash the source blocks so we can
    // resume from possible write errors. In verify mode, we can skip stashing
    // because the source blocks won't be overwritten.
    if (*overlap && params.canwrite) {
      LOG(INFO) << "stashing " << *src_blocks << " overlapping blocks to " << srchash;

      bool stash_exists = false;
      if (WriteStash(params.stashbase, srchash, *src_blocks, params.buffer, true,
                     &stash_exists) != 0) {
        LOG(ERROR) << "failed to stash overlapping source blocks";
        return -1;
      }

      params.stashed += *src_blocks;
      // Can be deleted when the write has completed.
      if (!stash_exists) {
        params.freestash = srchash;
      }
    }

    // Source blocks have expected content, command can proceed.
    return 0;
  }

  if (*overlap && LoadStash(params, srchash, true, nullptr, params.buffer, true) == 0) {
    // Overlapping source blocks were previously stashed, command can proceed. We are recovering
    // from an interrupted command, so we don't know if the stash can safely be deleted after this
    // command.
    return 0;
  }

  // Valid source data not available, update cannot be resumed.
  LOG(ERROR) << "partition has unexpected contents";
  PrintHashForCorruptedSourceBlocks(params, params.buffer);

  params.isunresumable = true;

  return -1;
}

static int PerformCommandMove(CommandParameters& params) {
  size_t blocks = 0;
  bool overlap = false;
  RangeSet tgt;
  int status = LoadSrcTgtVersion3(params, tgt, &blocks, true, &overlap);

  if (status == -1) {
    LOG(ERROR) << "failed to read blocks for move";
    return -1;
  }

  if (status == 0) {
    params.foundwrites = true;
  } else if (params.foundwrites) {
    LOG(WARNING) << "warning: commands executed out of order [" << params.cmdname << "]";
  }

  if (params.canwrite) {
    if (status == 0) {
      LOG(INFO) << "  moving " << blocks << " blocks";

      if (WriteBlocks(tgt, params.buffer, params.fd) == -1) {
        return -1;
      }
    } else {
      LOG(INFO) << "skipping " << blocks << " already moved blocks";
    }
  }

  if (!params.freestash.empty()) {
    FreeStash(params.stashbase, params.freestash);
    params.freestash.clear();
  }

  params.written += tgt.blocks();

  return 0;
}

static int PerformCommandStash(CommandParameters& params) {
  // <stash_id> <src_range>
  if (params.cpos + 1 >= params.tokens.size()) {
    LOG(ERROR) << "missing id and/or src range fields in stash command";
    return -1;
  }

  const std::string& id = params.tokens[params.cpos++];
  size_t blocks = 0;
  if (LoadStash(params, id, true, &blocks, params.buffer, false) == 0) {
    // Stash file already exists and has expected contents. Do not read from source again, as the
    // source may have been already overwritten during a previous attempt.
    return 0;
  }

  RangeSet src = RangeSet::Parse(params.tokens[params.cpos++]);

  allocate(src.blocks() * BLOCKSIZE, params.buffer);
  if (ReadBlocks(src, params.buffer, params.fd) == -1) {
    return -1;
  }
  blocks = src.blocks();
  stash_map[id] = src;

  if (VerifyBlocks(id, params.buffer, blocks, true) != 0) {
    // Source blocks have unexpected contents. If we actually need this data later, this is an
    // unrecoverable error. However, the command that uses the data may have already completed
    // previously, so the possible failure will occur during source block verification.
    LOG(ERROR) << "failed to load source blocks for stash " << id;
    return 0;
  }

  // In verify mode, we don't need to stash any blocks.
  if (!params.canwrite) {
    return 0;
  }

  LOG(INFO) << "stashing " << blocks << " blocks to " << id;
  params.stashed += blocks;
  return WriteStash(params.stashbase, id, blocks, params.buffer, false, nullptr);
}

static int PerformCommandFree(CommandParameters& params) {
  // <stash_id>
  if (params.cpos >= params.tokens.size()) {
    LOG(ERROR) << "missing stash id in free command";
    return -1;
  }

  const std::string& id = params.tokens[params.cpos++];
  stash_map.erase(id);

  if (params.createdstash || params.canwrite) {
    return FreeStash(params.stashbase, id);
  }

  return 0;
}

static int PerformCommandZero(CommandParameters& params) {
  if (params.cpos >= params.tokens.size()) {
    LOG(ERROR) << "missing target blocks for zero";
    return -1;
  }

  RangeSet tgt = RangeSet::Parse(params.tokens[params.cpos++]);

  LOG(INFO) << "  zeroing " << tgt.blocks() << " blocks";

  allocate(BLOCKSIZE, params.buffer);
  memset(params.buffer.data(), 0, BLOCKSIZE);

  if (params.canwrite) {
    for (const auto& range : tgt) {
      off64_t offset = static_cast<off64_t>(range.first) * BLOCKSIZE;
      size_t size = (range.second - range.first) * BLOCKSIZE;
      if (!discard_blocks(params.fd, offset, size)) {
        return -1;
      }

      if (!check_lseek(params.fd, offset, SEEK_SET)) {
        return -1;
      }

      for (size_t j = range.first; j < range.second; ++j) {
        if (write_all(params.fd, params.buffer, BLOCKSIZE) == -1) {
          return -1;
        }
      }
    }
  }

  if (params.cmdname[0] == 'z') {
    // Update only for the zero command, as the erase command will call
    // this if DEBUG_ERASE is defined.
    params.written += tgt.blocks();
  }

  return 0;
}

static int PerformCommandNew(CommandParameters& params) {
  if (params.cpos >= params.tokens.size()) {
    LOG(ERROR) << "missing target blocks for new";
    return -1;
  }

  RangeSet tgt = RangeSet::Parse(params.tokens[params.cpos++]);

  if (params.canwrite) {
    LOG(INFO) << " writing " << tgt.blocks() << " blocks of new data";

    pthread_mutex_lock(&params.nti.mu);
    params.nti.writer = std::make_unique<RangeSinkWriter>(params.fd, tgt);
    pthread_cond_broadcast(&params.nti.cv);

    while (params.nti.writer != nullptr) {
      if (!params.nti.receiver_available) {
        LOG(ERROR) << "missing " << (tgt.blocks() * BLOCKSIZE - params.nti.writer->BytesWritten())
                   << " bytes of new data";
        pthread_mutex_unlock(&params.nti.mu);
        return -1;
      }
      pthread_cond_wait(&params.nti.cv, &params.nti.mu);
    }

    pthread_mutex_unlock(&params.nti.mu);
  }

  params.written += tgt.blocks();

  return 0;
}

static int PerformCommandDiff(CommandParameters& params) {
  // <offset> <length>
  if (params.cpos + 1 >= params.tokens.size()) {
    LOG(ERROR) << "missing patch offset or length for " << params.cmdname;
    return -1;
  }

  size_t offset;
  if (!android::base::ParseUint(params.tokens[params.cpos++], &offset)) {
    LOG(ERROR) << "invalid patch offset";
    return -1;
  }

  size_t len;
  if (!android::base::ParseUint(params.tokens[params.cpos++], &len)) {
    LOG(ERROR) << "invalid patch len";
    return -1;
  }

  RangeSet tgt;
  size_t blocks = 0;
  bool overlap = false;
  int status = LoadSrcTgtVersion3(params, tgt, &blocks, false, &overlap);

  if (status == -1) {
    LOG(ERROR) << "failed to read blocks for diff";
    return -1;
  }

  if (status == 0) {
    params.foundwrites = true;
  } else if (params.foundwrites) {
    LOG(WARNING) << "warning: commands executed out of order [" << params.cmdname << "]";
  }

  if (params.canwrite) {
    if (status == 0) {
      LOG(INFO) << "patching " << blocks << " blocks to " << tgt.blocks();
      Value patch_value(
          VAL_BLOB, std::string(reinterpret_cast<const char*>(params.patch_start + offset), len));

      RangeSinkWriter writer(params.fd, tgt);
      if (params.cmdname[0] == 'i') {  // imgdiff
        if (ApplyImagePatch(params.buffer.data(), blocks * BLOCKSIZE, &patch_value,
                            std::bind(&RangeSinkWriter::Write, &writer, std::placeholders::_1,
                                      std::placeholders::_2),
                            nullptr, nullptr) != 0) {
          LOG(ERROR) << "Failed to apply image patch.";
          failure_type = kPatchApplicationFailure;
          return -1;
        }
      } else {
        if (ApplyBSDiffPatch(params.buffer.data(), blocks * BLOCKSIZE, &patch_value, 0,
                             std::bind(&RangeSinkWriter::Write, &writer, std::placeholders::_1,
                                       std::placeholders::_2),
                             nullptr) != 0) {
          LOG(ERROR) << "Failed to apply bsdiff patch.";
          failure_type = kPatchApplicationFailure;
          return -1;
        }
      }

      // We expect the output of the patcher to fill the tgt ranges exactly.
      if (!writer.Finished()) {
        LOG(ERROR) << "range sink underrun?";
      }
    } else {
      LOG(INFO) << "skipping " << blocks << " blocks already patched to " << tgt.blocks() << " ["
                << params.cmdline << "]";
    }
  }

  if (!params.freestash.empty()) {
    FreeStash(params.stashbase, params.freestash);
    params.freestash.clear();
  }

  params.written += tgt.blocks();

  return 0;
}

static int PerformCommandErase(CommandParameters& params) {
  if (DEBUG_ERASE) {
    return PerformCommandZero(params);
  }

  struct stat sb;
  if (fstat(params.fd, &sb) == -1) {
    PLOG(ERROR) << "failed to fstat device to erase";
    return -1;
  }

  if (!S_ISBLK(sb.st_mode)) {
    LOG(ERROR) << "not a block device; skipping erase";
    return -1;
  }

  if (params.cpos >= params.tokens.size()) {
    LOG(ERROR) << "missing target blocks for erase";
    return -1;
  }

  RangeSet tgt = RangeSet::Parse(params.tokens[params.cpos++]);

  if (params.canwrite) {
    LOG(INFO) << " erasing " << tgt.blocks() << " blocks";

    for (const auto& range : tgt) {
      uint64_t blocks[2];
      // offset in bytes
      blocks[0] = range.first * static_cast<uint64_t>(BLOCKSIZE);
      // length in bytes
      blocks[1] = (range.second - range.first) * static_cast<uint64_t>(BLOCKSIZE);

#ifndef SUPPRESS_EMMC_WIPE
      if (ioctl(params.fd, BLKDISCARD, &blocks) == -1) {
        PLOG(ERROR) << "BLKDISCARD ioctl failed";
        return -1;
      }
#endif
    }
  }

  return 0;
}

// Definitions for transfer list command functions
typedef int (*CommandFunction)(CommandParameters&);

struct Command {
    const char* name;
    CommandFunction f;
};

// args:
//    - block device (or file) to modify in-place
//    - transfer list (blob)
//    - new data stream (filename within package.zip)
//    - patch stream (filename within package.zip, must be uncompressed)

static Value* PerformBlockImageUpdate(const char* name, State* state,
                                      const std::vector<std::unique_ptr<Expr>>& argv,
                                      const Command* commands, size_t cmdcount, bool dryrun) {
  CommandParameters params = {};
  params.canwrite = !dryrun;

  LOG(INFO) << "performing " << (dryrun ? "verification" : "update");
  if (state->is_retry) {
    is_retry = true;
    LOG(INFO) << "This update is a retry.";
  }
  if (argv.size() != 4) {
    ErrorAbort(state, kArgsParsingFailure, "block_image_update expects 4 arguments, got %zu",
               argv.size());
    return StringValue("");
  }

  std::vector<std::unique_ptr<Value>> args;
  if (!ReadValueArgs(state, argv, &args)) {
    return nullptr;
  }

  const std::unique_ptr<Value>& blockdev_filename = args[0];
  const std::unique_ptr<Value>& transfer_list_value = args[1];
  const std::unique_ptr<Value>& new_data_fn = args[2];
  const std::unique_ptr<Value>& patch_data_fn = args[3];

  if (blockdev_filename->type != VAL_STRING) {
    ErrorAbort(state, kArgsParsingFailure, "blockdev_filename argument to %s must be string", name);
    return StringValue("");
  }
  if (transfer_list_value->type != VAL_BLOB) {
    ErrorAbort(state, kArgsParsingFailure, "transfer_list argument to %s must be blob", name);
    return StringValue("");
  }
  if (new_data_fn->type != VAL_STRING) {
    ErrorAbort(state, kArgsParsingFailure, "new_data_fn argument to %s must be string", name);
    return StringValue("");
  }
  if (patch_data_fn->type != VAL_STRING) {
    ErrorAbort(state, kArgsParsingFailure, "patch_data_fn argument to %s must be string", name);
    return StringValue("");
  }

  UpdaterInfo* ui = static_cast<UpdaterInfo*>(state->cookie);
  if (ui == nullptr) {
    return StringValue("");
  }

  FILE* cmd_pipe = ui->cmd_pipe;
  ZipArchiveHandle za = ui->package_zip;

  if (cmd_pipe == nullptr || za == nullptr) {
    return StringValue("");
  }

  ZipString path_data(patch_data_fn->data.c_str());
  ZipEntry patch_entry;
  if (FindEntry(za, path_data, &patch_entry) != 0) {
    LOG(ERROR) << name << "(): no file \"" << patch_data_fn->data << "\" in package";
    return StringValue("");
  }

  params.patch_start = ui->package_zip_addr + patch_entry.offset;
  ZipString new_data(new_data_fn->data.c_str());
  ZipEntry new_entry;
  if (FindEntry(za, new_data, &new_entry) != 0) {
    LOG(ERROR) << name << "(): no file \"" << new_data_fn->data << "\" in package";
    return StringValue("");
  }

  params.fd.reset(TEMP_FAILURE_RETRY(ota_open(blockdev_filename->data.c_str(), O_RDWR)));
  if (params.fd == -1) {
    PLOG(ERROR) << "open \"" << blockdev_filename->data << "\" failed";
    return StringValue("");
  }

  if (params.canwrite) {
    params.nti.za = za;
    params.nti.entry = new_entry;
    params.nti.brotli_compressed = android::base::EndsWith(new_data_fn->data, ".br");
    if (params.nti.brotli_compressed) {
      // Initialize brotli decoder state.
      params.nti.brotli_decoder_state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    }
    params.nti.receiver_available = true;

    pthread_mutex_init(&params.nti.mu, nullptr);
    pthread_cond_init(&params.nti.cv, nullptr);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    int error = pthread_create(&params.thread, &attr, unzip_new_data, &params.nti);
    if (error != 0) {
      PLOG(ERROR) << "pthread_create failed";
      return StringValue("");
    }
  }

  std::vector<std::string> lines = android::base::Split(transfer_list_value->data, "\n");
  if (lines.size() < 2) {
    ErrorAbort(state, kArgsParsingFailure, "too few lines in the transfer list [%zd]\n",
               lines.size());
    return StringValue("");
  }

  // First line in transfer list is the version number.
  if (!android::base::ParseInt(lines[0], &params.version, 3, 4)) {
    LOG(ERROR) << "unexpected transfer list version [" << lines[0] << "]";
    return StringValue("");
  }

  LOG(INFO) << "blockimg version is " << params.version;

  // Second line in transfer list is the total number of blocks we expect to write.
  size_t total_blocks;
  if (!android::base::ParseUint(lines[1], &total_blocks)) {
    ErrorAbort(state, kArgsParsingFailure, "unexpected block count [%s]\n", lines[1].c_str());
    return StringValue("");
  }

  if (total_blocks == 0) {
    return StringValue("t");
  }

  size_t start = 2;
  if (lines.size() < 4) {
    ErrorAbort(state, kArgsParsingFailure, "too few lines in the transfer list [%zu]\n",
               lines.size());
    return StringValue("");
  }

  // Third line is how many stash entries are needed simultaneously.
  LOG(INFO) << "maximum stash entries " << lines[2];

  // Fourth line is the maximum number of blocks that will be stashed simultaneously
  size_t stash_max_blocks;
  if (!android::base::ParseUint(lines[3], &stash_max_blocks)) {
    ErrorAbort(state, kArgsParsingFailure, "unexpected maximum stash blocks [%s]\n",
               lines[3].c_str());
    return StringValue("");
  }

  int res = CreateStash(state, stash_max_blocks, blockdev_filename->data, params.stashbase);
  if (res == -1) {
    return StringValue("");
  }

  params.createdstash = res;

  start += 2;

  // Build a map of the available commands
  std::unordered_map<std::string, const Command*> cmd_map;
  for (size_t i = 0; i < cmdcount; ++i) {
    if (cmd_map.find(commands[i].name) != cmd_map.end()) {
      LOG(ERROR) << "Error: command [" << commands[i].name << "] already exists in the cmd map.";
      return StringValue(strdup(""));
    }
    cmd_map[commands[i].name] = &commands[i];
  }

  int rc = -1;

  // Subsequent lines are all individual transfer commands
  for (auto it = lines.cbegin() + start; it != lines.cend(); it++) {
    const std::string& line(*it);
    if (line.empty()) continue;

    params.tokens = android::base::Split(line, " ");
    params.cpos = 0;
    params.cmdname = params.tokens[params.cpos++].c_str();
    params.cmdline = line.c_str();

    if (cmd_map.find(params.cmdname) == cmd_map.end()) {
      LOG(ERROR) << "unexpected command [" << params.cmdname << "]";
      goto pbiudone;
    }

    const Command* cmd = cmd_map[params.cmdname];

    if (cmd->f != nullptr && cmd->f(params) == -1) {
      LOG(ERROR) << "failed to execute command [" << line << "]";
      goto pbiudone;
    }

    if (params.canwrite) {
      if (ota_fsync(params.fd) == -1) {
        failure_type = kFsyncFailure;
        PLOG(ERROR) << "fsync failed";
        goto pbiudone;
      }
      fprintf(cmd_pipe, "set_progress %.4f\n", static_cast<double>(params.written) / total_blocks);
      fflush(cmd_pipe);
    }
  }

  if (params.canwrite) {
    pthread_join(params.thread, nullptr);

    LOG(INFO) << "wrote " << params.written << " blocks; expected " << total_blocks;
    LOG(INFO) << "stashed " << params.stashed << " blocks";
    LOG(INFO) << "max alloc needed was " << params.buffer.size();

    const char* partition = strrchr(blockdev_filename->data.c_str(), '/');
    if (partition != nullptr && *(partition + 1) != 0) {
      fprintf(cmd_pipe, "log bytes_written_%s: %zu\n", partition + 1, params.written * BLOCKSIZE);
      fprintf(cmd_pipe, "log bytes_stashed_%s: %zu\n", partition + 1, params.stashed * BLOCKSIZE);
      fflush(cmd_pipe);
    }
    // Delete stash only after successfully completing the update, as it may contain blocks needed
    // to complete the update later.
    DeleteStash(params.stashbase);
  } else {
    LOG(INFO) << "verified partition contents; update may be resumed";
  }

  rc = 0;

pbiudone:
  if (ota_fsync(params.fd) == -1) {
    failure_type = kFsyncFailure;
    PLOG(ERROR) << "fsync failed";
  }
  // params.fd will be automatically closed because it's a unique_fd.

  if (params.nti.brotli_decoder_state != nullptr) {
    BrotliDecoderDestroyInstance(params.nti.brotli_decoder_state);
  }

  // Only delete the stash if the update cannot be resumed, or it's a verification run and we
  // created the stash.
  if (params.isunresumable || (!params.canwrite && params.createdstash)) {
    DeleteStash(params.stashbase);
  }

  if (failure_type != kNoCause && state->cause_code == kNoCause) {
    state->cause_code = failure_type;
  }

  return StringValue(rc == 0 ? "t" : "");
}

/**
 * The transfer list is a text file containing commands to transfer data from one place to another
 * on the target partition. We parse it and execute the commands in order:
 *
 *    zero [rangeset]
 *      - Fill the indicated blocks with zeros.
 *
 *    new [rangeset]
 *      - Fill the blocks with data read from the new_data file.
 *
 *    erase [rangeset]
 *      - Mark the given blocks as empty.
 *
 *    move <...>
 *    bsdiff <patchstart> <patchlen> <...>
 *    imgdiff <patchstart> <patchlen> <...>
 *      - Read the source blocks, apply a patch (or not in the case of move), write result to target
 *      blocks.  bsdiff or imgdiff specifies the type of patch; move means no patch at all.
 *
 *        See the comments in LoadSrcTgtVersion3() for a description of the <...> format.
 *
 *    stash <stash_id> <src_range>
 *      - Load the given source range and stash the data in the given slot of the stash table.
 *
 *    free <stash_id>
 *      - Free the given stash data.
 *
 * The creator of the transfer list will guarantee that no block is read (ie, used as the source for
 * a patch or move) after it has been written.
 *
 * The creator will guarantee that a given stash is loaded (with a stash command) before it's used
 * in a move/bsdiff/imgdiff command.
 *
 * Within one command the source and target ranges may overlap so in general we need to read the
 * entire source into memory before writing anything to the target blocks.
 *
 * All the patch data is concatenated into one patch_data file in the update package. It must be
 * stored uncompressed because we memory-map it in directly from the archive. (Since patches are
 * already compressed, we lose very little by not compressing their concatenation.)
 *
 * Commands that read data from the partition (i.e. move/bsdiff/imgdiff/stash) have one or more
 * additional hashes before the range parameters, which are used to check if the command has already
 * been completed and verify the integrity of the source data.
 */
Value* BlockImageVerifyFn(const char* name, State* state,
                          const std::vector<std::unique_ptr<Expr>>& argv) {
    // Commands which are not tested are set to nullptr to skip them completely
    const Command commands[] = {
        { "bsdiff",     PerformCommandDiff  },
        { "erase",      nullptr             },
        { "free",       PerformCommandFree  },
        { "imgdiff",    PerformCommandDiff  },
        { "move",       PerformCommandMove  },
        { "new",        nullptr             },
        { "stash",      PerformCommandStash },
        { "zero",       nullptr             }
    };

    // Perform a dry run without writing to test if an update can proceed
    return PerformBlockImageUpdate(name, state, argv, commands,
                sizeof(commands) / sizeof(commands[0]), true);
}

Value* BlockImageUpdateFn(const char* name, State* state,
                          const std::vector<std::unique_ptr<Expr>>& argv) {
    const Command commands[] = {
        { "bsdiff",     PerformCommandDiff  },
        { "erase",      PerformCommandErase },
        { "free",       PerformCommandFree  },
        { "imgdiff",    PerformCommandDiff  },
        { "move",       PerformCommandMove  },
        { "new",        PerformCommandNew   },
        { "stash",      PerformCommandStash },
        { "zero",       PerformCommandZero  }
    };

    return PerformBlockImageUpdate(name, state, argv, commands,
                sizeof(commands) / sizeof(commands[0]), false);
}

Value* RangeSha1Fn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 2) {
    ErrorAbort(state, kArgsParsingFailure, "range_sha1 expects 2 arguments, got %zu", argv.size());
    return StringValue("");
  }

  std::vector<std::unique_ptr<Value>> args;
  if (!ReadValueArgs(state, argv, &args)) {
    return nullptr;
  }

  const std::unique_ptr<Value>& blockdev_filename = args[0];
  const std::unique_ptr<Value>& ranges = args[1];

  if (blockdev_filename->type != VAL_STRING) {
    ErrorAbort(state, kArgsParsingFailure, "blockdev_filename argument to %s must be string", name);
    return StringValue("");
  }
  if (ranges->type != VAL_STRING) {
    ErrorAbort(state, kArgsParsingFailure, "ranges argument to %s must be string", name);
    return StringValue("");
  }

  android::base::unique_fd fd(ota_open(blockdev_filename->data.c_str(), O_RDWR));
  if (fd == -1) {
    ErrorAbort(state, kFileOpenFailure, "open \"%s\" failed: %s", blockdev_filename->data.c_str(),
               strerror(errno));
    return StringValue("");
  }

  RangeSet rs = RangeSet::Parse(ranges->data);

  SHA_CTX ctx;
  SHA1_Init(&ctx);

  std::vector<uint8_t> buffer(BLOCKSIZE);
  for (const auto& range : rs) {
    if (!check_lseek(fd, static_cast<off64_t>(range.first) * BLOCKSIZE, SEEK_SET)) {
      ErrorAbort(state, kLseekFailure, "failed to seek %s: %s", blockdev_filename->data.c_str(),
                 strerror(errno));
      return StringValue("");
    }

    for (size_t j = range.first; j < range.second; ++j) {
      if (read_all(fd, buffer, BLOCKSIZE) == -1) {
        ErrorAbort(state, kFreadFailure, "failed to read %s: %s", blockdev_filename->data.c_str(),
                   strerror(errno));
        return StringValue("");
      }

      SHA1_Update(&ctx, buffer.data(), BLOCKSIZE);
    }
  }
  uint8_t digest[SHA_DIGEST_LENGTH];
  SHA1_Final(digest, &ctx);

  return StringValue(print_sha1(digest));
}

// This function checks if a device has been remounted R/W prior to an incremental
// OTA update. This is an common cause of update abortion. The function reads the
// 1st block of each partition and check for mounting time/count. It return string "t"
// if executes successfully and an empty string otherwise.

Value* CheckFirstBlockFn(const char* name, State* state,
                         const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 1) {
    ErrorAbort(state, kArgsParsingFailure, "check_first_block expects 1 argument, got %zu",
               argv.size());
    return StringValue("");
  }

  std::vector<std::unique_ptr<Value>> args;
  if (!ReadValueArgs(state, argv, &args)) {
    return nullptr;
  }

  const std::unique_ptr<Value>& arg_filename = args[0];

  if (arg_filename->type != VAL_STRING) {
    ErrorAbort(state, kArgsParsingFailure, "filename argument to %s must be string", name);
    return StringValue("");
  }

  android::base::unique_fd fd(ota_open(arg_filename->data.c_str(), O_RDONLY));
  if (fd == -1) {
    ErrorAbort(state, kFileOpenFailure, "open \"%s\" failed: %s", arg_filename->data.c_str(),
               strerror(errno));
    return StringValue("");
  }

  RangeSet blk0(std::vector<Range>{ Range{ 0, 1 } });
  std::vector<uint8_t> block0_buffer(BLOCKSIZE);

  if (ReadBlocks(blk0, block0_buffer, fd) == -1) {
    ErrorAbort(state, kFreadFailure, "failed to read %s: %s", arg_filename->data.c_str(),
               strerror(errno));
    return StringValue("");
  }

  // https://ext4.wiki.kernel.org/index.php/Ext4_Disk_Layout
  // Super block starts from block 0, offset 0x400
  //   0x2C: len32 Mount time
  //   0x30: len32 Write time
  //   0x34: len16 Number of mounts since the last fsck
  //   0x38: len16 Magic signature 0xEF53

  time_t mount_time = *reinterpret_cast<uint32_t*>(&block0_buffer[0x400 + 0x2C]);
  uint16_t mount_count = *reinterpret_cast<uint16_t*>(&block0_buffer[0x400 + 0x34]);

  if (mount_count > 0) {
    uiPrintf(state, "Device was remounted R/W %" PRIu16 " times", mount_count);
    uiPrintf(state, "Last remount happened on %s", ctime(&mount_time));
  }

  return StringValue("t");
}

Value* BlockImageRecoverFn(const char* name, State* state,
                           const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 2) {
    ErrorAbort(state, kArgsParsingFailure, "block_image_recover expects 2 arguments, got %zu",
               argv.size());
    return StringValue("");
  }

  std::vector<std::unique_ptr<Value>> args;
  if (!ReadValueArgs(state, argv, &args)) {
    return nullptr;
  }

  const std::unique_ptr<Value>& filename = args[0];
  const std::unique_ptr<Value>& ranges = args[1];

  if (filename->type != VAL_STRING) {
    ErrorAbort(state, kArgsParsingFailure, "filename argument to %s must be string", name);
    return StringValue("");
  }
  if (ranges->type != VAL_STRING) {
    ErrorAbort(state, kArgsParsingFailure, "ranges argument to %s must be string", name);
    return StringValue("");
  }

  // Output notice to log when recover is attempted
  LOG(INFO) << filename->data << " image corrupted, attempting to recover...";

  // When opened with O_RDWR, libfec rewrites corrupted blocks when they are read
  fec::io fh(filename->data, O_RDWR);

  if (!fh) {
    ErrorAbort(state, kLibfecFailure, "fec_open \"%s\" failed: %s", filename->data.c_str(),
               strerror(errno));
    return StringValue("");
  }

  if (!fh.has_ecc() || !fh.has_verity()) {
    ErrorAbort(state, kLibfecFailure, "unable to use metadata to correct errors");
    return StringValue("");
  }

  fec_status status;
  if (!fh.get_status(status)) {
    ErrorAbort(state, kLibfecFailure, "failed to read FEC status");
    return StringValue("");
  }

  uint8_t buffer[BLOCKSIZE];
  for (const auto& range : RangeSet::Parse(ranges->data)) {
    for (size_t j = range.first; j < range.second; ++j) {
      // Stay within the data area, libfec validates and corrects metadata
      if (status.data_size <= static_cast<uint64_t>(j) * BLOCKSIZE) {
        continue;
      }

      if (fh.pread(buffer, BLOCKSIZE, static_cast<off64_t>(j) * BLOCKSIZE) != BLOCKSIZE) {
        ErrorAbort(state, kLibfecFailure, "failed to recover %s (block %zu): %s",
                   filename->data.c_str(), j, strerror(errno));
        return StringValue("");
      }

      // If we want to be able to recover from a situation where rewriting a corrected
      // block doesn't guarantee the same data will be returned when re-read later, we
      // can save a copy of corrected blocks to /cache. Note:
      //
      //  1. Maximum space required from /cache is the same as the maximum number of
      //     corrupted blocks we can correct. For RS(255, 253) and a 2 GiB partition,
      //     this would be ~16 MiB, for example.
      //
      //  2. To find out if this block was corrupted, call fec_get_status after each
      //     read and check if the errors field value has increased.
    }
  }
  LOG(INFO) << "..." << filename->data << " image recovered successfully.";
  return StringValue("t");
}

void RegisterBlockImageFunctions() {
  RegisterFunction("block_image_verify", BlockImageVerifyFn);
  RegisterFunction("block_image_update", BlockImageUpdateFn);
  RegisterFunction("block_image_recover", BlockImageRecoverFn);
  RegisterFunction("check_first_block", CheckFirstBlockFn);
  RegisterFunction("range_sha1", RangeSha1Fn);
}
