/*
 * Copyright (C) 2009 The Android Open Source Project
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

// See imgdiff.cpp in this directory for a description of the patch file
// format.

#include <applypatch/imgpatch.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/cdefs.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/memory.h>
#include <applypatch/applypatch.h>
#include <applypatch/imgdiff.h>
#include <openssl/sha.h>
#include <zlib.h>

#include "edify/expr.h"
#include "otautil/print_sha1.h"

static inline int64_t Read8(const void *address) {
  return android::base::get_unaligned<int64_t>(address);
}

static inline int32_t Read4(const void *address) {
  return android::base::get_unaligned<int32_t>(address);
}

// This function is a wrapper of ApplyBSDiffPatch(). It has a custom sink function to deflate the
// patched data and stream the deflated data to output.
static bool ApplyBSDiffPatchAndStreamOutput(const uint8_t* src_data, size_t src_len,
                                            const Value& patch, size_t patch_offset,
                                            const char* deflate_header, SinkFn sink) {
  size_t expected_target_length = static_cast<size_t>(Read8(deflate_header + 32));
  CHECK_GT(expected_target_length, static_cast<size_t>(0));
  int level = Read4(deflate_header + 40);
  int method = Read4(deflate_header + 44);
  int window_bits = Read4(deflate_header + 48);
  int mem_level = Read4(deflate_header + 52);
  int strategy = Read4(deflate_header + 56);

  z_stream strm;
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = 0;
  strm.next_in = nullptr;
  int ret = deflateInit2(&strm, level, method, window_bits, mem_level, strategy);
  if (ret != Z_OK) {
    LOG(ERROR) << "Failed to init uncompressed data deflation: " << ret;
    return false;
  }

  // Define a custom sink wrapper that feeds to bspatch. It deflates the available patch data on
  // the fly and outputs the compressed data to the given sink.
  size_t actual_target_length = 0;
  size_t total_written = 0;
  static constexpr size_t buffer_size = 32768;
  auto compression_sink = [&strm, &actual_target_length, &expected_target_length, &total_written,
                           &ret, &sink](const uint8_t* data, size_t len) -> size_t {
    // The input patch length for an update never exceeds INT_MAX.
    strm.avail_in = len;
    strm.next_in = data;
    do {
      std::vector<uint8_t> buffer(buffer_size);
      strm.avail_out = buffer_size;
      strm.next_out = buffer.data();
      if (actual_target_length + len < expected_target_length) {
        ret = deflate(&strm, Z_NO_FLUSH);
      } else {
        ret = deflate(&strm, Z_FINISH);
      }
      if (ret != Z_OK && ret != Z_STREAM_END) {
        LOG(ERROR) << "Failed to deflate stream: " << ret;
        // zero length indicates an error in the sink function of bspatch().
        return 0;
      }

      size_t have = buffer_size - strm.avail_out;
      total_written += have;
      if (sink(buffer.data(), have) != have) {
        LOG(ERROR) << "Failed to write " << have << " compressed bytes to output.";
        return 0;
      }
    } while ((strm.avail_in != 0 || strm.avail_out == 0) && ret != Z_STREAM_END);

    actual_target_length += len;
    return len;
  };

  int bspatch_result = ApplyBSDiffPatch(src_data, src_len, patch, patch_offset, compression_sink);
  deflateEnd(&strm);

  if (bspatch_result != 0) {
    return false;
  }

  if (ret != Z_STREAM_END) {
    LOG(ERROR) << "ret is expected to be Z_STREAM_END, but it's " << ret;
    return false;
  }

  if (expected_target_length != actual_target_length) {
    LOG(ERROR) << "target length is expected to be " << expected_target_length << ", but it's "
               << actual_target_length;
    return false;
  }
  LOG(DEBUG) << "bspatch wrote " << total_written << " bytes in total to streaming output.";

  return true;
}

int ApplyImagePatch(const unsigned char* old_data, size_t old_size, const unsigned char* patch_data,
                    size_t patch_size, SinkFn sink) {
  Value patch(Value::Type::BLOB,
              std::string(reinterpret_cast<const char*>(patch_data), patch_size));
  return ApplyImagePatch(old_data, old_size, patch, sink, nullptr);
}

int ApplyImagePatch(const unsigned char* old_data, size_t old_size, const Value& patch, SinkFn sink,
                    const Value* bonus_data) {
  if (patch.data.size() < 12) {
    printf("patch too short to contain header\n");
    return -1;
  }

  // IMGDIFF2 uses CHUNK_NORMAL, CHUNK_DEFLATE, and CHUNK_RAW. (IMGDIFF1, which is no longer
  // supported, used CHUNK_NORMAL and CHUNK_GZIP.)
  const char* const patch_header = patch.data.data();
  if (memcmp(patch_header, "IMGDIFF2", 8) != 0) {
    printf("corrupt patch file header (magic number)\n");
    return -1;
  }

  int num_chunks = Read4(patch_header + 8);
  size_t pos = 12;
  for (int i = 0; i < num_chunks; ++i) {
    // each chunk's header record starts with 4 bytes.
    if (pos + 4 > patch.data.size()) {
      printf("failed to read chunk %d record\n", i);
      return -1;
    }
    int type = Read4(patch_header + pos);
    pos += 4;

    if (type == CHUNK_NORMAL) {
      const char* normal_header = patch_header + pos;
      pos += 24;
      if (pos > patch.data.size()) {
        printf("failed to read chunk %d normal header data\n", i);
        return -1;
      }

      size_t src_start = static_cast<size_t>(Read8(normal_header));
      size_t src_len = static_cast<size_t>(Read8(normal_header + 8));
      size_t patch_offset = static_cast<size_t>(Read8(normal_header + 16));

      if (src_start + src_len > old_size) {
        printf("source data too short\n");
        return -1;
      }
      if (ApplyBSDiffPatch(old_data + src_start, src_len, patch, patch_offset, sink) != 0) {
        printf("Failed to apply bsdiff patch.\n");
        return -1;
      }

      LOG(DEBUG) << "Processed chunk type normal";
    } else if (type == CHUNK_RAW) {
      const char* raw_header = patch_header + pos;
      pos += 4;
      if (pos > patch.data.size()) {
        printf("failed to read chunk %d raw header data\n", i);
        return -1;
      }

      size_t data_len = static_cast<size_t>(Read4(raw_header));

      if (pos + data_len > patch.data.size()) {
        printf("failed to read chunk %d raw data\n", i);
        return -1;
      }
      if (sink(reinterpret_cast<const unsigned char*>(patch_header + pos), data_len) != data_len) {
        printf("failed to write chunk %d raw data\n", i);
        return -1;
      }
      pos += data_len;

      LOG(DEBUG) << "Processed chunk type raw";
    } else if (type == CHUNK_DEFLATE) {
      // deflate chunks have an additional 60 bytes in their chunk header.
      const char* deflate_header = patch_header + pos;
      pos += 60;
      if (pos > patch.data.size()) {
        printf("failed to read chunk %d deflate header data\n", i);
        return -1;
      }

      size_t src_start = static_cast<size_t>(Read8(deflate_header));
      size_t src_len = static_cast<size_t>(Read8(deflate_header + 8));
      size_t patch_offset = static_cast<size_t>(Read8(deflate_header + 16));
      size_t expanded_len = static_cast<size_t>(Read8(deflate_header + 24));

      if (src_start + src_len > old_size) {
        printf("source data too short\n");
        return -1;
      }

      // Decompress the source data; the chunk header tells us exactly
      // how big we expect it to be when decompressed.

      // Note: expanded_len will include the bonus data size if the patch was constructed with
      // bonus data. The deflation will come up 'bonus_size' bytes short; these must be appended
      // from the bonus_data value.
      size_t bonus_size = (i == 1 && bonus_data != nullptr) ? bonus_data->data.size() : 0;

      std::vector<unsigned char> expanded_source(expanded_len);

      // inflate() doesn't like strm.next_out being a nullptr even with
      // avail_out being zero (Z_STREAM_ERROR).
      if (expanded_len != 0) {
        z_stream strm;
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = src_len;
        strm.next_in = old_data + src_start;
        strm.avail_out = expanded_len;
        strm.next_out = expanded_source.data();

        int ret = inflateInit2(&strm, -15);
        if (ret != Z_OK) {
          printf("failed to init source inflation: %d\n", ret);
          return -1;
        }

        // Because we've provided enough room to accommodate the output
        // data, we expect one call to inflate() to suffice.
        ret = inflate(&strm, Z_SYNC_FLUSH);
        if (ret != Z_STREAM_END) {
          printf("source inflation returned %d\n", ret);
          return -1;
        }
        // We should have filled the output buffer exactly, except
        // for the bonus_size.
        if (strm.avail_out != bonus_size) {
          printf("source inflation short by %zu bytes\n", strm.avail_out - bonus_size);
          return -1;
        }
        inflateEnd(&strm);

        if (bonus_size) {
          memcpy(expanded_source.data() + (expanded_len - bonus_size), bonus_data->data.data(),
                 bonus_size);
        }
      }

      if (!ApplyBSDiffPatchAndStreamOutput(expanded_source.data(), expanded_len, patch,
                                           patch_offset, deflate_header, sink)) {
        LOG(ERROR) << "Fail to apply streaming bspatch.";
        return -1;
      }

      LOG(DEBUG) << "Processed chunk type deflate";
    } else {
      printf("patch chunk %d is unknown type %d\n", i, type);
      return -1;
    }
  }

  return 0;
}
