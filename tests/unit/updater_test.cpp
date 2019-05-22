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

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <bootloader_message/bootloader_message.h>
#include <brotli/encode.h>
#include <bsdiff/bsdiff.h>
#include <gtest/gtest.h>
#include <verity/hash_tree_builder.h>
#include <ziparchive/zip_archive.h>
#include <ziparchive/zip_writer.h>

#include "applypatch/applypatch.h"
#include "common/test_constants.h"
#include "edify/expr.h"
#include "otautil/error_code.h"
#include "otautil/paths.h"
#include "otautil/print_sha1.h"
#include "otautil/sysutil.h"
#include "private/commands.h"
#include "updater/blockimg.h"
#include "updater/install.h"
#include "updater/updater.h"
#include "updater/updater_runtime.h"

using namespace std::string_literals;

using PackageEntries = std::unordered_map<std::string, std::string>;

static void expect(const char* expected, const std::string& expr_str, CauseCode cause_code,
                   Updater* updater) {
  std::unique_ptr<Expr> e;
  int error_count = 0;
  ASSERT_EQ(0, ParseString(expr_str, &e, &error_count));
  ASSERT_EQ(0, error_count);

  State state(expr_str, updater);

  std::string result;
  bool status = Evaluate(&state, e, &result);

  if (expected == nullptr) {
    ASSERT_FALSE(status);
  } else {
    ASSERT_TRUE(status) << "Evaluate() finished with error message: " << state.errmsg;
    ASSERT_STREQ(expected, result.c_str());
  }

  // Error code is set in updater/updater.cpp only, by parsing State.errmsg.
  ASSERT_EQ(kNoError, state.error_code);

  // Cause code should always be available.
  ASSERT_EQ(cause_code, state.cause_code);
}

static void expect(const char* expected, const std::string& expr_str, CauseCode cause_code) {
  Updater updater(std::make_unique<UpdaterRuntime>(nullptr));
  expect(expected, expr_str, cause_code, &updater);
}

static void BuildUpdatePackage(const PackageEntries& entries, int fd) {
  FILE* zip_file_ptr = fdopen(fd, "wb");
  ZipWriter zip_writer(zip_file_ptr);

  for (const auto& entry : entries) {
    // All the entries are written as STORED.
    ASSERT_EQ(0, zip_writer.StartEntry(entry.first.c_str(), 0));
    if (!entry.second.empty()) {
      ASSERT_EQ(0, zip_writer.WriteBytes(entry.second.data(), entry.second.size()));
    }
    ASSERT_EQ(0, zip_writer.FinishEntry());
  }

  ASSERT_EQ(0, zip_writer.Finish());
  ASSERT_EQ(0, fclose(zip_file_ptr));
}

static std::string GetSha1(std::string_view content) {
  uint8_t digest[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const uint8_t*>(content.data()), content.size(), digest);
  return print_sha1(digest);
}

static Value* BlobToString(const char* name, State* state,
                           const std::vector<std::unique_ptr<Expr>>& argv) {
  if (argv.size() != 1) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects 1 arg, got %zu", name, argv.size());
  }

  std::vector<std::unique_ptr<Value>> args;
  if (!ReadValueArgs(state, argv, &args)) {
    return nullptr;
  }

  if (args[0]->type != Value::Type::BLOB) {
    return ErrorAbort(state, kArgsParsingFailure, "%s() expects a BLOB argument", name);
  }

  args[0]->type = Value::Type::STRING;
  return args[0].release();
}

class UpdaterTestBase {
 protected:
  UpdaterTestBase() : updater_(std::make_unique<UpdaterRuntime>(nullptr)) {}

  void SetUp() {
    RegisterBuiltins();
    RegisterInstallFunctions();
    RegisterBlockImageFunctions();

    // Each test is run in a separate process (isolated mode). Shared temporary files won't cause
    // conflicts.
    Paths::Get().set_cache_temp_source(temp_saved_source_.path);
    Paths::Get().set_last_command_file(temp_last_command_.path);
    Paths::Get().set_stash_directory_base(temp_stash_base_.path);

    last_command_file_ = temp_last_command_.path;
    image_file_ = image_temp_file_.path;
  }

  void TearDown() {
    // Clean up the last_command_file if any.
    ASSERT_TRUE(android::base::RemoveFileIfExists(last_command_file_));

    // Clear partition updated marker if any.
    std::string updated_marker{ temp_stash_base_.path };
    updated_marker += "/" + GetSha1(image_temp_file_.path) + ".UPDATED";
    ASSERT_TRUE(android::base::RemoveFileIfExists(updated_marker));
  }

  void RunBlockImageUpdate(bool is_verify, PackageEntries entries, const std::string& image_file,
                           const std::string& result, CauseCode cause_code = kNoCause) {
    CHECK(entries.find("transfer_list") != entries.end());
    std::string new_data =
        entries.find("new_data.br") != entries.end() ? "new_data.br" : "new_data";
    std::string script = is_verify ? "block_image_verify" : "block_image_update";
    script += R"((")" + image_file + R"(", package_extract_file("transfer_list"), ")" + new_data +
              R"(", "patch_data"))";
    entries.emplace(Updater::SCRIPT_NAME, script);

    // Build the update package.
    TemporaryFile zip_file;
    BuildUpdatePackage(entries, zip_file.release());

    // Set up the handler, command_pipe, patch offset & length.
    TemporaryFile temp_pipe;
    ASSERT_TRUE(updater_.Init(temp_pipe.release(), zip_file.path, false));
    ASSERT_TRUE(updater_.RunUpdate());
    ASSERT_EQ(result, updater_.GetResult());

    // Parse the cause code written to the command pipe.
    int received_cause_code = kNoCause;
    std::string pipe_content;
    ASSERT_TRUE(android::base::ReadFileToString(temp_pipe.path, &pipe_content));
    auto lines = android::base::Split(pipe_content, "\n");
    for (std::string_view line : lines) {
      if (android::base::ConsumePrefix(&line, "log cause: ")) {
        ASSERT_TRUE(android::base::ParseInt(line.data(), &received_cause_code));
      }
    }
    ASSERT_EQ(cause_code, received_cause_code);
  }

  TemporaryFile temp_saved_source_;
  TemporaryDir temp_stash_base_;
  std::string last_command_file_;
  std::string image_file_;

  Updater updater_;

 private:
  TemporaryFile temp_last_command_;
  TemporaryFile image_temp_file_;
};

