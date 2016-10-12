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

#include <android-base/properties.h>
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

    char* result = Evaluate(&state, e);

    if (expected == nullptr) {
        EXPECT_EQ(nullptr, result);
    } else {
        EXPECT_STREQ(expected, result);
    }

    // Error code is set in updater/updater.cpp only, by parsing State.errmsg.
    EXPECT_EQ(kNoError, state.error_code);

    // Cause code should always be available.
    EXPECT_EQ(cause_code, state.cause_code);

    free(result);
}

class UpdaterTest : public ::testing::Test {
  protected:
    virtual void SetUp() {
        RegisterBuiltins();
        RegisterInstallFunctions();
        FinishRegistration();
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
