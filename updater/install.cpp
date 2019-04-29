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

#include "updater/install.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/capability.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include <memory>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parsedouble.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <applypatch/applypatch.h>
#include <bootloader_message/bootloader_message.h>
#include <ext4_utils/wipe.h>
#include <openssl/sha.h>
#include <selinux/label.h>
#include <selinux/selinux.h>
#include <tune2fs.h>
#include <ziparchive/zip_archive.h>

#include "edify/expr.h"
#include "otautil/dirutil.h"
#include "otautil/error_code.h"
#include "otautil/mounts.h"
#include "otautil/print_sha1.h"
#include "otautil/sysutil.h"
#include "updater/updater.h"

// Send over the buffer to recovery though the command pipe.
static void uiPrint(State* state, const std::string& buffer) {
  UpdaterInfo* ui = static_cast<UpdaterInfo*>(state->cookie);

  // "line1\nline2\n" will be split into 3 tokens: "line1", "line2" and "".
  // So skip sending empty strings to UI.
  std::vector<std::string> lines = android::base::Split(buffer, "\n");
  for (auto& line : lines) {
    if (!line.empty()) {
      fprintf(ui->cmd_pipe, "ui_print %s\n", line.c_str());
    }
  }

  // On the updater side, we need to dump the contents to stderr (which has
  // been redirected to the log file). Because the recovery will only print
  // the contents to screen when processing pipe command ui_print.
  LOG(INFO) << buffer;
}

void uiPrintf(State* _Nonnull state, const char* _Nonnull format, ...) {
  std::string error_msg;

  va_list ap;
  va_start(ap, format);
  android::base::StringAppendV(&error_msg, format, ap);
  va_end(ap);

  uiPrint(state, error_msg);
}

// This is the updater side handler for ui_print() in edify script. Contents will be sent over to
// the recovery side for on-screen display.
Value* UIPrintFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s(): Failed to parse the argument(s)", name);
  }

  std::string buffer = android::base::Join(args, "");
  uiPrint(state, buffer);
  return StringValue(buffer);
}

// package_extract_file(package_file[, dest_file])
//   Extracts a single package_file from the update package and writes it to dest_file,
//   overwriting existing files if necessary. Without the dest_file argument, returns the
//   contents of the package file as a binary blob.
Value* PackageExtractFileFn(const char* name, State* state,
                            const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() < 1 || argv.size() > 2) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 1 or 2 args, got %zu", name,
                      argv.size());
  }

  if (argv.size() == 2) {
    // The two-argument version extracts to a file.

    std::vector<std::string> args;
    if (!ReadArgs(state, argv, &args)) {
      return ErrorAbort(state, kArgsParsingFailure, "%s() Failed to parse %zu args", name,
                        argv.size());
    }
    const std::string& zip_path = args[0];
    const std::string& dest_path = args[1];

    ZipArchiveHandle za = static_cast<UpdaterInfo*>(state->cookie)->package_zip;
    ZipString zip_string_path(zip_path.c_str());
    ZipEntry entry;
    if (FindEntry(za, zip_string_path, &entry) != 0) {
      LOG(ERROR) << name << ": no " << zip_path << " in package";
      return StringValue("");
    }

    android::base::unique_fd fd(TEMP_FAILURE_RETRY(
        open(dest_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR)));
    if (fd == -1) {
      PLOG(ERROR) << name << ": can't open " << dest_path << " for write";
      return StringValue("");
    }

    bool success = true;
    int32_t ret = ExtractEntryToFile(za, &entry, fd);
    if (ret != 0) {
      LOG(ERROR) << name << ": Failed to extract entry \"" << zip_path << "\" ("
                 << entry.uncompressed_length << " bytes) to \"" << dest_path
                 << "\": " << ErrorCodeString(ret);
      success = false;
    }
    if (fsync(fd) == -1) {
      PLOG(ERROR) << "fsync of \"" << dest_path << "\" failed";
      success = false;
    }

    if (close(fd.release()) != 0) {
      PLOG(ERROR) << "close of \"" << dest_path << "\" failed";
      success = false;
    }

    return StringValue(success ? "t" : "");
  } else {
    // The one-argument version returns the contents of the file as the result.

    std::vector<std::string> args;
    if (!ReadArgs(state, argv, &args)) {
      return ErrorAbort(state, kArgsParsingFailure, "%s() Failed to parse %zu args", name,
                        argv.size());
    }
    const std::string& zip_path = args[0];

    ZipArchiveHandle za = static_cast<UpdaterInfo*>(state->cookie)->package_zip;
    ZipString zip_string_path(zip_path.c_str());
    ZipEntry entry;
    if (FindEntry(za, zip_string_path, &entry) != 0) {
      return ErrorAbort(state, kPackageExtractFileFailure, "%s(): no %s in package", name,
                        zip_path.c_str());
    }

    std::string buffer;
    buffer.resize(entry.uncompressed_length);

    int32_t ret =
        ExtractToMemory(za, &entry, reinterpret_cast<uint8_t*>(&buffer[0]), buffer.size());
    if (ret != 0) {
      return ErrorAbort(state, kPackageExtractFileFailure,
                        "%s: Failed to extract entry \"%s\" (%zu bytes) to memory: %s", name,
                        zip_path.c_str(), buffer.size(), ErrorCodeString(ret));
    }

    return new Value(Value::Type::BLOB, buffer);
  }
}

