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

static void expect(const char* expected, const char* expr_str,
                   ErrorCode error_code, CauseCode cause_code) {
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

    EXPECT_EQ(error_code, state.error_code);
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
           kNoError, kNoCause);

    expect(android::base::GetProperty("ro.build.fingerprint", "").c_str(),
           "getprop(\"ro.build.fingerprint\")",
           kNoError, kNoCause);

    // getprop() accepts only one parameter.
    expect(nullptr, "getprop()", kNoError, kArgsParsingFailure);
    expect(nullptr, "getprop(\"arg1\", \"arg2\")", kNoError, kArgsParsingFailure);
}