class UpdaterTest : public UpdaterTestBase, public ::testing::Test {
 protected:
  void SetUp() override {
    UpdaterTestBase::SetUp();

    RegisterFunction("blob_to_string", BlobToString);
    // Enable a special command "abort" to simulate interruption.
    Command::abort_allowed_ = true;
  }

  void TearDown() override {
    UpdaterTestBase::TearDown();
  }

  void SetUpdaterCmdPipe(int fd) {
    FILE* cmd_pipe = fdopen(fd, "w");
    ASSERT_NE(nullptr, cmd_pipe);
    updater_.cmd_pipe_.reset(cmd_pipe);
  }

  void SetUpdaterOtaPackageHandle(ZipArchiveHandle handle) {
    updater_.package_handle_ = handle;
  }

  void FlushUpdaterCommandPipe() const {
    fflush(updater_.cmd_pipe_.get());
  }
};

TEST_F(UpdaterTest, getprop) {
    expect(android::base::GetProperty("ro.product.device", "").c_str(),
           "getprop(\"ro.product.device\")",
           kNoCause);

    expect(android::base::GetProperty("ro.build.fingerprint", "").c_str(),
           "getprop(\"ro.build.fingerprint\")",
           kNoCause);

    // getprop() accepts only one parameter.
    expect(nullptr, "getprop()", kArgsParsingFailure);
    expect(nullptr, "getprop(\"arg1\", \"arg2\")", kArgsParsingFailure);
}

TEST_F(UpdaterTest, patch_partition_check) {
  // Zero argument is not valid.
  expect(nullptr, "patch_partition_check()", kArgsParsingFailure);

  std::string source_file = from_testdata_base("boot.img");
  std::string source_content;
  ASSERT_TRUE(android::base::ReadFileToString(source_file, &source_content));
  size_t source_size = source_content.size();
  std::string source_hash = GetSha1(source_content);
  Partition source(source_file, source_size, source_hash);

  std::string target_file = from_testdata_base("recovery.img");
  std::string target_content;
  ASSERT_TRUE(android::base::ReadFileToString(target_file, &target_content));
  size_t target_size = target_content.size();
  std::string target_hash = GetSha1(target_content);
  Partition target(target_file, target_size, target_hash);

  // One argument is not valid.
  expect(nullptr, "patch_partition_check(\"" + source.ToString() + "\")", kArgsParsingFailure);
  expect(nullptr, "patch_partition_check(\"" + target.ToString() + "\")", kArgsParsingFailure);

  // Both of the source and target have the desired checksum.
  std::string cmd =
      "patch_partition_check(\"" + source.ToString() + "\", \"" + target.ToString() + "\")";
  expect("t", cmd, kNoCause);

  // Only source partition has the desired checksum.
  Partition bad_target(target_file, target_size - 1, target_hash);
  cmd = "patch_partition_check(\"" + source.ToString() + "\", \"" + bad_target.ToString() + "\")";
  expect("t", cmd, kNoCause);

  // Only target partition has the desired checksum.
  Partition bad_source(source_file, source_size + 1, source_hash);
  cmd = "patch_partition_check(\"" + bad_source.ToString() + "\", \"" + target.ToString() + "\")";
  expect("t", cmd, kNoCause);

  // Neither of the source or target has the desired checksum.
  cmd =
      "patch_partition_check(\"" + bad_source.ToString() + "\", \"" + bad_target.ToString() + "\")";
  expect("", cmd, kNoCause);
}

TEST_F(UpdaterTest, file_getprop) {
    // file_getprop() expects two arguments.
    expect(nullptr, "file_getprop()", kArgsParsingFailure);
    expect(nullptr, "file_getprop(\"arg1\")", kArgsParsingFailure);
    expect(nullptr, "file_getprop(\"arg1\", \"arg2\", \"arg3\")", kArgsParsingFailure);

    // File doesn't exist.
    expect(nullptr, "file_getprop(\"/doesntexist\", \"key1\")", kFreadFailure);

    // Reject too large files (current limit = 65536).
    TemporaryFile temp_file1;
    std::string buffer(65540, '\0');
    ASSERT_TRUE(android::base::WriteStringToFile(buffer, temp_file1.path));

    // Read some keys.
    TemporaryFile temp_file2;
    std::string content("ro.product.name=tardis\n"
                        "# comment\n\n\n"
                        "ro.product.model\n"
                        "ro.product.board =  magic \n");
    ASSERT_TRUE(android::base::WriteStringToFile(content, temp_file2.path));

    std::string script1("file_getprop(\"" + std::string(temp_file2.path) +
                       "\", \"ro.product.name\")");
    expect("tardis", script1, kNoCause);

    std::string script2("file_getprop(\"" + std::string(temp_file2.path) +
                       "\", \"ro.product.board\")");
    expect("magic", script2, kNoCause);

    // No match.
    std::string script3("file_getprop(\"" + std::string(temp_file2.path) +
                       "\", \"ro.product.wrong\")");
    expect("", script3, kNoCause);

    std::string script4("file_getprop(\"" + std::string(temp_file2.path) +
                       "\", \"ro.product.name=\")");
    expect("", script4, kNoCause);

    std::string script5("file_getprop(\"" + std::string(temp_file2.path) +
                       "\", \"ro.product.nam\")");
    expect("", script5, kNoCause);

    std::string script6("file_getprop(\"" + std::string(temp_file2.path) +
                       "\", \"ro.product.model\")");
    expect("", script6, kNoCause);
}

