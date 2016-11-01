/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agree to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fcntl.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>

#include <memory>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <android-base/test_utils.h>
#include <openssl/sha.h>

#include "applypatch/applypatch.h"
#include "common/test_constants.h"
#include "print_sha1.h"

static const std::string DATA_PATH = getenv("ANDROID_DATA");
static const std::string TESTDATA_PATH = "/recovery/testdata";

static void sha1sum(const std::string& fname, std::string* sha1) {
    ASSERT_NE(nullptr, sha1);

    std::string data;
    ASSERT_TRUE(android::base::ReadFileToString(fname, &data));

    uint8_t digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const uint8_t*>(data.c_str()), data.size(), digest);
    *sha1 = print_sha1(digest);
}

static void mangle_file(const std::string& fname) {
    std::string content;
    content.reserve(1024);
    for (size_t i = 0; i < 1024; i++) {
        content[i] = rand() % 256;
    }
    ASSERT_TRUE(android::base::WriteStringToFile(content, fname));
}

static bool file_cmp(const std::string& f1, const std::string& f2) {
    std::string c1;
    android::base::ReadFileToString(f1, &c1);
    std::string c2;
    android::base::ReadFileToString(f2, &c2);
    return c1 == c2;
}

static std::string from_testdata_base(const std::string& fname) {
    return DATA_PATH + NATIVE_TEST_PATH + TESTDATA_PATH + "/" + fname;
}

class ApplyPatchTest : public ::testing::Test {
  public:
    static void SetUpTestCase() {
        // set up files
        old_file = from_testdata_base("old.file");
        new_file = from_testdata_base("new.file");
        patch_file = from_testdata_base("patch.bsdiff");
        rand_file = "/cache/applypatch_test_rand.file";
        cache_file = "/cache/saved.file";

        // write stuff to rand_file
        ASSERT_TRUE(android::base::WriteStringToFile("hello", rand_file));

        // set up SHA constants
        sha1sum(old_file, &old_sha1);
        sha1sum(new_file, &new_sha1);
        srand(time(NULL));
        bad_sha1_a = android::base::StringPrintf("%040x", rand());
        bad_sha1_b = android::base::StringPrintf("%040x", rand());

        struct stat st;
        stat(&new_file[0], &st);
        new_size = st.st_size;
    }

    static std::string old_file;
    static std::string new_file;
    static std::string rand_file;
    static std::string cache_file;
    static std::string patch_file;

    static std::string old_sha1;
    static std::string new_sha1;
    static std::string bad_sha1_a;
    static std::string bad_sha1_b;

    static size_t new_size;
};

std::string ApplyPatchTest::old_file;
std::string ApplyPatchTest::new_file;

static void cp(const std::string& src, const std::string& tgt) {
    std::string cmd = "cp " + src + " " + tgt;
    system(&cmd[0]);
}

static void backup_old() {
    cp(ApplyPatchTest::old_file, ApplyPatchTest::cache_file);
}

static void restore_old() {
    cp(ApplyPatchTest::cache_file, ApplyPatchTest::old_file);
}

class ApplyPatchCacheTest : public ApplyPatchTest {
  public:
    virtual void SetUp() {
        backup_old();
    }

    virtual void TearDown() {
        restore_old();
    }
};

class ApplyPatchFullTest : public ApplyPatchCacheTest {
  public:
    static void SetUpTestCase() {
        ApplyPatchTest::SetUpTestCase();

        output_f = new TemporaryFile();
        output_loc = std::string(output_f->path);

        struct FileContents fc;

        ASSERT_EQ(0, LoadFileContents(&rand_file[0], &fc));
        patches.push_back(
            std::make_unique<Value>(VAL_BLOB, std::string(fc.data.begin(), fc.data.end())));

        ASSERT_EQ(0, LoadFileContents(&patch_file[0], &fc));
        patches.push_back(
            std::make_unique<Value>(VAL_BLOB, std::string(fc.data.begin(), fc.data.end())));
    }

    static void TearDownTestCase() {
        delete output_f;
    }

    static std::vector<std::unique_ptr<Value>> patches;
    static TemporaryFile* output_f;
    static std::string output_loc;
};

class ApplyPatchDoubleCacheTest : public ApplyPatchFullTest {
  public:
    virtual void SetUp() {
        ApplyPatchCacheTest::SetUp();
        cp(cache_file, "/cache/reallysaved.file");
    }

    virtual void TearDown() {
        cp("/cache/reallysaved.file", cache_file);
        ApplyPatchCacheTest::TearDown();
    }
};

std::string ApplyPatchTest::rand_file;
std::string ApplyPatchTest::patch_file;
std::string ApplyPatchTest::cache_file;
std::string ApplyPatchTest::old_sha1;
std::string ApplyPatchTest::new_sha1;
std::string ApplyPatchTest::bad_sha1_a;
std::string ApplyPatchTest::bad_sha1_b;

size_t ApplyPatchTest::new_size;

std::vector<std::unique_ptr<Value>> ApplyPatchFullTest::patches;
TemporaryFile* ApplyPatchFullTest::output_f;
std::string ApplyPatchFullTest::output_loc;

