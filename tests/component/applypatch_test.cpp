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

#include <string>

#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <android-base/test_utils.h>

#include "applypatch/applypatch.h"
#include "common/test_constants.h"
#include "openssl/sha.h"
#include "print_sha1.h"

static const std::string DATA_PATH = getenv("ANDROID_DATA");
static const std::string TESTDATA_PATH = "/recovery/testdata";
static const std::string WORK_FS = "/data";

static std::string sha1sum(const std::string& fname) {
    uint8_t digest[SHA_DIGEST_LENGTH];
    std::string data;
    android::base::ReadFileToString(fname, &data);

    SHA1((const uint8_t*)data.c_str(), data.size(), digest);
    return print_sha1(digest);
}

static void mangle_file(const std::string& fname) {
    FILE* fh = fopen(&fname[0], "w");
    int r;
    for (int i=0; i < 1024; i++) {
        r = rand();
        fwrite(&r, sizeof(short), 1, fh);
    }
    fclose(fh);
}

static bool file_cmp(std::string& f1, std::string& f2) {
    std::string c1;
    std::string c2;
    android::base::ReadFileToString(f1, &c1);
    android::base::ReadFileToString(f2, &c2);
    return c1 == c2;
}

static std::string from_testdata_base(const std::string fname) {
    return android::base::StringPrintf("%s%s%s/%s",
            &DATA_PATH[0],
            &NATIVE_TEST_PATH[0],
            &TESTDATA_PATH[0],
            &fname[0]);
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
            android::base::WriteStringToFile("hello", rand_file);

            // set up SHA constants
            old_sha1 = sha1sum(old_file);
            new_sha1 = sha1sum(new_file);
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

static void cp(std::string src, std::string tgt) {
    std::string cmd = android::base::StringPrintf("cp %s %s",
            &src[0],
            &tgt[0]);
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
            unsigned long free_kb = FreeSpaceForFile(&WORK_FS[0]);
            ASSERT_GE(free_kb * 1024, new_size * 3 / 2);
            output_f = new TemporaryFile();
            output_loc = std::string(output_f->path);

            struct FileContents fc;

            ASSERT_EQ(0, LoadFileContents(&rand_file[0], &fc));
            Value* patch1 = new Value();
            patch1->type = VAL_BLOB;
            patch1->size = fc.data.size();
            patch1->data = static_cast<char*>(malloc(fc.data.size()));
            memcpy(patch1->data, fc.data.data(), fc.data.size());
            patches.push_back(patch1);

            ASSERT_EQ(0, LoadFileContents(&patch_file[0], &fc));
            Value* patch2 = new Value();
            patch2->type = VAL_BLOB;
            patch2->size = fc.st.st_size;
            patch2->data = static_cast<char*>(malloc(fc.data.size()));
            memcpy(patch2->data, fc.data.data(), fc.data.size());
            patches.push_back(patch2);
        }
        static void TearDownTestCase() {
            delete output_f;
            for (auto it = patches.begin(); it != patches.end(); ++it) {
                free((*it)->data);
                delete *it;
            }
            patches.clear();
        }

        static std::vector<Value*> patches;
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

std::vector<Value*> ApplyPatchFullTest::patches;
TemporaryFile* ApplyPatchFullTest::output_f;
std::string ApplyPatchFullTest::output_loc;

TEST_F(ApplyPatchTest, CheckModeSingle) {
    char* s = &old_sha1[0];
    ASSERT_EQ(0, applypatch_check(&old_file[0], 1, &s));
}

TEST_F(ApplyPatchTest, CheckModeMultiple) {
    char* argv[3] = {
        &bad_sha1_a[0],
        &old_sha1[0],
        &bad_sha1_b[0]
    };
    ASSERT_EQ(0, applypatch_check(&old_file[0], 3, argv));
}

TEST_F(ApplyPatchTest, CheckModeFailure) {
    char* argv[2] = {
        &bad_sha1_a[0],
        &bad_sha1_b[0]
    };
    ASSERT_NE(0, applypatch_check(&old_file[0], 2, argv));
}

TEST_F(ApplyPatchCacheTest, CheckCacheCorruptedSingle) {
    mangle_file(old_file);
    char* s = &old_sha1[0];
    ASSERT_EQ(0, applypatch_check(&old_file[0], 1, &s));
}

TEST_F(ApplyPatchCacheTest, CheckCacheCorruptedMultiple) {
    mangle_file(old_file);
    char* argv[3] = {
        &bad_sha1_a[0],
        &old_sha1[0],
        &bad_sha1_b[0]
    };
    ASSERT_EQ(0, applypatch_check(&old_file[0], 3, argv));
}

TEST_F(ApplyPatchCacheTest, CheckCacheCorruptedFailure) {
    mangle_file(old_file);
    char* argv[2] = {
        &bad_sha1_a[0],
        &bad_sha1_b[0]
    };
    ASSERT_NE(0, applypatch_check(&old_file[0], 2, argv));
}

TEST_F(ApplyPatchCacheTest, CheckCacheMissingSingle) {
    unlink(&old_file[0]);
    char* s = &old_sha1[0];
    ASSERT_EQ(0, applypatch_check(&old_file[0], 1, &s));
}

TEST_F(ApplyPatchCacheTest, CheckCacheMissingMultiple) {
    unlink(&old_file[0]);
    char* argv[3] = {
        &bad_sha1_a[0],
        &old_sha1[0],
        &bad_sha1_b[0]
    };
    ASSERT_EQ(0, applypatch_check(&old_file[0], 3, argv));
}

TEST_F(ApplyPatchCacheTest, CheckCacheMissingFailure) {
    unlink(&old_file[0]);
    char* argv[2] = {
        &bad_sha1_a[0],
        &bad_sha1_b[0]
    };
    ASSERT_NE(0, applypatch_check(&old_file[0], 2, argv));
}

TEST_F(ApplyPatchFullTest, ApplyInPlace) {
    std::vector<char*> sha1s;
    sha1s.push_back(&bad_sha1_a[0]);
    sha1s.push_back(&old_sha1[0]);

    int ap_result = applypatch(&old_file[0],
            "-",
            &new_sha1[0],
            new_size,
            2,
            sha1s.data(),
            patches.data(),
            nullptr);
    ASSERT_EQ(0, ap_result);
    ASSERT_TRUE(file_cmp(old_file, new_file));
    // reapply, applypatch is idempotent so it should succeed
    ap_result = applypatch(&old_file[0],
            "-",
            &new_sha1[0],
            new_size,
            2,
            sha1s.data(),
            patches.data(),
            nullptr);
    ASSERT_EQ(0, ap_result);
    ASSERT_TRUE(file_cmp(old_file, new_file));
}

TEST_F(ApplyPatchFullTest, ApplyInNewLocation) {
    std::vector<char*> sha1s;
    sha1s.push_back(&bad_sha1_a[0]);
    sha1s.push_back(&old_sha1[0]);
    int ap_result = applypatch(&old_file[0],
            &output_loc[0],
            &new_sha1[0],
            new_size,
            2,
            sha1s.data(),
            patches.data(),
            nullptr);
    ASSERT_EQ(0, ap_result);
    ASSERT_TRUE(file_cmp(output_loc, new_file));
    ap_result = applypatch(&old_file[0],
            &output_loc[0],
            &new_sha1[0],
            new_size,
            2,
            sha1s.data(),
            patches.data(),
            nullptr);
    ASSERT_EQ(0, ap_result);
    ASSERT_TRUE(file_cmp(output_loc, new_file));
}

TEST_F(ApplyPatchFullTest, ApplyCorruptedInNewLocation) {
    mangle_file(old_file);
    std::vector<char*> sha1s;
    sha1s.push_back(&bad_sha1_a[0]);
    sha1s.push_back(&old_sha1[0]);
    int ap_result = applypatch(&old_file[0],
            &output_loc[0],
            &new_sha1[0],
            new_size,
            2,
            sha1s.data(),
            patches.data(),
            nullptr);
    ASSERT_EQ(0, ap_result);
    ASSERT_TRUE(file_cmp(output_loc, new_file));
    ap_result = applypatch(&old_file[0],
            &output_loc[0],
            &new_sha1[0],
            new_size,
            2,
            sha1s.data(),
            patches.data(),
            nullptr);
    ASSERT_EQ(0, ap_result);
    ASSERT_TRUE(file_cmp(output_loc, new_file));
}

TEST_F(ApplyPatchDoubleCacheTest, ApplyDoubleCorruptedInNewLocation) {
    mangle_file(old_file);
    mangle_file(cache_file);

    std::vector<char*> sha1s;
    sha1s.push_back(&bad_sha1_a[0]);
    sha1s.push_back(&old_sha1[0]);
    int ap_result = applypatch(&old_file[0],
            &output_loc[0],
            &new_sha1[0],
            new_size,
            2,
            sha1s.data(),
            patches.data(),
            nullptr);
    ASSERT_NE(0, ap_result);
    ASSERT_FALSE(file_cmp(output_loc, new_file));
    ap_result = applypatch(&old_file[0],
            &output_loc[0],
            &new_sha1[0],
            new_size,
            2,
            sha1s.data(),
            patches.data(),
            nullptr);
    ASSERT_NE(0, ap_result);
    ASSERT_FALSE(file_cmp(output_loc, new_file));
}