// patch_partition_check(target_partition, source_partition)
//   Checks if the target and source partitions have the desired checksums to be patched. It returns
//   directly, if the target partition already has the expected checksum. Otherwise it in turn
//   checks the integrity of the source partition and the backup file on /cache.
//
// For example, patch_partition_check(
//     "EMMC:/dev/block/boot:12342568:8aaacf187a6929d0e9c3e9e46ea7ff495b43424d",
//     "EMMC:/dev/block/boot:12363048:06b0b16299dcefc94900efed01e0763ff644ffa4")
Value* PatchPartitionCheckFn(const char* name, State* state,
                             const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 2) {
    return ErrorAbort(state, kArgsParsingFailure,
                      "%s(): Invalid number of args (expected 2, got %zu)", name, argv.size());
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args, 0, 2)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s(): Failed to parse the argument(s)", name);
  }

  std::string err;
  auto target = Partition::Parse(args[0], &err);
  if (!target) {
    return ErrorAbort(state, kArgsParsingFailure, "%s(): Failed to parse target \"%s\": %s", name,
                      args[0].c_str(), err.c_str());
  }

  auto source = Partition::Parse(args[1], &err);
  if (!source) {
    return ErrorAbort(state, kArgsParsingFailure, "%s(): Failed to parse source \"%s\": %s", name,
                      args[1].c_str(), err.c_str());
  }

  bool result = PatchPartitionCheck(target, source);
  return StringValue(result ? "t" : "");
}

// patch_partition(target, source, patch)
//   Applies the given patch to the source partition, and writes the result to the target partition.
//
// For example, patch_partition(
//     "EMMC:/dev/block/boot:12342568:8aaacf187a6929d0e9c3e9e46ea7ff495b43424d",
//     "EMMC:/dev/block/boot:12363048:06b0b16299dcefc94900efed01e0763ff644ffa4",
//     package_extract_file("boot.img.p"))
Value* PatchPartitionFn(const char* name, State* state,
                        const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 3) {
    return ErrorAbort(state, kArgsParsingFailure,
                      "%s(): Invalid number of args (expected 3, got %zu)", name, argv.size());
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args, 0, 2)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s(): Failed to parse the argument(s)", name);
  }

  std::string err;
  auto target = Partition::Parse(args[0], &err);
  if (!target) {
    return ErrorAbort(state, kArgsParsingFailure, "%s(): Failed to parse target \"%s\": %s", name,
                      args[0].c_str(), err.c_str());
  }

  auto source = Partition::Parse(args[1], &err);
  if (!source) {
    return ErrorAbort(state, kArgsParsingFailure, "%s(): Failed to parse source \"%s\": %s", name,
                      args[1].c_str(), err.c_str());
  }

  std::vector<std::unique_ptr<Value>> values;
  if (!ReadValueArgs(state, argv, &values, 2, 1) || values[0]->type != Value::Type::BLOB) {
    return ErrorAbort(state, kArgsParsingFailure, "%s(): Invalid patch arg", name);
  }

  bool result = PatchPartition(target, source, *values[0], nullptr);
  return StringValue(result ? "t" : "");
}

