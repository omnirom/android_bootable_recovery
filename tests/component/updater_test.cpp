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

#include <string>

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/test_utils.h>
#include <gtest/gtest.h>

#include "edify/expr.h"
#include "error_code.h"
#include "updater/install.h"

struct selabel_handle *sehandle = nullptr;

static void expect(const char* expected, const char* expr_str, CauseCode cause_code) {
    Expr* e;
    int error_count;
    EXPECT_EQ(parse_string(expr_str, &e, &error_count), 0);

    State state(expr_str, nullptr);

    std::string result;
    bool status = Evaluate(&state, e, &result);

    if (expected == nullptr) {
        EXPECT_FALSE(status);
    } else {
        EXPECT_STREQ(expected, result.c_str());
    }

    // Error code is set in updater/updater.cpp only, by parsing State.errmsg.
    EXPECT_EQ(kNoError, state.error_code);

    // Cause code should always be available.
    EXPECT_EQ(cause_code, state.cause_code);

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
}