// TODO: Test extracting to block device.
TEST_F(UpdaterTest, package_extract_file) {
  // package_extract_file expects 1 or 2 arguments.
  expect(nullptr, "package_extract_file()", kArgsParsingFailure);
  expect(nullptr, "package_extract_file(\"arg1\", \"arg2\", \"arg3\")", kArgsParsingFailure);

  std::string zip_path = from_testdata_base("ziptest_valid.zip");
  ZipArchiveHandle handle;
  ASSERT_EQ(0, OpenArchive(zip_path.c_str(), &handle));

  // Need to set up the ziphandle.
  SetUpdaterOtaPackageHandle(handle);

  // Two-argument version.
  TemporaryFile temp_file1;
  std::string script("package_extract_file(\"a.txt\", \"" + std::string(temp_file1.path) + "\")");
  expect("t", script, kNoCause, &updater_);

  // Verify the extracted entry.
  std::string data;
  ASSERT_TRUE(android::base::ReadFileToString(temp_file1.path, &data));
  ASSERT_EQ(kATxtContents, data);

  // Now extract another entry to the same location, which should overwrite.
  script = "package_extract_file(\"b.txt\", \"" + std::string(temp_file1.path) + "\")";
  expect("t", script, kNoCause, &updater_);

  ASSERT_TRUE(android::base::ReadFileToString(temp_file1.path, &data));
  ASSERT_EQ(kBTxtContents, data);

  // Missing zip entry. The two-argument version doesn't abort.
  script = "package_extract_file(\"doesntexist\", \"" + std::string(temp_file1.path) + "\")";
  expect("", script, kNoCause, &updater_);

  // Extract to /dev/full should fail.
  script = "package_extract_file(\"a.txt\", \"/dev/full\")";
  expect("", script, kNoCause, &updater_);

  // One-argument version. package_extract_file() gives a VAL_BLOB, which needs to be converted to
  // VAL_STRING for equality test.
  script = "blob_to_string(package_extract_file(\"a.txt\")) == \"" + kATxtContents + "\"";
  expect("t", script, kNoCause, &updater_);

  script = "blob_to_string(package_extract_file(\"b.txt\")) == \"" + kBTxtContents + "\"";
  expect("t", script, kNoCause, &updater_);

  // Missing entry. The one-argument version aborts the evaluation.
  script = "package_extract_file(\"doesntexist\")";
  expect(nullptr, script, kPackageExtractFileFailure, &updater_);
}

TEST_F(UpdaterTest, read_file) {
  // read_file() expects one argument.
  expect(nullptr, "read_file()", kArgsParsingFailure);
  expect(nullptr, "read_file(\"arg1\", \"arg2\")", kArgsParsingFailure);

  // Write some value to file and read back.
  TemporaryFile temp_file;
  std::string script("write_value(\"foo\", \""s + temp_file.path + "\");");
  expect("t", script, kNoCause);

  script = "read_file(\""s + temp_file.path + "\") == \"foo\"";
  expect("t", script, kNoCause);

  script = "read_file(\""s + temp_file.path + "\") == \"bar\"";
  expect("", script, kNoCause);

  // It should fail gracefully when read fails.
  script = "read_file(\"/doesntexist\")";
  expect("", script, kNoCause);
}

TEST_F(UpdaterTest, compute_hash_tree_smoke) {
  std::string data;
  for (unsigned char i = 0; i < 128; i++) {
    data += std::string(4096, i);
  }
  // Appends an additional block for verity data.
  data += std::string(4096, 0);
  ASSERT_EQ(129 * 4096, data.size());
  ASSERT_TRUE(android::base::WriteStringToFile(data, image_file_));

  std::string salt = "aee087a5be3b982978c923f566a94613496b417f2af592639bc80d141e34dfe7";
  std::string expected_root_hash =
      "7e0a8d8747f54384014ab996f5b2dc4eb7ff00c630eede7134c9e3f05c0dd8ca";
  // hash_tree_ranges, source_ranges, hash_algorithm, salt_hex, root_hash
  std::vector<std::string> tokens{ "compute_hash_tree", "2,128,129", "2,0,128", "sha256", salt,
                                   expected_root_hash };
  std::string hash_tree_command = android::base::Join(tokens, " ");

  std::vector<std::string> transfer_list{
    "4", "2", "0", "2", hash_tree_command,
  };

  PackageEntries entries{
    { "new_data", "" },
    { "patch_data", "" },
    { "transfer_list", android::base::Join(transfer_list, "\n") },
  };

  RunBlockImageUpdate(false, entries, image_file_, "t");

  std::string updated;
  ASSERT_TRUE(android::base::ReadFileToString(image_file_, &updated));
  ASSERT_EQ(129 * 4096, updated.size());
  ASSERT_EQ(data.substr(0, 128 * 4096), updated.substr(0, 128 * 4096));

  // Computes the SHA256 of the salt + hash_tree_data and expects the result to match with the
  // root_hash.
  std::vector<unsigned char> salt_bytes;
  ASSERT_TRUE(HashTreeBuilder::ParseBytesArrayFromString(salt, &salt_bytes));
  std::vector<unsigned char> hash_tree = std::move(salt_bytes);
  hash_tree.insert(hash_tree.end(), updated.begin() + 128 * 4096, updated.end());

  std::vector<unsigned char> digest(SHA256_DIGEST_LENGTH);
  SHA256(hash_tree.data(), hash_tree.size(), digest.data());
  ASSERT_EQ(expected_root_hash, HashTreeBuilder::BytesArrayToString(digest));
}

TEST_F(UpdaterTest, compute_hash_tree_root_mismatch) {
  std::string data;
  for (size_t i = 0; i < 128; i++) {
    data += std::string(4096, i);
  }
  // Appends an additional block for verity data.
  data += std::string(4096, 0);
  ASSERT_EQ(129 * 4096, data.size());
  // Corrupts one bit
  data[4096] = 'A';
  ASSERT_TRUE(android::base::WriteStringToFile(data, image_file_));

  std::string salt = "aee087a5be3b982978c923f566a94613496b417f2af592639bc80d141e34dfe7";
  std::string expected_root_hash =
      "7e0a8d8747f54384014ab996f5b2dc4eb7ff00c630eede7134c9e3f05c0dd8ca";
  // hash_tree_ranges, source_ranges, hash_algorithm, salt_hex, root_hash
  std::vector<std::string> tokens{ "compute_hash_tree", "2,128,129", "2,0,128", "sha256", salt,
                                   expected_root_hash };
  std::string hash_tree_command = android::base::Join(tokens, " ");

  std::vector<std::string> transfer_list{
    "4", "2", "0", "2", hash_tree_command,
  };

  PackageEntries entries{
    { "new_data", "" },
    { "patch_data", "" },
    { "transfer_list", android::base::Join(transfer_list, "\n") },
  };

  RunBlockImageUpdate(false, entries, image_file_, "", kHashTreeComputationFailure);
}