// mount(fs_type, partition_type, location, mount_point)
// mount(fs_type, partition_type, location, mount_point, mount_options)

//    fs_type="ext4"   partition_type="EMMC"    location=device
Value* MountFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 4 && argv.size() != 5) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 4-5 args, got %zu", name,
                      argv.size());
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() Failed to parse the argument(s)", name);
  }
  const std::string& fs_type = args[0];
  const std::string& partition_type = args[1];
  const std::string& location = args[2];
  const std::string& mount_point = args[3];
  std::string mount_options;

  if (argv.size() == 5) {
    mount_options = args[4];
  }

  if (fs_type.empty()) {
    return ErrorAbort(state, kArgsParsingFailure, "fs_type argument to %s() can't be empty", name);
  }
  if (partition_type.empty()) {
    return ErrorAbort(state, kArgsParsingFailure, "partition_type argument to %s() can't be empty",
                      name);
  }
  if (location.empty()) {
    return ErrorAbort(state, kArgsParsingFailure, "location argument to %s() can't be empty", name);
  }
  if (mount_point.empty()) {
    return ErrorAbort(state, kArgsParsingFailure, "mount_point argument to %s() can't be empty",
                      name);
  }

  {
    char* secontext = nullptr;

    if (sehandle) {
      selabel_lookup(sehandle, &secontext, mount_point.c_str(), 0755);
      setfscreatecon(secontext);
    }

    mkdir(mount_point.c_str(), 0755);

    if (secontext) {
      freecon(secontext);
      setfscreatecon(nullptr);
    }
  }

  if (mount(location.c_str(), mount_point.c_str(), fs_type.c_str(),
            MS_NOATIME | MS_NODEV | MS_NODIRATIME, mount_options.c_str()) < 0) {
    uiPrintf(state, "%s: Failed to mount %s at %s: %s", name, location.c_str(), mount_point.c_str(),
             strerror(errno));
    return StringValue("");
  }

  return StringValue(mount_point);
}

// is_mounted(mount_point)
Value* IsMountedFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 1) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 1 arg, got %zu", name, argv.size());
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() Failed to parse the argument(s)", name);
  }
  const std::string& mount_point = args[0];
  if (mount_point.empty()) {
    return ErrorAbort(state, kArgsParsingFailure,
                      "mount_point argument to unmount() can't be empty");
  }

  scan_mounted_volumes();
  MountedVolume* vol = find_mounted_volume_by_mount_point(mount_point.c_str());
  if (vol == nullptr) {
    return StringValue("");
  }

  return StringValue(mount_point);
}

Value* UnmountFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 1) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 1 arg, got %zu", name, argv.size());
  }
  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() Failed to parse the argument(s)", name);
  }
  const std::string& mount_point = args[0];
  if (mount_point.empty()) {
    return ErrorAbort(state, kArgsParsingFailure,
                      "mount_point argument to unmount() can't be empty");
  }

  scan_mounted_volumes();
  MountedVolume* vol = find_mounted_volume_by_mount_point(mount_point.c_str());
  if (vol == nullptr) {
    uiPrintf(state, "Failed to unmount %s: No such volume", mount_point.c_str());
    return nullptr;
  } else {
    int ret = unmount_mounted_volume(vol);
    if (ret != 0) {
      uiPrintf(state, "Failed to unmount %s: %s", mount_point.c_str(), strerror(errno));
    }
  }

  return StringValue(mount_point);
}

