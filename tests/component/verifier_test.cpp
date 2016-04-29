/*
 * Copyright (C) 2009 The Android Open Source Project
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

#include <errno.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <memory>
#include <string>
#include <vector>

#include <openssl/sha.h>

#include <android-base/stringprintf.h>

#include "common.h"
#include "common/test_constants.h"
#include "minzip/SysUtil.h"
#include "ui.h"
#include "verifier.h"

static const char* DATA_PATH = getenv("ANDROID_DATA");
static const char* TESTDATA_PATH = "/recovery/testdata/";

RecoveryUI* ui = NULL;

class MockUI : public RecoveryUI {
    void Init() { }
    void SetStage(int, int) { }
    void SetLocale(const char*) { }
    void SetBackground(Icon icon) { }
    void SetSystemUpdateText(bool security_update) { }

    void SetProgressType(ProgressType determinate) { }
    void ShowProgress(float portion, float seconds) { }
    void SetProgress(float fraction) { }

    void ShowText(bool visible) { }
    bool IsTextVisible() { return false; }
    bool WasTextEverVisible() { return false; }
    void Print(const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
    }
    void PrintOnScreenOnly(const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
    }
    void ShowFile(const char*) { }

    void StartMenu(const char* const * headers, const char* const * items,
                           int initial_selection) { }
    int SelectMenu(int sel) { return 0; }
    void EndMenu() { }
};

void
ui_print(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    vfprintf(stdout, format, ap);
    va_end(ap);
}

class VerifierTest : public testing::TestWithParam<std::vector<std::string>> {
  public:
    MemMapping memmap;
    std::vector<Certificate> certs;

    virtual void SetUp() {
        std::vector<std::string> args = GetParam();
        std::string package =
            android::base::StringPrintf("%s%s%s%s", DATA_PATH, NATIVE_TEST_PATH,
                                        TESTDATA_PATH, args[0].c_str());
        if (sysMapFile(package.c_str(), &memmap) != 0) {
            FAIL() << "Failed to mmap " << package << ": " << strerror(errno)
                   << "\n";
        }

        for (auto it = ++(args.cbegin()); it != args.cend(); ++it) {
            if (it->substr(it->length() - 3, it->length()) == "256") {
                if (certs.empty()) {
                    FAIL() << "May only specify -sha256 after key type\n";
                }
                certs.back().hash_len = SHA256_DIGEST_LENGTH;
            } else {
                std::string public_key_file = android::base::StringPrintf(
                    "%s%s%stest_key_%s.txt", DATA_PATH, NATIVE_TEST_PATH,
                    TESTDATA_PATH, it->c_str());
                ASSERT_TRUE(load_keys(public_key_file.c_str(), certs));
                certs.back().hash_len = SHA_DIGEST_LENGTH;
            }
        }
        if (certs.empty()) {
            std::string public_key_file = android::base::StringPrintf(
                "%s%s%stest_key_e3.txt", DATA_PATH, NATIVE_TEST_PATH,
                TESTDATA_PATH);
            ASSERT_TRUE(load_keys(public_key_file.c_str(), certs));
            certs.back().hash_len = SHA_DIGEST_LENGTH;
        }
    }

    static void SetUpTestCase() {
        ui = new MockUI();
    }
};

class VerifierSuccessTest : public VerifierTest {
};

class VerifierFailureTest : public VerifierTest {
};

TEST_P(VerifierSuccessTest, VerifySucceed) {
    ASSERT_EQ(verify_file(memmap.addr, memmap.length, certs), VERIFY_SUCCESS);
}

TEST_P(VerifierFailureTest, VerifyFailure) {
    ASSERT_EQ(verify_file(memmap.addr, memmap.length, certs), VERIFY_FAILURE);
}

INSTANTIATE_TEST_CASE_P(SingleKeySuccess, VerifierSuccessTest,
        ::testing::Values(
            std::vector<std::string>({"otasigned.zip", "e3"}),
            std::vector<std::string>({"otasigned_f4.zip", "f4"}),
            std::vector<std::string>({"otasigned_sha256.zip", "e3", "sha256"}),
            std::vector<std::string>({"otasigned_f4_sha256.zip", "f4", "sha256"}),
            std::vector<std::string>({"otasigned_ecdsa_sha256.zip", "ec", "sha256"})));

INSTANTIATE_TEST_CASE_P(MultiKeySuccess, VerifierSuccessTest,
        ::testing::Values(
            std::vector<std::string>({"otasigned.zip", "f4", "e3"}),
            std::vector<std::string>({"otasigned_f4.zip", "ec", "f4"}),
            std::vector<std::string>({"otasigned_sha256.zip", "ec", "e3", "e3", "sha256"}),
            std::vector<std::string>({"otasigned_f4_sha256.zip", "ec", "sha256", "e3", "f4", "sha256"}),
            std::vector<std::string>({"otasigned_ecdsa_sha256.zip", "f4", "sha256", "e3", "ec", "sha256"})));

INSTANTIATE_TEST_CASE_P(WrongKey, VerifierFailureTest,
        ::testing::Values(
            std::vector<std::string>({"otasigned.zip", "f4"}),
            std::vector<std::string>({"otasigned_f4.zip", "e3"}),
            std::vector<std::string>({"otasigned_ecdsa_sha256.zip", "e3", "sha256"})));

INSTANTIATE_TEST_CASE_P(WrongHash, VerifierFailureTest,
        ::testing::Values(
            std::vector<std::string>({"otasigned.zip", "e3", "sha256"}),
            std::vector<std::string>({"otasigned_f4.zip", "f4", "sha256"}),
            std::vector<std::string>({"otasigned_sha256.zip"}),
            std::vector<std::string>({"otasigned_f4_sha256.zip", "f4"}),
            std::vector<std::string>({"otasigned_ecdsa_sha256.zip"})));

INSTANTIATE_TEST_CASE_P(BadPackage, VerifierFailureTest,
        ::testing::Values(
            std::vector<std::string>({"random.zip"}),
            std::vector<std::string>({"fake-eocd.zip"}),
            std::vector<std::string>({"alter-metadata.zip"}),
            std::vector<std::string>({"alter-footer.zip"})));