TEST_F(UpdaterTest, write_value) {
  // write_value() expects two arguments.
  expect(nullptr, "write_value()", kArgsParsingFailure);
  expect(nullptr, "write_value(\"arg1\")", kArgsParsingFailure);
  expect(nullptr, "write_value(\"arg1\", \"arg2\", \"arg3\")", kArgsParsingFailure);

  // filename cannot be empty.
  expect(nullptr, "write_value(\"value\", \"\")", kArgsParsingFailure);

  // Write some value to file.
  TemporaryFile temp_file;
  std::string value = "magicvalue";
  std::string script("write_value(\"" + value + "\", \"" + std::string(temp_file.path) + "\")");
  expect("t", script, kNoCause);

  // Verify the content.
  std::string content;
  ASSERT_TRUE(android::base::ReadFileToString(temp_file.path, &content));
  ASSERT_EQ(value, content);

  // Allow writing empty string.
  script = "write_value(\"\", \"" + std::string(temp_file.path) + "\")";
  expect("t", script, kNoCause);

  // Verify the content.
  ASSERT_TRUE(android::base::ReadFileToString(temp_file.path, &content));
  ASSERT_EQ("", content);

  // It should fail gracefully when write fails.
  script = "write_value(\"value\", \"/proc/0/file1\")";
  expect("", script, kNoCause);
}

TEST_F(UpdaterTest, get_stage) {
  // get_stage() expects one argument.
  expect(nullptr, "get_stage()", kArgsParsingFailure);
  expect(nullptr, "get_stage(\"arg1\", \"arg2\")", kArgsParsingFailure);
  expect(nullptr, "get_stage(\"arg1\", \"arg2\", \"arg3\")", kArgsParsingFailure);

  // Set up a local file as BCB.
  TemporaryFile tf;
  std::string temp_file(tf.path);
  bootloader_message boot;
  strlcpy(boot.stage, "2/3", sizeof(boot.stage));
  std::string err;
  ASSERT_TRUE(write_bootloader_message_to(boot, temp_file, &err));

  // Can read the stage value.
  std::string script("get_stage(\"" + temp_file + "\")");
  expect("2/3", script, kNoCause);

  // Bad BCB path.
  script = "get_stage(\"doesntexist\")";
  expect("", script, kNoCause);
}

TEST_F(UpdaterTest, set_stage) {
  // set_stage() expects two arguments.
  expect(nullptr, "set_stage()", kArgsParsingFailure);
  expect(nullptr, "set_stage(\"arg1\")", kArgsParsingFailure);
  expect(nullptr, "set_stage(\"arg1\", \"arg2\", \"arg3\")", kArgsParsingFailure);

  // Set up a local file as BCB.
  TemporaryFile tf;
  std::string temp_file(tf.path);
  bootloader_message boot;
  strlcpy(boot.command, "command", sizeof(boot.command));
  strlcpy(boot.stage, "2/3", sizeof(boot.stage));
  std::string err;
  ASSERT_TRUE(write_bootloader_message_to(boot, temp_file, &err));

  // Write with set_stage().
  std::string script("set_stage(\"" + temp_file + "\", \"1/3\")");
  expect(tf.path, script, kNoCause);

  // Verify.
  bootloader_message boot_verify;
  ASSERT_TRUE(read_bootloader_message_from(&boot_verify, temp_file, &err));

  // Stage should be updated, with command part untouched.
  ASSERT_STREQ("1/3", boot_verify.stage);
  ASSERT_STREQ(boot.command, boot_verify.command);

  // Bad BCB path.
  script = "set_stage(\"doesntexist\", \"1/3\")";
  expect("", script, kNoCause);

  script = "set_stage(\"/dev/full\", \"1/3\")";
  expect("", script, kNoCause);
}

TEST_F(UpdaterTest, set_progress) {
  // set_progress() expects one argument.
  expect(nullptr, "set_progress()", kArgsParsingFailure);
  expect(nullptr, "set_progress(\"arg1\", \"arg2\")", kArgsParsingFailure);

  // Invalid progress argument.
  expect(nullptr, "set_progress(\"arg1\")", kArgsParsingFailure);
  expect(nullptr, "set_progress(\"3x+5\")", kArgsParsingFailure);
  expect(nullptr, "set_progress(\".3.5\")", kArgsParsingFailure);

  TemporaryFile tf;
  SetUpdaterCmdPipe(tf.release());
  expect(".52", "set_progress(\".52\")", kNoCause, &updater_);
  FlushUpdaterCommandPipe();

  std::string cmd;
  ASSERT_TRUE(android::base::ReadFileToString(tf.path, &cmd));
  ASSERT_EQ(android::base::StringPrintf("set_progress %f\n", .52), cmd);
  // recovery-updater protocol expects 2 tokens ("set_progress <frac>").
  ASSERT_EQ(2U, android::base::Split(cmd, " ").size());
}

TEST_F(UpdaterTest, show_progress) {
  // show_progress() expects two arguments.
  expect(nullptr, "show_progress()", kArgsParsingFailure);
  expect(nullptr, "show_progress(\"arg1\")", kArgsParsingFailure);
  expect(nullptr, "show_progress(\"arg1\", \"arg2\", \"arg3\")", kArgsParsingFailure);

  // Invalid progress arguments.
  expect(nullptr, "show_progress(\"arg1\", \"arg2\")", kArgsParsingFailure);
  expect(nullptr, "show_progress(\"3x+5\", \"10\")", kArgsParsingFailure);
  expect(nullptr, "show_progress(\".3\", \"5a\")", kArgsParsingFailure);

  TemporaryFile tf;
  SetUpdaterCmdPipe(tf.release());
  expect(".52", "show_progress(\".52\", \"10\")", kNoCause, &updater_);
  FlushUpdaterCommandPipe();

  std::string cmd;
  ASSERT_TRUE(android::base::ReadFileToString(tf.path, &cmd));
  ASSERT_EQ(android::base::StringPrintf("progress %f %d\n", .52, 10), cmd);
  // recovery-updater protocol expects 3 tokens ("progress <frac> <secs>").
  ASSERT_EQ(3U, android::base::Split(cmd, " ").size());
}