static int exec_cmd(const std::vector<std::string>& args) {
  CHECK(!args.empty());
  auto argv = StringVectorToNullTerminatedArray(args);

  pid_t child;
  if ((child = vfork()) == 0) {
    execv(argv[0], argv.data());
    _exit(EXIT_FAILURE);
  }

  int status;
  waitpid(child, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    LOG(ERROR) << args[0] << " failed with status " << WEXITSTATUS(status);
  }
  return WEXITSTATUS(status);
}

// format(fs_type, partition_type, location, fs_size, mount_point)
//
//    fs_type="ext4"  partition_type="EMMC"  location=device  fs_size=<bytes> mount_point=<location>
//    fs_type="f2fs"  partition_type="EMMC"  location=device  fs_size=<bytes> mount_point=<location>
//    if fs_size == 0, then make fs uses the entire partition.
//    if fs_size > 0, that is the size to use
//    if fs_size < 0, then reserve that many bytes at the end of the partition (not for "f2fs")
Value* FormatFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 5) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 5 args, got %zu", name,
                      argv.size());
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() Failed to parse the argument(s)", name);
  }
  const std::string& fs_type = args[0];
  const std::string& partition_type = args[1];
  const std::string& location = args[2];
  const std::string& fs_size = args[3];
  const std::string& mount_point = args[4];

  if (fs_type.empty()) {
    return ErrorAbort(state, kArgsParsingFailure, "fs_type argument to %s() can't be empty", name);
  }
  if (partition_type.empty()) {
    return ErrorAbort(state, kArgsParsingFailure, "partition_type argument to %s() can't be empty",
                      name);
  }
  if (location.empty()) {
    return ErrorAbort(state, kArgsParsingFailure, "location argument to %s() can't be empty", name);
  }
  if (mount_point.empty()) {
    return ErrorAbort(state, kArgsParsingFailure, "mount_point argument to %s() can't be empty",
                      name);
  }

  int64_t size;
  if (!android::base::ParseInt(fs_size, &size)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s: failed to parse int in %s", name,
                      fs_size.c_str());
  }

  if (fs_type == "ext4") {
    std::vector<std::string> mke2fs_args = {
      "/system/bin/mke2fs", "-t", "ext4", "-b", "4096", location
    };
    if (size != 0) {
      mke2fs_args.push_back(std::to_string(size / 4096LL));
    }

    if (auto status = exec_cmd(mke2fs_args); status != 0) {
      LOG(ERROR) << name << ": mke2fs failed (" << status << ") on " << location;
      return StringValue("");
    }

    if (auto status = exec_cmd({ "/system/bin/e2fsdroid", "-e", "-a", mount_point, location });
        status != 0) {
      LOG(ERROR) << name << ": e2fsdroid failed (" << status << ") on " << location;
      return StringValue("");
    }
    return StringValue(location);
  }

  if (fs_type == "f2fs") {
    if (size < 0) {
      LOG(ERROR) << name << ": fs_size can't be negative for f2fs: " << fs_size;
      return StringValue("");
    }
    std::vector<std::string> f2fs_args = {
      "/system/bin/make_f2fs", "-g", "android", "-w", "512", location
    };
    if (size >= 512) {
      f2fs_args.push_back(std::to_string(size / 512));
    }
    if (auto status = exec_cmd(f2fs_args); status != 0) {
      LOG(ERROR) << name << ": make_f2fs failed (" << status << ") on " << location;
      return StringValue("");
    }

    if (auto status = exec_cmd({ "/system/bin/sload_f2fs", "-t", mount_point, location });
        status != 0) {
      LOG(ERROR) << name << ": sload_f2fs failed (" << status << ") on " << location;
      return StringValue("");
    }

    return StringValue(location);
  }

  LOG(ERROR) << name << ": unsupported fs_type \"" << fs_type << "\" partition_type \""
             << partition_type << "\"";
  return nullptr;
}

