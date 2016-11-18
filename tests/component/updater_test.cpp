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

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/test_utils.h>
#include <gtest/gtest.h>
#include <ziparchive/zip_archive.h>

#include "common/test_constants.h"
#include "edify/expr.h"
#include "error_code.h"
#include "updater/install.h"
#include "updater/updater.h"

struct selabel_handle *sehandle = nullptr;

static void expect(const char* expected, const char* expr_str, CauseCode cause_code,
                   UpdaterInfo* info = nullptr) {
  Expr* e;
  int error_count = 0;
  ASSERT_EQ(0, parse_string(expr_str, &e, &error_count));
  ASSERT_EQ(0, error_count);

  State state(expr_str, info);

  std::string result;
  bool status = Evaluate(&state, e, &result);

  if (expected == nullptr) {
    ASSERT_FALSE(status);
  } else {
    ASSERT_TRUE(status);
    ASSERT_STREQ(expected, result.c_str());
  }

  // Error code is set in updater/updater.cpp only, by parsing State.errmsg.
  ASSERT_EQ(kNoError, state.error_code);

  // Cause code should always be available.
  ASSERT_EQ(cause_code, state.cause_code);
}

class UpdaterTest : public ::testing::Test {
  protected:
    virtual void SetUp() {
        RegisterBuiltins();
        RegisterInstallFunctions();
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

TEST_F(UpdaterTest, sha1_check) {
    // sha1_check(data) returns the SHA-1 of the data.
    expect("81fe8bfe87576c3ecb22426f8e57847382917acf", "sha1_check(\"abcd\")", kNoCause);
    expect("da39a3ee5e6b4b0d3255bfef95601890afd80709", "sha1_check(\"\")", kNoCause);

    // sha1_check(data, sha1_hex, [sha1_hex, ...]) returns the matched SHA-1.
    expect("81fe8bfe87576c3ecb22426f8e57847382917acf",
           "sha1_check(\"abcd\", \"81fe8bfe87576c3ecb22426f8e57847382917acf\")",
           kNoCause);

    expect("81fe8bfe87576c3ecb22426f8e57847382917acf",
           "sha1_check(\"abcd\", \"wrong_sha1\", \"81fe8bfe87576c3ecb22426f8e57847382917acf\")",
           kNoCause);

    // Or "" if there's no match.
    expect("",
           "sha1_check(\"abcd\", \"wrong_sha1\")",
           kNoCause);

    expect("",
           "sha1_check(\"abcd\", \"wrong_sha1\", \"wrong_sha2\")",
           kNoCause);

    // sha1_check() expects at least one argument.
    expect(nullptr, "sha1_check()", kArgsParsingFailure);
}

TEST_F(UpdaterTest, file_getprop) {
    // file_getprop() expects two arguments.
    expect(nullptr, "file_getprop()", kArgsParsingFailure);
    expect(nullptr, "file_getprop(\"arg1\")", kArgsParsingFailure);
    expect(nullptr, "file_getprop(\"arg1\", \"arg2\", \"arg3\")", kArgsParsingFailure);

    // File doesn't exist.
    expect(nullptr, "file_getprop(\"/doesntexist\", \"key1\")", kFileGetPropFailure);

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
    expect("tardis", script1.c_str(), kNoCause);

    std::string script2("file_getprop(\"" + std::string(temp_file2.path) +
                       "\", \"ro.product.board\")");
    expect("magic", script2.c_str(), kNoCause);

    // No match.
    std::string script3("file_getprop(\"" + std::string(temp_file2.path) +
                       "\", \"ro.product.wrong\")");
    expect("", script3.c_str(), kNoCause);

    std::string script4("file_getprop(\"" + std::string(temp_file2.path) +
                       "\", \"ro.product.name=\")");
    expect("", script4.c_str(), kNoCause);

    std::string script5("file_getprop(\"" + std::string(temp_file2.path) +
                       "\", \"ro.product.nam\")");
    expect("", script5.c_str(), kNoCause);

    std::string script6("file_getprop(\"" + std::string(temp_file2.path) +
                       "\", \"ro.product.model\")");
    expect("", script6.c_str(), kNoCause);
}

TEST_F(UpdaterTest, delete) {
    // Delete none.
    expect("0", "delete()", kNoCause);
    expect("0", "delete(\"/doesntexist\")", kNoCause);
    expect("0", "delete(\"/doesntexist1\", \"/doesntexist2\")", kNoCause);
    expect("0", "delete(\"/doesntexist1\", \"/doesntexist2\", \"/doesntexist3\")", kNoCause);

    // Delete one file.
    TemporaryFile temp_file1;
    ASSERT_TRUE(android::base::WriteStringToFile("abc", temp_file1.path));
    std::string script1("delete(\"" + std::string(temp_file1.path) + "\")");
    expect("1", script1.c_str(), kNoCause);

    // Delete two files.
    TemporaryFile temp_file2;
    ASSERT_TRUE(android::base::WriteStringToFile("abc", temp_file2.path));
    TemporaryFile temp_file3;
    ASSERT_TRUE(android::base::WriteStringToFile("abc", temp_file3.path));
    std::string script2("delete(\"" + std::string(temp_file2.path) + "\", \"" +
                        std::string(temp_file3.path) + "\")");
    expect("2", script2.c_str(), kNoCause);

    // Delete already deleted files.
    expect("0", script2.c_str(), kNoCause);

    // Delete one out of three.
    TemporaryFile temp_file4;
    ASSERT_TRUE(android::base::WriteStringToFile("abc", temp_file4.path));
    std::string script3("delete(\"/doesntexist1\", \"" + std::string(temp_file4.path) +
                        "\", \"/doesntexist2\")");
    expect("1", script3.c_str(), kNoCause);
}

TEST_F(UpdaterTest, rename) {
    // rename() expects two arguments.
    expect(nullptr, "rename()", kArgsParsingFailure);
    expect(nullptr, "rename(\"arg1\")", kArgsParsingFailure);
    expect(nullptr, "rename(\"arg1\", \"arg2\", \"arg3\")", kArgsParsingFailure);

    // src_name or dst_name cannot be empty.
    expect(nullptr, "rename(\"\", \"arg2\")", kArgsParsingFailure);
    expect(nullptr, "rename(\"arg1\", \"\")", kArgsParsingFailure);

    // File doesn't exist (both of src and dst).
    expect(nullptr, "rename(\"/doesntexist\", \"/doesntexisteither\")" , kFileRenameFailure);

    // Can't create parent directory.
    TemporaryFile temp_file1;
    ASSERT_TRUE(android::base::WriteStringToFile("abc", temp_file1.path));
    std::string script1("rename(\"" + std::string(temp_file1.path) + "\", \"/proc/0/file1\")");
    expect(nullptr, script1.c_str(), kFileRenameFailure);

    // Rename.
    TemporaryFile temp_file2;
    std::string script2("rename(\"" + std::string(temp_file1.path) + "\", \"" +
                        std::string(temp_file2.path) + "\")");
    expect(temp_file2.path, script2.c_str(), kNoCause);

    // Already renamed.
    expect(temp_file2.path, script2.c_str(), kNoCause);

    // Parents create successfully.
    TemporaryFile temp_file3;
    TemporaryDir td;
    std::string temp_dir(td.path);
    std::string dst_file = temp_dir + "/aaa/bbb/a.txt";
    std::string script3("rename(\"" + std::string(temp_file3.path) + "\", \"" + dst_file + "\")");
    expect(dst_file.c_str(), script3.c_str(), kNoCause);

    // Clean up the temp files under td.
    ASSERT_EQ(0, unlink(dst_file.c_str()));
    ASSERT_EQ(0, rmdir((temp_dir + "/aaa/bbb").c_str()));
    ASSERT_EQ(0, rmdir((temp_dir + "/aaa").c_str()));
}

TEST_F(UpdaterTest, symlink) {
    // symlink expects 1+ argument.
    expect(nullptr, "symlink()", kArgsParsingFailure);

    // symlink should fail if src is an empty string.
    TemporaryFile temp_file1;
    std::string script1("symlink(\"" + std::string(temp_file1.path) + "\", \"\")");
    expect(nullptr, script1.c_str(), kSymlinkFailure);

    std::string script2("symlink(\"" + std::string(temp_file1.path) + "\", \"src1\", \"\")");
    expect(nullptr, script2.c_str(), kSymlinkFailure);

    // symlink failed to remove old src.
    std::string script3("symlink(\"" + std::string(temp_file1.path) + "\", \"/proc\")");
    expect(nullptr, script3.c_str(), kSymlinkFailure);

    // symlink can create symlinks.
    TemporaryFile temp_file;
    std::string content = "magicvalue";
    ASSERT_TRUE(android::base::WriteStringToFile(content, temp_file.path));

    TemporaryDir td;
    std::string src1 = std::string(td.path) + "/symlink1";
    std::string src2 = std::string(td.path) + "/symlink2";
    std::string script4("symlink(\"" + std::string(temp_file.path) + "\", \"" +
                        src1 + "\", \"" + src2 + "\")");
    expect("t", script4.c_str(), kNoCause);

    // Verify the created symlinks.
    struct stat sb;
    ASSERT_TRUE(lstat(src1.c_str(), &sb) == 0 && S_ISLNK(sb.st_mode));
    ASSERT_TRUE(lstat(src2.c_str(), &sb) == 0 && S_ISLNK(sb.st_mode));

    // Clean up the leftovers.
    ASSERT_EQ(0, unlink(src1.c_str()));
    ASSERT_EQ(0, unlink(src2.c_str()));
}

TEST_F(UpdaterTest, package_extract_dir) {
  // package_extract_dir expects 2 arguments.
  expect(nullptr, "package_extract_dir()", kArgsParsingFailure);
  expect(nullptr, "package_extract_dir(\"arg1\")", kArgsParsingFailure);
  expect(nullptr, "package_extract_dir(\"arg1\", \"arg2\", \"arg3\")", kArgsParsingFailure);

  std::string zip_path = from_testdata_base("ziptest_valid.zip");
  ZipArchiveHandle handle;
  ASSERT_EQ(0, OpenArchive(zip_path.c_str(), &handle));

  // Need to set up the ziphandle.
  UpdaterInfo updater_info;
  updater_info.package_zip = handle;

  // Extract "b/c.txt" and "b/d.txt" with package_extract_dir("b", "<dir>").
  TemporaryDir td;
  std::string temp_dir(td.path);
  std::string script("package_extract_dir(\"b\", \"" + temp_dir + "\")");
  expect("t", script.c_str(), kNoCause, &updater_info);

  // Verify.
  std::string data;
  std::string file_c = temp_dir + "/c.txt";
  ASSERT_TRUE(android::base::ReadFileToString(file_c, &data));
  ASSERT_EQ(kCTxtContents, data);

  std::string file_d = temp_dir + "/d.txt";
  ASSERT_TRUE(android::base::ReadFileToString(file_d, &data));
  ASSERT_EQ(kDTxtContents, data);

  // Modify the contents in order to retry. It's expected to be overwritten.
  ASSERT_TRUE(android::base::WriteStringToFile("random", file_c));
  ASSERT_TRUE(android::base::WriteStringToFile("random", file_d));

  // Extract again and verify.
  expect("t", script.c_str(), kNoCause, &updater_info);

  ASSERT_TRUE(android::base::ReadFileToString(file_c, &data));
  ASSERT_EQ(kCTxtContents, data);
  ASSERT_TRUE(android::base::ReadFileToString(file_d, &data));
  ASSERT_EQ(kDTxtContents, data);

  // Clean up the temp files under td.
  ASSERT_EQ(0, unlink(file_c.c_str()));
  ASSERT_EQ(0, unlink(file_d.c_str()));

  // Extracting "b/" (with slash) should give the same result.
  script = "package_extract_dir(\"b/\", \"" + temp_dir + "\")";
  expect("t", script.c_str(), kNoCause, &updater_info);

  ASSERT_TRUE(android::base::ReadFileToString(file_c, &data));
  ASSERT_EQ(kCTxtContents, data);
  ASSERT_TRUE(android::base::ReadFileToString(file_d, &data));
  ASSERT_EQ(kDTxtContents, data);

  ASSERT_EQ(0, unlink(file_c.c_str()));
  ASSERT_EQ(0, unlink(file_d.c_str()));

  // Extracting "" is allowed. The entries will carry the path name.
  script = "package_extract_dir(\"\", \"" + temp_dir + "\")";
  expect("t", script.c_str(), kNoCause, &updater_info);

  std::string file_a = temp_dir + "/a.txt";
  ASSERT_TRUE(android::base::ReadFileToString(file_a, &data));
  ASSERT_EQ(kATxtContents, data);
  std::string file_b = temp_dir + "/b.txt";
  ASSERT_TRUE(android::base::ReadFileToString(file_b, &data));
  ASSERT_EQ(kBTxtContents, data);
  std::string file_b_c = temp_dir + "/b/c.txt";
  ASSERT_TRUE(android::base::ReadFileToString(file_b_c, &data));
  ASSERT_EQ(kCTxtContents, data);
  std::string file_b_d = temp_dir + "/b/d.txt";
  ASSERT_TRUE(android::base::ReadFileToString(file_b_d, &data));
  ASSERT_EQ(kDTxtContents, data);

  ASSERT_EQ(0, unlink(file_a.c_str()));
  ASSERT_EQ(0, unlink(file_b.c_str()));
  ASSERT_EQ(0, unlink(file_b_c.c_str()));
  ASSERT_EQ(0, unlink(file_b_d.c_str()));
  ASSERT_EQ(0, rmdir((temp_dir + "/b").c_str()));

  // Extracting non-existent entry should still give "t".
  script = "package_extract_dir(\"doesntexist\", \"" + temp_dir + "\")";
  expect("t", script.c_str(), kNoCause, &updater_info);

  // Only relative zip_path is allowed.
  script = "package_extract_dir(\"/b\", \"" + temp_dir + "\")";
  expect("", script.c_str(), kNoCause, &updater_info);

  // Only absolute dest_path is allowed.
  script = "package_extract_dir(\"b\", \"path\")";
  expect("", script.c_str(), kNoCause, &updater_info);

  CloseArchive(handle);
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
  UpdaterInfo updater_info;
  updater_info.package_zip = handle;

  // Two-argument version.
  TemporaryFile temp_file1;
  std::string script("package_extract_file(\"a.txt\", \"" + std::string(temp_file1.path) + "\")");
  expect("t", script.c_str(), kNoCause, &updater_info);

  // Verify the extracted entry.
  std::string data;
  ASSERT_TRUE(android::base::ReadFileToString(temp_file1.path, &data));
  ASSERT_EQ(kATxtContents, data);

  // Now extract another entry to the same location, which should overwrite.
  script = "package_extract_file(\"b.txt\", \"" + std::string(temp_file1.path) + "\")";
  expect("t", script.c_str(), kNoCause, &updater_info);

  ASSERT_TRUE(android::base::ReadFileToString(temp_file1.path, &data));
  ASSERT_EQ(kBTxtContents, data);

  // Missing zip entry. The two-argument version doesn't abort.
  script = "package_extract_file(\"doesntexist\", \"" + std::string(temp_file1.path) + "\")";
  expect("", script.c_str(), kNoCause, &updater_info);

  // Extract to /dev/full should fail.
  script = "package_extract_file(\"a.txt\", \"/dev/full\")";
  expect("", script.c_str(), kNoCause, &updater_info);

  // One-argument version.
  script = "sha1_check(package_extract_file(\"a.txt\"))";
  expect(kATxtSha1Sum.c_str(), script.c_str(), kNoCause, &updater_info);

  script = "sha1_check(package_extract_file(\"b.txt\"))";
  expect(kBTxtSha1Sum.c_str(), script.c_str(), kNoCause, &updater_info);

  // Missing entry. The one-argument version aborts the evaluation.
  script = "package_extract_file(\"doesntexist\")";
  expect(nullptr, script.c_str(), kPackageExtractFileFailure, &updater_info);

  CloseArchive(handle);
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
  expect("t", script.c_str(), kNoCause);

  // Verify the content.
  std::string content;
  ASSERT_TRUE(android::base::ReadFileToString(temp_file.path, &content));
  ASSERT_EQ(value, content);

  // Allow writing empty string.
  script = "write_value(\"\", \"" + std::string(temp_file.path) + "\")";
  expect("t", script.c_str(), kNoCause);

  // Verify the content.
  ASSERT_TRUE(android::base::ReadFileToString(temp_file.path, &content));
  ASSERT_EQ("", content);

  // It should fail gracefully when write fails.
  script = "write_value(\"value\", \"/proc/0/file1\")";
  expect("", script.c_str(), kNoCause);
}