TEST_F(UpdaterTest, block_image_update_parsing_error) {
  std::vector<std::string> transfer_list{
    // clang-format off
    "4",
    "2",
    "0",
    // clang-format on
  };

  PackageEntries entries{
    { "new_data", "" },
    { "patch_data", "" },
    { "transfer_list", android::base::Join(transfer_list, '\n') },
  };

  RunBlockImageUpdate(false, entries, image_file_, "", kArgsParsingFailure);
}

// Generates the bsdiff of the given source and target images, and writes the result entries.
// target_blocks specifies the block count to be written into the `bsdiff` command, which may be
// different from the given target size in order to trigger overrun / underrun paths.
static void GetEntriesForBsdiff(std::string_view source, std::string_view target,
                                size_t target_blocks, PackageEntries* entries) {
  // Generate the patch data.
  TemporaryFile patch_file;
  ASSERT_EQ(0, bsdiff::bsdiff(reinterpret_cast<const uint8_t*>(source.data()), source.size(),
                              reinterpret_cast<const uint8_t*>(target.data()), target.size(),
                              patch_file.path, nullptr));
  std::string patch_content;
  ASSERT_TRUE(android::base::ReadFileToString(patch_file.path, &patch_content));

  // Create the transfer list that contains a bsdiff.
  std::string src_hash = GetSha1(source);
  std::string tgt_hash = GetSha1(target);
  size_t source_blocks = source.size() / 4096;
  std::vector<std::string> transfer_list{
    // clang-format off
    "4",
    std::to_string(target_blocks),
    "0",
    "0",
    // bsdiff patch_offset patch_length source_hash target_hash target_range source_block_count
    // source_range
    android::base::StringPrintf("bsdiff 0 %zu %s %s 2,0,%zu %zu 2,0,%zu", patch_content.size(),
                                src_hash.c_str(), tgt_hash.c_str(), target_blocks, source_blocks,
                                source_blocks),
    // clang-format on
  };

  *entries = {
    { "new_data", "" },
    { "patch_data", patch_content },
    { "transfer_list", android::base::Join(transfer_list, '\n') },
  };
}

TEST_F(UpdaterTest, block_image_update_patch_data) {
  // Both source and target images have 10 blocks.
  std::string source =
      std::string(4096, 'a') + std::string(4096, 'c') + std::string(4096 * 3, '\0');
  std::string target =
      std::string(4096, 'b') + std::string(4096, 'd') + std::string(4096 * 3, '\0');
  ASSERT_TRUE(android::base::WriteStringToFile(source, image_file_));

  PackageEntries entries;
  GetEntriesForBsdiff(std::string_view(source).substr(0, 4096 * 2),
                      std::string_view(target).substr(0, 4096 * 2), 2, &entries);
  RunBlockImageUpdate(false, entries, image_file_, "t");

  // The update_file should be patched correctly.
  std::string updated;
  ASSERT_TRUE(android::base::ReadFileToString(image_file_, &updated));
  ASSERT_EQ(target, updated);
}

TEST_F(UpdaterTest, block_image_update_patch_overrun) {
  // Both source and target images have 10 blocks.
  std::string source =
      std::string(4096, 'a') + std::string(4096, 'c') + std::string(4096 * 3, '\0');
  std::string target =
      std::string(4096, 'b') + std::string(4096, 'd') + std::string(4096 * 3, '\0');
  ASSERT_TRUE(android::base::WriteStringToFile(source, image_file_));

  // Provide one less block to trigger the overrun path.
  PackageEntries entries;
  GetEntriesForBsdiff(std::string_view(source).substr(0, 4096 * 2),
                      std::string_view(target).substr(0, 4096 * 2), 1, &entries);

  // The update should fail due to overrun.
  RunBlockImageUpdate(false, entries, image_file_, "", kPatchApplicationFailure);
}

TEST_F(UpdaterTest, block_image_update_patch_underrun) {
  // Both source and target images have 10 blocks.
  std::string source =
      std::string(4096, 'a') + std::string(4096, 'c') + std::string(4096 * 3, '\0');
  std::string target =
      std::string(4096, 'b') + std::string(4096, 'd') + std::string(4096 * 3, '\0');
  ASSERT_TRUE(android::base::WriteStringToFile(source, image_file_));

  // Provide one more block to trigger the overrun path.
  PackageEntries entries;
  GetEntriesForBsdiff(std::string_view(source).substr(0, 4096 * 2),
                      std::string_view(target).substr(0, 4096 * 2), 3, &entries);

  // The update should fail due to underrun.
  RunBlockImageUpdate(false, entries, image_file_, "", kPatchApplicationFailure);
}

TEST_F(UpdaterTest, block_image_update_fail) {
  std::string src_content(4096 * 2, 'e');
  std::string src_hash = GetSha1(src_content);
  // Stash and free some blocks, then fail the update intentionally.
  std::vector<std::string> transfer_list{
    // clang-format off
    "4",
    "2",
    "0",
    "2",
    "stash " + src_hash + " 2,0,2",
    "free " + src_hash,
    "abort",
    // clang-format on
  };

  // Add a new data of 10 bytes to test the deadlock.
  PackageEntries entries{
    { "new_data", std::string(10, 0) },
    { "patch_data", "" },
    { "transfer_list", android::base::Join(transfer_list, '\n') },
  };

  ASSERT_TRUE(android::base::WriteStringToFile(src_content, image_file_));

  RunBlockImageUpdate(false, entries, image_file_, "");

  // Updater generates the stash name based on the input file name.
  std::string name_digest = GetSha1(image_file_);
  std::string stash_base = std::string(temp_stash_base_.path) + "/" + name_digest;
  ASSERT_EQ(0, access(stash_base.c_str(), F_OK));
  // Expect the stashed blocks to be freed.
  ASSERT_EQ(-1, access((stash_base + src_hash).c_str(), F_OK));
  ASSERT_EQ(0, rmdir(stash_base.c_str()));
}