Value* ShowProgressFn(const char* name, State* state,
                      const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 2) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 2 args, got %zu", name,
                      argv.size());
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() Failed to parse the argument(s)", name);
  }
  const std::string& frac_str = args[0];
  const std::string& sec_str = args[1];

  double frac;
  if (!android::base::ParseDouble(frac_str.c_str(), &frac)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s: failed to parse double in %s", name,
                      frac_str.c_str());
  }
  int sec;
  if (!android::base::ParseInt(sec_str.c_str(), &sec)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s: failed to parse int in %s", name,
                      sec_str.c_str());
  }

  UpdaterInfo* ui = static_cast<UpdaterInfo*>(state->cookie);
  fprintf(ui->cmd_pipe, "progress %f %d\n", frac, sec);

  return StringValue(frac_str);
}

Value* SetProgressFn(const char* name, State* state,
                     const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 1) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 1 arg, got %zu", name, argv.size());
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() Failed to parse the argument(s)", name);
  }
  const std::string& frac_str = args[0];

  double frac;
  if (!android::base::ParseDouble(frac_str.c_str(), &frac)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s: failed to parse double in %s", name,
                      frac_str.c_str());
  }

  UpdaterInfo* ui = static_cast<UpdaterInfo*>(state->cookie);
  fprintf(ui->cmd_pipe, "set_progress %f\n", frac);

  return StringValue(frac_str);
}

Value* GetPropFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 1) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 1 arg, got %zu", name, argv.size());
  }
  std::string key;
  if (!Evaluate(state, argv[0], &key)) {
    return nullptr;
  }
  std::string value = android::base::GetProperty(key, "");

  return StringValue(value);
}

// file_getprop(file, key)
//
//   interprets 'file' as a getprop-style file (key=value pairs, one
//   per line. # comment lines, blank lines, lines without '=' ignored),
//   and returns the value for 'key' (or "" if it isn't defined).
Value* FileGetPropFn(const char* name, State* state,
                     const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 2) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 2 args, got %zu", name,
                      argv.size());
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() Failed to parse the argument(s)", name);
  }
  const std::string& filename = args[0];
  const std::string& key = args[1];

  std::string buffer;
  if (!android::base::ReadFileToString(filename, &buffer)) {
    ErrorAbort(state, kFreadFailure, "%s: failed to read %s", name, filename.c_str());
    return nullptr;
  }

  std::vector<std::string> lines = android::base::Split(buffer, "\n");
  for (size_t i = 0; i < lines.size(); i++) {
    std::string line = android::base::Trim(lines[i]);

    // comment or blank line: skip to next line
    if (line.empty() || line[0] == '#') {
      continue;
    }
    size_t equal_pos = line.find('=');
    if (equal_pos == std::string::npos) {
      continue;
    }

    // trim whitespace between key and '='
    std::string str = android::base::Trim(line.substr(0, equal_pos));

    // not the key we're looking for
    if (key != str) continue;

    return StringValue(android::base::Trim(line.substr(equal_pos + 1)));
  }

  return StringValue("");
}

// apply_patch_space(bytes)
Value* ApplyPatchSpaceFn(const char* name, State* state,
                         const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 1) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 1 args, got %zu", name,
                      argv.size());
  }
  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() Failed to parse the argument(s)", name);
  }
  const std::string& bytes_str = args[0];

  size_t bytes;
  if (!android::base::ParseUint(bytes_str.c_str(), &bytes)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s(): can't parse \"%s\" as byte count", name,
                      bytes_str.c_str());
  }

  // Skip the cache size check if the update is a retry.
  if (state->is_retry || CheckAndFreeSpaceOnCache(bytes)) {
    return StringValue("t");
  }
  return StringValue("");
}

Value* WipeCacheFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (!argv.empty()) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects no args, got %zu", name,
                      argv.size());
  }
  fprintf(static_cast<UpdaterInfo*>(state->cookie)->cmd_pipe, "wipe_cache\n");
  return StringValue("t");
}

