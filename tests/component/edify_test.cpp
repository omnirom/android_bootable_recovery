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

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "edify/expr.h"

static void expect(const char* expr_str, const char* expected) {
    std::unique_ptr<Expr> e;
    int error_count = 0;
    EXPECT_EQ(0, parse_string(expr_str, &e, &error_count));
    EXPECT_EQ(0, error_count);

    State state(expr_str, nullptr);

    std::string result;
    bool status = Evaluate(&state, e, &result);

    if (expected == nullptr) {
        EXPECT_FALSE(status);
    } else {
        EXPECT_STREQ(expected, result.c_str());
    }

}

class EdifyTest : public ::testing::Test {
  protected:
    virtual void SetUp() {
        RegisterBuiltins();
    }
};

TEST_F(EdifyTest, parsing) {
    expect("a", "a");
    expect("\"a\"", "a");
    expect("\"\\x61\"", "a");
    expect("# this is a comment\n"
           "  a\n"
           "   \n",
           "a");
}

TEST_F(EdifyTest, sequence) {
    // sequence operator
    expect("a; b; c", "c");
}

TEST_F(EdifyTest, concat) {
    // string concat operator
    expect("a + b", "ab");
    expect("a + \n \"b\"", "ab");
    expect("a + b +\nc\n", "abc");

    // string concat function
    expect("concat(a, b)", "ab");
    expect("concat(a,\n \"b\")", "ab");
    expect("concat(a + b,\nc,\"d\")", "abcd");
    expect("\"concat\"(a + b,\nc,\"d\")", "abcd");
}

TEST_F(EdifyTest, logical) {
    // logical and
    expect("a && b", "b");
    expect("a && \"\"", "");
    expect("\"\" && b", "");
    expect("\"\" && \"\"", "");
    expect("\"\" && abort()", "");   // test short-circuiting
    expect("t && abort()", nullptr);

    // logical or
    expect("a || b", "a");
    expect("a || \"\"", "a");
    expect("\"\" || b", "b");
    expect("\"\" || \"\"", "");
    expect("a || abort()", "a");     // test short-circuiting
    expect("\"\" || abort()", NULL);

    // logical not
    expect("!a", "");
    expect("! \"\"", "t");
    expect("!!a", "t");
}

TEST_F(EdifyTest, precedence) {
    // precedence
    expect("\"\" == \"\" && b", "b");
    expect("a + b == ab", "t");
    expect("ab == a + b", "t");
    expect("a + (b == ab)", "a");
    expect("(ab == a) + b", "b");
}

TEST_F(EdifyTest, substring) {
    // substring function
    expect("is_substring(cad, abracadabra)", "t");
    expect("is_substring(abrac, abracadabra)", "t");
    expect("is_substring(dabra, abracadabra)", "t");
    expect("is_substring(cad, abracxadabra)", "");
    expect("is_substring(abrac, axbracadabra)", "");
    expect("is_substring(dabra, abracadabrxa)", "");
}

TEST_F(EdifyTest, ifelse) {
    // ifelse function
    expect("ifelse(t, yes, no)", "yes");
    expect("ifelse(!t, yes, no)", "no");
    expect("ifelse(t, yes, abort())", "yes");
    expect("ifelse(!t, abort(), no)", "no");
}

TEST_F(EdifyTest, if_statement) {
    // if "statements"
    expect("if t then yes else no endif", "yes");
    expect("if \"\" then yes else no endif", "no");
    expect("if \"\" then yes endif", "");
    expect("if \"\"; t then yes endif", "yes");
}

TEST_F(EdifyTest, comparison) {
    // numeric comparisons
    expect("less_than_int(3, 14)", "t");
    expect("less_than_int(14, 3)", "");
    expect("less_than_int(x, 3)", "");
    expect("less_than_int(3, x)", "");
    expect("greater_than_int(3, 14)", "");
    expect("greater_than_int(14, 3)", "t");
    expect("greater_than_int(x, 3)", "");
    expect("greater_than_int(3, x)", "");
}

TEST_F(EdifyTest, big_string) {
    // big string
    expect(std::string(8192, 's').c_str(), std::string(8192, 's').c_str());
}

TEST_F(EdifyTest, unknown_function) {
    // unknown function
    const char* script1 = "unknown_function()";
    std::unique_ptr<Expr> expr;
    int error_count = 0;
    EXPECT_EQ(1, parse_string(script1, &expr, &error_count));
    EXPECT_EQ(1, error_count);

    const char* script2 = "abc; unknown_function()";
    error_count = 0;
    EXPECT_EQ(1, parse_string(script2, &expr, &error_count));
    EXPECT_EQ(1, error_count);

    const char* script3 = "unknown_function1() || yes";
    error_count = 0;
    EXPECT_EQ(1, parse_string(script3, &expr, &error_count));
    EXPECT_EQ(1, error_count);
}