TEST_F(UpdaterTest, new_data_over_write) {
  std::vector<std::string> transfer_list{
    // clang-format off
    "4",
    "1",
    "0",
    "0",
    "new 2,0,1",
    // clang-format on
  };

  // Write 4096 + 100 bytes of new data.
  PackageEntries entries{
    { "new_data", std::string(4196, 0) },
    { "patch_data", "" },
    { "transfer_list", android::base::Join(transfer_list, '\n') },
  };

  RunBlockImageUpdate(false, entries, image_file_, "t");
}

TEST_F(UpdaterTest, new_data_short_write) {
  std::vector<std::string> transfer_list{
    // clang-format off
    "4",
    "1",
    "0",
    "0",
    "new 2,0,1",
    // clang-format on
  };

  PackageEntries entries{
    { "patch_data", "" },
    { "transfer_list", android::base::Join(transfer_list, '\n') },
  };

  // Updater should report the failure gracefully rather than stuck in deadlock.
  entries["new_data"] = "";
  RunBlockImageUpdate(false, entries, image_file_, "");

  entries["new_data"] = std::string(10, 'a');
  RunBlockImageUpdate(false, entries, image_file_, "");

  // Expect to write 1 block of new data successfully.
  entries["new_data"] = std::string(4096, 'a');
  RunBlockImageUpdate(false, entries, image_file_, "t");
}

TEST_F(UpdaterTest, brotli_new_data) {
  auto generator = []() { return rand() % 128; };
  // Generate 100 blocks of random data.
  std::string brotli_new_data;
  brotli_new_data.reserve(4096 * 100);
  generate_n(back_inserter(brotli_new_data), 4096 * 100, generator);

  size_t encoded_size = BrotliEncoderMaxCompressedSize(brotli_new_data.size());
  std::string encoded_data(encoded_size, 0);
  ASSERT_TRUE(BrotliEncoderCompress(
      BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW, BROTLI_DEFAULT_MODE, brotli_new_data.size(),
      reinterpret_cast<const uint8_t*>(brotli_new_data.data()), &encoded_size,
      reinterpret_cast<uint8_t*>(const_cast<char*>(encoded_data.data()))));
  encoded_data.resize(encoded_size);

  // Write a few small chunks of new data, then a large chunk, and finally a few small chunks.
  // This helps us to catch potential short writes.
  std::vector<std::string> transfer_list = {
    "4",
    "100",
    "0",
    "0",
    "new 2,0,1",
    "new 2,1,2",
    "new 4,2,50,50,97",
    "new 2,97,98",
    "new 2,98,99",
    "new 2,99,100",
  };

  PackageEntries entries{
    { "new_data.br", std::move(encoded_data) },
    { "patch_data", "" },
    { "transfer_list", android::base::Join(transfer_list, '\n') },
  };

  RunBlockImageUpdate(false, entries, image_file_, "t");

  std::string updated_content;
  ASSERT_TRUE(android::base::ReadFileToString(image_file_, &updated_content));
  ASSERT_EQ(brotli_new_data, updated_content);
}

TEST_F(UpdaterTest, last_command_update) {
  std::string block1(4096, '1');
  std::string block2(4096, '2');
  std::string block3(4096, '3');
  std::string block1_hash = GetSha1(block1);
  std::string block2_hash = GetSha1(block2);
  std::string block3_hash = GetSha1(block3);

  // Compose the transfer list to fail the first update.
  std::vector<std::string> transfer_list_fail{
    // clang-format off
    "4",
    "2",
    "0",
    "2",
    "stash " + block1_hash + " 2,0,1",
    "move " + block1_hash + " 2,1,2 1 2,0,1",
    "stash " + block3_hash + " 2,2,3",
    "abort",
    // clang-format on
  };

  // Mimic a resumed update with the same transfer commands.
  std::vector<std::string> transfer_list_continue{
    // clang-format off
    "4",
    "2",
    "0",
    "2",
    "stash " + block1_hash + " 2,0,1",
    "move " + block1_hash + " 2,1,2 1 2,0,1",
    "stash " + block3_hash + " 2,2,3",
    "move " + block1_hash + " 2,2,3 1 2,0,1",
    // clang-format on
  };

  ASSERT_TRUE(android::base::WriteStringToFile(block1 + block2 + block3, image_file_));

  PackageEntries entries{
    { "new_data", "" },
    { "patch_data", "" },
    { "transfer_list", android::base::Join(transfer_list_fail, '\n') },
  };

  // "2\nstash " + block3_hash + " 2,2,3"
  std::string last_command_content =
      "2\n" + transfer_list_fail[TransferList::kTransferListHeaderLines + 2];

  RunBlockImageUpdate(false, entries, image_file_, "");

  // Expect last_command to contain the last stash command.
  std::string last_command_actual;
  ASSERT_TRUE(android::base::ReadFileToString(last_command_file_, &last_command_actual));
  EXPECT_EQ(last_command_content, last_command_actual);

  std::string updated_contents;
  ASSERT_TRUE(android::base::ReadFileToString(image_file_, &updated_contents));
  ASSERT_EQ(block1 + block1 + block3, updated_contents);

  // "Resume" the update. Expect the first 'move' to be skipped but the second 'move' to be
  // executed. Note that we intentionally reset the image file.
  entries["transfer_list"] = android::base::Join(transfer_list_continue, '\n');
  ASSERT_TRUE(android::base::WriteStringToFile(block1 + block2 + block3, image_file_));
  RunBlockImageUpdate(false, entries, image_file_, "t");

  ASSERT_TRUE(android::base::ReadFileToString(image_file_, &updated_contents));
  ASSERT_EQ(block1 + block2 + block1, updated_contents);
}