Value* RunProgramFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() < 1) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects at least 1 arg", name);
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() Failed to parse the argument(s)", name);
  }

  auto exec_args = StringVectorToNullTerminatedArray(args);
  LOG(INFO) << "about to run program [" << exec_args[0] << "] with " << argv.size() << " args";

  pid_t child = fork();
  if (child == 0) {
    execv(exec_args[0], exec_args.data());
    PLOG(ERROR) << "run_program: execv failed";
    _exit(EXIT_FAILURE);
  }

  int status;
  waitpid(child, &status, 0);
  if (WIFEXITED(status)) {
    if (WEXITSTATUS(status) != 0) {
      LOG(ERROR) << "run_program: child exited with status " << WEXITSTATUS(status);
    }
  } else if (WIFSIGNALED(status)) {
    LOG(ERROR) << "run_program: child terminated by signal " << WTERMSIG(status);
  }

  return StringValue(std::to_string(status));
}

// read_file(filename)
//   Reads a local file 'filename' and returns its contents as a string Value.
Value* ReadFileFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 1) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 1 arg, got %zu", name, argv.size());
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s(): Failed to parse the argument(s)", name);
  }
  const std::string& filename = args[0];

  std::string contents;
  if (android::base::ReadFileToString(filename, &contents)) {
    return new Value(Value::Type::STRING, std::move(contents));
  }

  // Leave it to caller to handle the failure.
  PLOG(ERROR) << name << ": Failed to read " << filename;
  return StringValue("");
}

// write_value(value, filename)
//   Writes 'value' to 'filename'.
//   Example: write_value("960000", "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq")
Value* WriteValueFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 2) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 2 args, got %zu", name,
                      argv.size());
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s(): Failed to parse the argument(s)", name);
  }

  const std::string& filename = args[1];
  if (filename.empty()) {
    return ErrorAbort(state, kArgsParsingFailure, "%s(): Filename cannot be empty", name);
  }

  const std::string& value = args[0];
  if (!android::base::WriteStringToFile(value, filename)) {
    PLOG(ERROR) << name << ": Failed to write to \"" << filename << "\"";
    return StringValue("");
  } else {
    return StringValue("t");
  }
}

// Immediately reboot the device.  Recovery is not finished normally,
// so if you reboot into recovery it will re-start applying the
// current package (because nothing has cleared the copy of the
// arguments stored in the BCB).
//
// The argument is the partition name passed to the android reboot
// property.  It can be "recovery" to boot from the recovery
// partition, or "" (empty string) to boot from the regular boot
// partition.
Value* RebootNowFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 2) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 2 args, got %zu", name,
                      argv.size());
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s(): Failed to parse the argument(s)", name);
  }
  const std::string& filename = args[0];
  const std::string& property = args[1];

  // Zero out the 'command' field of the bootloader message. Leave the rest intact.
  bootloader_message boot;
  std::string err;
  if (!read_bootloader_message_from(&boot, filename, &err)) {
    LOG(ERROR) << name << "(): Failed to read from \"" << filename << "\": " << err;
    return StringValue("");
  }
  memset(boot.command, 0, sizeof(boot.command));
  if (!write_bootloader_message_to(boot, filename, &err)) {
    LOG(ERROR) << name << "(): Failed to write to \"" << filename << "\": " << err;
    return StringValue("");
  }

  Reboot(property);

  sleep(5);
  return ErrorAbort(state, kRebootFailure, "%s() failed to reboot", name);
}

// Store a string value somewhere that future invocations of recovery
// can access it.  This value is called the "stage" and can be used to
// drive packages that need to do reboots in the middle of
// installation and keep track of where they are in the multi-stage
// install.
//
// The first argument is the block device for the misc partition
// ("/misc" in the fstab), which is where this value is stored.  The
// second argument is the string to store; it should not exceed 31
// bytes.
Value* SetStageFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 2) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 2 args, got %zu", name,
                      argv.size());
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() Failed to parse the argument(s)", name);
  }
  const std::string& filename = args[0];
  const std::string& stagestr = args[1];

  // Store this value in the misc partition, immediately after the
  // bootloader message that the main recovery uses to save its
  // arguments in case of the device restarting midway through
  // package installation.
  bootloader_message boot;
  std::string err;
  if (!read_bootloader_message_from(&boot, filename, &err)) {
    LOG(ERROR) << name << "(): Failed to read from \"" << filename << "\": " << err;
    return StringValue("");
  }
  strlcpy(boot.stage, stagestr.c_str(), sizeof(boot.stage));
  if (!write_bootloader_message_to(boot, filename, &err)) {
    LOG(ERROR) << name << "(): Failed to write to \"" << filename << "\": " << err;
    return StringValue("");
  }

  return StringValue(filename);
}