TEST_F(ApplyPatchTest, CheckModeSkip) {
    std::vector<std::string> sha1s;
    ASSERT_EQ(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchTest, CheckModeSingle) {
    std::vector<std::string> sha1s = { old_sha1 };
    ASSERT_EQ(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchTest, CheckModeMultiple) {
    std::vector<std::string> sha1s = {
        bad_sha1_a,
        old_sha1,
        bad_sha1_b
    };
    ASSERT_EQ(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchTest, CheckModeFailure) {
    std::vector<std::string> sha1s = {
        bad_sha1_a,
        bad_sha1_b
    };
    ASSERT_NE(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchCacheTest, CheckCacheCorruptedSingle) {
    mangle_file(old_file);
    std::vector<std::string> sha1s = { old_sha1 };
    ASSERT_EQ(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchCacheTest, CheckCacheCorruptedMultiple) {
    mangle_file(old_file);
    std::vector<std::string> sha1s = {
        bad_sha1_a,
        old_sha1,
        bad_sha1_b
    };
    ASSERT_EQ(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchCacheTest, CheckCacheCorruptedFailure) {
    mangle_file(old_file);
    std::vector<std::string> sha1s = {
        bad_sha1_a,
        bad_sha1_b
    };
    ASSERT_NE(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchCacheTest, CheckCacheMissingSingle) {
    unlink(&old_file[0]);
    std::vector<std::string> sha1s = { old_sha1 };
    ASSERT_EQ(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchCacheTest, CheckCacheMissingMultiple) {
    unlink(&old_file[0]);
    std::vector<std::string> sha1s = {
        bad_sha1_a,
        old_sha1,
        bad_sha1_b
    };
    ASSERT_EQ(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchCacheTest, CheckCacheMissingFailure) {
    unlink(&old_file[0]);
    std::vector<std::string> sha1s = {
        bad_sha1_a,
        bad_sha1_b
    };
    ASSERT_NE(0, applypatch_check(&old_file[0], sha1s));
}

TEST_F(ApplyPatchFullTest, ApplyInPlace) {
    std::vector<std::string> sha1s = {
        bad_sha1_a,
        old_sha1
    };
    int ap_result = applypatch(&old_file[0],
            "-",
            &new_sha1[0],
            new_size,
            sha1s,
            patches,
            nullptr);
    ASSERT_EQ(0, ap_result);
    ASSERT_TRUE(file_cmp(old_file, new_file));
    // reapply, applypatch is idempotent so it should succeed
    ap_result = applypatch(&old_file[0],
            "-",
            &new_sha1[0],
            new_size,
            sha1s,
            patches,
            nullptr);
    ASSERT_EQ(0, ap_result);
    ASSERT_TRUE(file_cmp(old_file, new_file));
}

TEST_F(ApplyPatchFullTest, ApplyInNewLocation) {
    std::vector<std::string> sha1s = {
        bad_sha1_a,
        old_sha1
    };
    // Apply bsdiff patch to new location.
    ASSERT_EQ(0, applypatch(&old_file[0],
            &output_loc[0],
            &new_sha1[0],
            new_size,
            sha1s,
            patches,
            nullptr));
    ASSERT_TRUE(file_cmp(output_loc, new_file));

    // Reapply to the same location.
    ASSERT_EQ(0, applypatch(&old_file[0],
            &output_loc[0],
            &new_sha1[0],
            new_size,
            sha1s,
            patches,
            nullptr));
    ASSERT_TRUE(file_cmp(output_loc, new_file));
}

TEST_F(ApplyPatchFullTest, ApplyCorruptedInNewLocation) {
    std::vector<std::string> sha1s = {
        bad_sha1_a,
        old_sha1
    };
    // Apply bsdiff patch to new location with corrupted source.
    mangle_file(old_file);
    int ap_result = applypatch(&old_file[0],
            &output_loc[0],
            &new_sha1[0],
            new_size,
            sha1s,
            patches,
            nullptr);
    ASSERT_EQ(0, ap_result);
    ASSERT_TRUE(file_cmp(output_loc, new_file));

    // Reapply bsdiff patch to new location with corrupted source.
    ap_result = applypatch(&old_file[0],
            &output_loc[0],
            &new_sha1[0],
            new_size,
            sha1s,
            patches,
            nullptr);
    ASSERT_EQ(0, ap_result);
    ASSERT_TRUE(file_cmp(output_loc, new_file));
}

TEST_F(ApplyPatchDoubleCacheTest, ApplyDoubleCorruptedInNewLocation) {
    std::vector<std::string> sha1s = {
        bad_sha1_a,
        old_sha1
    };

    // Apply bsdiff patch to new location with corrupted source and copy (no new file).
    // Expected to fail.
    mangle_file(old_file);
    mangle_file(cache_file);
    int ap_result = applypatch(&old_file[0],
            &output_loc[0],
            &new_sha1[0],
            new_size,
            sha1s,
            patches,
            nullptr);
    ASSERT_NE(0, ap_result);
    ASSERT_FALSE(file_cmp(output_loc, new_file));

    // Expected to fail again on retry.
    ap_result = applypatch(&old_file[0],
            &output_loc[0],
            &new_sha1[0],
            new_size,
            sha1s,
            patches,
            nullptr);
    ASSERT_NE(0, ap_result);
    ASSERT_FALSE(file_cmp(output_loc, new_file));

    // Expected to fail with incorrect new file.
    mangle_file(output_loc);
    ap_result = applypatch(&old_file[0],
            &output_loc[0],
            &new_sha1[0],
            new_size,
            sha1s,
            patches,
            nullptr);
    ASSERT_NE(0, ap_result);
    ASSERT_FALSE(file_cmp(output_loc, new_file));
}