TEST_F(UpdaterTest, last_command_update_unresumable) {
  std::string block1(4096, '1');
  std::string block2(4096, '2');
  std::string block1_hash = GetSha1(block1);
  std::string block2_hash = GetSha1(block2);

  // Construct an unresumable update with source blocks mismatch.
  std::vector<std::string> transfer_list_unresumable{
    // clang-format off
    "4",
    "2",
    "0",
    "2",
    "stash " + block1_hash + " 2,0,1",
    "move " + block2_hash + " 2,1,2 1 2,0,1",
    // clang-format on
  };

  PackageEntries entries{
    { "new_data", "" },
    { "patch_data", "" },
    { "transfer_list", android::base::Join(transfer_list_unresumable, '\n') },
  };

  ASSERT_TRUE(android::base::WriteStringToFile(block1 + block1, image_file_));

  std::string last_command_content =
      "0\n" + transfer_list_unresumable[TransferList::kTransferListHeaderLines];
  ASSERT_TRUE(android::base::WriteStringToFile(last_command_content, last_command_file_));

  RunBlockImageUpdate(false, entries, image_file_, "");

  // The last_command_file will be deleted if the update encounters an unresumable failure later.
  ASSERT_EQ(-1, access(last_command_file_.c_str(), R_OK));
}

TEST_F(UpdaterTest, last_command_verify) {
  std::string block1(4096, '1');
  std::string block2(4096, '2');
  std::string block3(4096, '3');
  std::string block1_hash = GetSha1(block1);
  std::string block2_hash = GetSha1(block2);
  std::string block3_hash = GetSha1(block3);

  std::vector<std::string> transfer_list_verify{
    // clang-format off
    "4",
    "2",
    "0",
    "2",
    "stash " + block1_hash + " 2,0,1",
    "move " + block1_hash + " 2,0,1 1 2,0,1",
    "move " + block1_hash + " 2,1,2 1 2,0,1",
    "stash " + block3_hash + " 2,2,3",
    // clang-format on
  };

  PackageEntries entries{
    { "new_data", "" },
    { "patch_data", "" },
    { "transfer_list", android::base::Join(transfer_list_verify, '\n') },
  };

  ASSERT_TRUE(android::base::WriteStringToFile(block1 + block1 + block3, image_file_));

  // Last command: "move " + block1_hash + " 2,1,2 1 2,0,1"
  std::string last_command_content =
      "2\n" + transfer_list_verify[TransferList::kTransferListHeaderLines + 2];

  // First run: expect the verification to succeed and the last_command_file is intact.
  ASSERT_TRUE(android::base::WriteStringToFile(last_command_content, last_command_file_));

  RunBlockImageUpdate(true, entries, image_file_, "t");

  std::string last_command_actual;
  ASSERT_TRUE(android::base::ReadFileToString(last_command_file_, &last_command_actual));
  EXPECT_EQ(last_command_content, last_command_actual);

  // Second run with a mismatching block image: expect the verification to succeed but
  // last_command_file to be deleted; because the target blocks in the last command don't have the
  // expected contents for the second move command.
  ASSERT_TRUE(android::base::WriteStringToFile(block1 + block2 + block3, image_file_));
  RunBlockImageUpdate(true, entries, image_file_, "t");
  ASSERT_EQ(-1, access(last_command_file_.c_str(), R_OK));
}

class ResumableUpdaterTest : public UpdaterTestBase, public testing::TestWithParam<size_t> {
 protected:
  void SetUp() override {
    UpdaterTestBase::SetUp();
    // Enable a special command "abort" to simulate interruption.
    Command::abort_allowed_ = true;
    index_ = GetParam();
  }

  void TearDown() override {
    UpdaterTestBase::TearDown();
  }

  size_t index_;
};

static std::string g_source_image;
static std::string g_target_image;
static PackageEntries g_entries;