// Return the value most recently saved with SetStageFn.  The argument
// is the block device for the misc partition.
Value* GetStageFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 1) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 1 arg, got %zu", name, argv.size());
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() Failed to parse the argument(s)", name);
  }
  const std::string& filename = args[0];

  bootloader_message boot;
  std::string err;
  if (!read_bootloader_message_from(&boot, filename, &err)) {
    LOG(ERROR) << name << "(): Failed to read from \"" << filename << "\": " << err;
    return StringValue("");
  }

  return StringValue(boot.stage);
}

Value* WipeBlockDeviceFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 2) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 2 args, got %zu", name,
                      argv.size());
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() Failed to parse the argument(s)", name);
  }
  const std::string& filename = args[0];
  const std::string& len_str = args[1];

  size_t len;
  if (!android::base::ParseUint(len_str.c_str(), &len)) {
    return nullptr;
  }
  android::base::unique_fd fd(open(filename.c_str(), O_WRONLY));
  if (fd == -1) {
    PLOG(ERROR) << "Failed to open " << filename;
    return StringValue("");
  }

  // The wipe_block_device function in ext4_utils returns 0 on success and 1
  // for failure.
  int status = wipe_block_device(fd, len);
  return StringValue((status == 0) ? "t" : "");
}

Value* EnableRebootFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (!argv.empty()) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects no args, got %zu", name,
                      argv.size());
  }
  UpdaterInfo* ui = static_cast<UpdaterInfo*>(state->cookie);
  fprintf(ui->cmd_pipe, "enable_reboot\n");
  return StringValue("t");
}

Value* Tune2FsFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.empty()) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects args, got %zu", name, argv.size());
  }

  std::vector<std::string> args;
  if (!ReadArgs(state, argv, &args)) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() could not read args", name);
  }

  // tune2fs expects the program name as its first arg.
  args.insert(args.begin(), "tune2fs");
  auto tune2fs_args = StringVectorToNullTerminatedArray(args);

  // tune2fs changes the filesystem parameters on an ext2 filesystem; it returns 0 on success.
  if (auto result = tune2fs_main(tune2fs_args.size() - 1, tune2fs_args.data()); result != 0) {
    return ErrorAbort(state, kTune2FsFailure, "%s() returned error code %d", name, result);
  }
  return StringValue("t");
}

void RegisterInstallFunctions() {
  RegisterFunction("mount", MountFn);
  RegisterFunction("is_mounted", IsMountedFn);
  RegisterFunction("unmount", UnmountFn);
  RegisterFunction("format", FormatFn);
  RegisterFunction("show_progress", ShowProgressFn);
  RegisterFunction("set_progress", SetProgressFn);
  RegisterFunction("package_extract_file", PackageExtractFileFn);

  RegisterFunction("getprop", GetPropFn);
  RegisterFunction("file_getprop", FileGetPropFn);

  RegisterFunction("apply_patch_space", ApplyPatchSpaceFn);
  RegisterFunction("patch_partition", PatchPartitionFn);
  RegisterFunction("patch_partition_check", PatchPartitionCheckFn);

  RegisterFunction("wipe_block_device", WipeBlockDeviceFn);

  RegisterFunction("read_file", ReadFileFn);
  RegisterFunction("write_value", WriteValueFn);

  RegisterFunction("wipe_cache", WipeCacheFn);

  RegisterFunction("ui_print", UIPrintFn);

  RegisterFunction("run_program", RunProgramFn);

  RegisterFunction("reboot_now", RebootNowFn);
  RegisterFunction("get_stage", GetStageFn);
  RegisterFunction("set_stage", SetStageFn);

  RegisterFunction("enable_reboot", EnableRebootFn);
  RegisterFunction("tune2fs", Tune2FsFn);
}