static std::vector<std::string> GenerateTransferList() {
  std::string a(4096, 'a');
  std::string b(4096, 'b');
  std::string c(4096, 'c');
  std::string d(4096, 'd');
  std::string e(4096, 'e');
  std::string f(4096, 'f');
  std::string g(4096, 'g');
  std::string h(4096, 'h');
  std::string i(4096, 'i');
  std::string zero(4096, '\0');

  std::string a_hash = GetSha1(a);
  std::string b_hash = GetSha1(b);
  std::string c_hash = GetSha1(c);
  std::string e_hash = GetSha1(e);

  auto loc = [](const std::string& range_text) {
    std::vector<std::string> pieces = android::base::Split(range_text, "-");
    size_t left;
    size_t right;
    if (pieces.size() == 1) {
      CHECK(android::base::ParseUint(pieces[0], &left));
      right = left + 1;
    } else {
      CHECK_EQ(2u, pieces.size());
      CHECK(android::base::ParseUint(pieces[0], &left));
      CHECK(android::base::ParseUint(pieces[1], &right));
      right++;
    }
    return android::base::StringPrintf("2,%zu,%zu", left, right);
  };

  // patch 1: "b d c" -> "g"
  TemporaryFile patch_file_bdc_g;
  std::string bdc = b + d + c;
  std::string bdc_hash = GetSha1(bdc);
  std::string g_hash = GetSha1(g);
  CHECK_EQ(0, bsdiff::bsdiff(reinterpret_cast<const uint8_t*>(bdc.data()), bdc.size(),
                             reinterpret_cast<const uint8_t*>(g.data()), g.size(),
                             patch_file_bdc_g.path, nullptr));
  std::string patch_bdc_g;
  CHECK(android::base::ReadFileToString(patch_file_bdc_g.path, &patch_bdc_g));

  // patch 2: "a b c d" -> "d c b"
  TemporaryFile patch_file_abcd_dcb;
  std::string abcd = a + b + c + d;
  std::string abcd_hash = GetSha1(abcd);
  std::string dcb = d + c + b;
  std::string dcb_hash = GetSha1(dcb);
  CHECK_EQ(0, bsdiff::bsdiff(reinterpret_cast<const uint8_t*>(abcd.data()), abcd.size(),
                             reinterpret_cast<const uint8_t*>(dcb.data()), dcb.size(),
                             patch_file_abcd_dcb.path, nullptr));
  std::string patch_abcd_dcb;
  CHECK(android::base::ReadFileToString(patch_file_abcd_dcb.path, &patch_abcd_dcb));

  std::vector<std::string> transfer_list{
    "4",
    "10",  // total blocks written
    "2",   // maximum stash entries
    "2",   // maximum number of stashed blocks

    // a b c d e a b c d e
    "stash " + b_hash + " " + loc("1"),
    // a b c d e a b c d e    [b(1)]
    "stash " + c_hash + " " + loc("2"),
    // a b c d e a b c d e    [b(1)][c(2)]
    "new " + loc("1-2"),
    // a i h d e a b c d e    [b(1)][c(2)]
    "zero " + loc("0"),
    // 0 i h d e a b c d e    [b(1)][c(2)]

    // bsdiff "b d c" (from stash, 3, stash) to get g(3)
    android::base::StringPrintf(
        "bsdiff 0 %zu %s %s %s 3 %s %s %s:%s %s:%s",
        patch_bdc_g.size(),                  // patch start (0), patch length
        bdc_hash.c_str(),                    // source hash
        g_hash.c_str(),                      // target hash
        loc("3").c_str(),                    // target range
        loc("3").c_str(), loc("1").c_str(),  // load "d" from block 3, into buffer at offset 1
        b_hash.c_str(), loc("0").c_str(),    // load "b" from stash, into buffer at offset 0
        c_hash.c_str(), loc("2").c_str()),   // load "c" from stash, into buffer at offset 2

    // 0 i h g e a b c d e    [b(1)][c(2)]
    "free " + b_hash,
    // 0 i h g e a b c d e    [c(2)]
    "free " + a_hash,
    // 0 i h g e a b c d e
    "stash " + a_hash + " " + loc("5"),
    // 0 i h g e a b c d e    [a(5)]
    "move " + e_hash + " " + loc("5") + " 1 " + loc("4"),
    // 0 i h g e e b c d e    [a(5)]

    // bsdiff "a b c d" (from stash, 6-8) to "d c b" (6-8)
    android::base::StringPrintf(  //
        "bsdiff %zu %zu %s %s %s 4 %s %s %s:%s",
        patch_bdc_g.size(),                          // patch start
        patch_bdc_g.size() + patch_abcd_dcb.size(),  // patch length
        abcd_hash.c_str(),                           // source hash
        dcb_hash.c_str(),                            // target hash
        loc("6-8").c_str(),                          // target range
        loc("6-8").c_str(),                          // load "b c d" from blocks 6-8
        loc("1-3").c_str(),                          //   into buffer at offset 1-3
        a_hash.c_str(),                              // load "a" from stash
        loc("0").c_str()),                           //   into buffer at offset 0

    // 0 i h g e e d c b e    [a(5)]
    "new " + loc("4"),
    // 0 i h g f e d c b e    [a(5)]
    "move " + a_hash + " " + loc("9") + " 1 - " + a_hash + ":" + loc("0"),
    // 0 i h g f e d c b a    [a(5)]
    "free " + a_hash,
    // 0 i h g f e d c b a
  };

  std::string new_data = i + h + f;
  std::string patch_data = patch_bdc_g + patch_abcd_dcb;

  g_entries = {
    { "new_data", new_data },
    { "patch_data", patch_data },
  };
  g_source_image = a + b + c + d + e + a + b + c + d + e;
  g_target_image = zero + i + h + g + f + e + d + c + b + a;

  return transfer_list;
}

static const std::vector<std::string> g_transfer_list = GenerateTransferList();

INSTANTIATE_TEST_CASE_P(InterruptAfterEachCommand, ResumableUpdaterTest,
                        ::testing::Range(static_cast<size_t>(0),
                                         g_transfer_list.size() -
                                             TransferList::kTransferListHeaderLines));

TEST_P(ResumableUpdaterTest, InterruptVerifyResume) {
  ASSERT_TRUE(android::base::WriteStringToFile(g_source_image, image_file_));

  LOG(INFO) << "Interrupting at line " << index_ << " ("
            << g_transfer_list[TransferList::kTransferListHeaderLines + index_] << ")";

  std::vector<std::string> transfer_list_copy{ g_transfer_list };
  transfer_list_copy[TransferList::kTransferListHeaderLines + index_] = "abort";

  g_entries["transfer_list"] = android::base::Join(transfer_list_copy, '\n');

  // Run update that's expected to fail.
  RunBlockImageUpdate(false, g_entries, image_file_, "");

  std::string last_command_expected;

  // Assert the last_command_file.
  if (index_ == 0) {
    ASSERT_EQ(-1, access(last_command_file_.c_str(), R_OK));
  } else {
    last_command_expected = std::to_string(index_ - 1) + "\n" +
                            g_transfer_list[TransferList::kTransferListHeaderLines + index_ - 1];
    std::string last_command_actual;
    ASSERT_TRUE(android::base::ReadFileToString(last_command_file_, &last_command_actual));
    ASSERT_EQ(last_command_expected, last_command_actual);
  }

  g_entries["transfer_list"] = android::base::Join(g_transfer_list, '\n');

  // Resume the interrupted update, by doing verification first.
  RunBlockImageUpdate(true, g_entries, image_file_, "t");

  // last_command_file should remain intact.
  if (index_ == 0) {
    ASSERT_EQ(-1, access(last_command_file_.c_str(), R_OK));
  } else {
    std::string last_command_actual;
    ASSERT_TRUE(android::base::ReadFileToString(last_command_file_, &last_command_actual));
    ASSERT_EQ(last_command_expected, last_command_actual);
  }

  // Resume the update.
  RunBlockImageUpdate(false, g_entries, image_file_, "t");

  // last_command_file should be gone after successful update.
  ASSERT_EQ(-1, access(last_command_file_.c_str(), R_OK));

  std::string updated_image_actual;
  ASSERT_TRUE(android::base::ReadFileToString(image_file_, &updated_image_actual));
  ASSERT_EQ(g_target_image, updated_image_actual);
}
