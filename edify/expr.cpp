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

#include "expr.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

// Functions should:
//
//    - return a malloc()'d string
//    - if Evaluate() on any argument returns nullptr, return nullptr.

static bool BooleanString(const std::string& s) {
    return !s.empty();
}

bool Evaluate(State* state, const std::unique_ptr<Expr>& expr, std::string* result) {
    if (result == nullptr) {
        return false;
    }

    std::unique_ptr<Value> v(expr->fn(expr->name.c_str(), state, expr->argv));
    if (!v) {
        return false;
    }
    if (v->type != VAL_STRING) {
        ErrorAbort(state, kArgsParsingFailure, "expecting string, got value type %d", v->type);
        return false;
    }

    *result = v->data;
    return true;
}

Value* EvaluateValue(State* state, const std::unique_ptr<Expr>& expr) {
    return expr->fn(expr->name.c_str(), state, expr->argv);
}

Value* StringValue(const char* str) {
    if (str == nullptr) {
        return nullptr;
    }
    return new Value(VAL_STRING, str);
}

Value* StringValue(const std::string& str) {
    return StringValue(str.c_str());
}

Value* ConcatFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
    if (argv.empty()) {
        return StringValue("");
    }
    std::string result;
    for (size_t i = 0; i < argv.size(); ++i) {
        std::string str;
        if (!Evaluate(state, argv[i], &str)) {
            return nullptr;
        }
        result += str;
    }

    return StringValue(result);
}

Value* IfElseFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
    if (argv.size() != 2 && argv.size() != 3) {
        state->errmsg = "ifelse expects 2 or 3 arguments";
        return nullptr;
    }

    std::string cond;
    if (!Evaluate(state, argv[0], &cond)) {
        return nullptr;
    }

    if (!cond.empty()) {
        return EvaluateValue(state, argv[1]);
    } else if (argv.size() == 3) {
        return EvaluateValue(state, argv[2]);
    }

    return StringValue("");
}

Value* AbortFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
    std::string msg;
    if (!argv.empty() && Evaluate(state, argv[0], &msg)) {
        state->errmsg = msg;
    } else {
        state->errmsg = "called abort()";
    }
    return nullptr;
}

Value* AssertFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
    for (size_t i = 0; i < argv.size(); ++i) {
        std::string result;
        if (!Evaluate(state, argv[i], &result)) {
            return nullptr;
        }
        if (result.empty()) {
            int len = argv[i]->end - argv[i]->start;
            state->errmsg = "assert failed: " + state->script.substr(argv[i]->start, len);
            return nullptr;
        }
    }
    return StringValue("");
}

Value* SleepFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
    std::string val;
    if (!Evaluate(state, argv[0], &val)) {
        return nullptr;
    }

    int v;
    if (!android::base::ParseInt(val.c_str(), &v, 0)) {
        return nullptr;
    }
    sleep(v);

    return StringValue(val);
}

Value* StdoutFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
    for (size_t i = 0; i < argv.size(); ++i) {
        std::string v;
        if (!Evaluate(state, argv[i], &v)) {
            return nullptr;
        }
        fputs(v.c_str(), stdout);
    }
    return StringValue("");
}

Value* LogicalAndFn(const char* name, State* state,
                    const std::vector<std::unique_ptr<Expr>>& argv) {
    std::string left;
    if (!Evaluate(state, argv[0], &left)) {
        return nullptr;
    }
    if (BooleanString(left)) {
        return EvaluateValue(state, argv[1]);
    } else {
        return StringValue("");
    }
}

Value* LogicalOrFn(const char* name, State* state,
                   const std::vector<std::unique_ptr<Expr>>& argv) {
    std::string left;
    if (!Evaluate(state, argv[0], &left)) {
        return nullptr;
    }
    if (!BooleanString(left)) {
        return EvaluateValue(state, argv[1]);
    } else {
        return StringValue(left);
    }
}

Value* LogicalNotFn(const char* name, State* state,
                    const std::vector<std::unique_ptr<Expr>>& argv) {
    std::string val;
    if (!Evaluate(state, argv[0], &val)) {
        return nullptr;
    }

    return StringValue(BooleanString(val) ? "" : "t");
}

Value* SubstringFn(const char* name, State* state,
                   const std::vector<std::unique_ptr<Expr>>& argv) {
    std::string needle;
    if (!Evaluate(state, argv[0], &needle)) {
        return nullptr;
    }

    std::string haystack;
    if (!Evaluate(state, argv[1], &haystack)) {
        return nullptr;
    }

    std::string result = (haystack.find(needle) != std::string::npos) ? "t" : "";
    return StringValue(result);
}

Value* EqualityFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
    std::string left;
    if (!Evaluate(state, argv[0], &left)) {
        return nullptr;
    }
    std::string right;
    if (!Evaluate(state, argv[1], &right)) {
        return nullptr;
    }

    const char* result = (left == right) ? "t" : "";
    return StringValue(result);
}

Value* InequalityFn(const char* name, State* state,
                    const std::vector<std::unique_ptr<Expr>>& argv) {
    std::string left;
    if (!Evaluate(state, argv[0], &left)) {
        return nullptr;
    }
    std::string right;
    if (!Evaluate(state, argv[1], &right)) {
        return nullptr;
    }

    const char* result = (left != right) ? "t" : "";
    return StringValue(result);
}

Value* SequenceFn(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
    std::unique_ptr<Value> left(EvaluateValue(state, argv[0]));
    if (!left) {
        return nullptr;
    }
    return EvaluateValue(state, argv[1]);
}

Value* LessThanIntFn(const char* name, State* state,
                     const std::vector<std::unique_ptr<Expr>>& argv) {
    if (argv.size() != 2) {
        state->errmsg = "less_than_int expects 2 arguments";
        return nullptr;
    }

    std::vector<std::string> args;
    if (!ReadArgs(state, argv, &args)) {
        return nullptr;
    }

    // Parse up to at least long long or 64-bit integers.
    int64_t l_int;
    if (!android::base::ParseInt(args[0].c_str(), &l_int)) {
        state->errmsg = "failed to parse int in " + args[0];
        return nullptr;
    }

    int64_t r_int;
    if (!android::base::ParseInt(args[1].c_str(), &r_int)) {
        state->errmsg = "failed to parse int in " + args[1];
        return nullptr;
    }

    return StringValue(l_int < r_int ? "t" : "");
}

Value* GreaterThanIntFn(const char* name, State* state,
                        const std::vector<std::unique_ptr<Expr>>& argv) {
    if (argv.size() != 2) {
        state->errmsg = "greater_than_int expects 2 arguments";
        return nullptr;
    }

    std::vector<std::string> args;
    if (!ReadArgs(state, argv, &args)) {
        return nullptr;
    }

    // Parse up to at least long long or 64-bit integers.
    int64_t l_int;
    if (!android::base::ParseInt(args[0].c_str(), &l_int)) {
        state->errmsg = "failed to parse int in " + args[0];
        return nullptr;
    }

    int64_t r_int;
    if (!android::base::ParseInt(args[1].c_str(), &r_int)) {
        state->errmsg = "failed to parse int in " + args[1];
        return nullptr;
    }

    return StringValue(l_int > r_int ? "t" : "");
}

Value* Literal(const char* name, State* state, const std::vector<std::unique_ptr<Expr>>& argv) {
    return StringValue(name);
}

// -----------------------------------------------------------------
//   the function table
// -----------------------------------------------------------------

static std::unordered_map<std::string, Function> fn_table;

void RegisterFunction(const std::string& name, Function fn) {
    fn_table[name] = fn;
}

Function FindFunction(const std::string& name) {
    if (fn_table.find(name) == fn_table.end()) {
        return nullptr;
    } else {
        return fn_table[name];
    }
}

void RegisterBuiltins() {
    RegisterFunction("ifelse", IfElseFn);
    RegisterFunction("abort", AbortFn);
    RegisterFunction("assert", AssertFn);
    RegisterFunction("concat", ConcatFn);
    RegisterFunction("is_substring", SubstringFn);
    RegisterFunction("stdout", StdoutFn);
    RegisterFunction("sleep", SleepFn);

    RegisterFunction("less_than_int", LessThanIntFn);
    RegisterFunction("greater_than_int", GreaterThanIntFn);
}


// -----------------------------------------------------------------
//   convenience methods for functions
// -----------------------------------------------------------------

// Evaluate the expressions in argv, and put the results of strings in args. If any expression
// evaluates to nullptr, return false. Return true on success.
bool ReadArgs(State* state, const std::vector<std::unique_ptr<Expr>>& argv,
              std::vector<std::string>* args) {
    return ReadArgs(state, argv, args, 0, argv.size());
}

bool ReadArgs(State* state, const std::vector<std::unique_ptr<Expr>>& argv,
              std::vector<std::string>* args, size_t start, size_t len) {
    if (args == nullptr) {
        return false;
    }
    if (start + len > argv.size()) {
        return false;
    }
    for (size_t i = start; i < start + len; ++i) {
        std::string var;
        if (!Evaluate(state, argv[i], &var)) {
            args->clear();
            return false;
        }
        args->push_back(var);
    }
    return true;
}

// Evaluate the expressions in argv, and put the results of Value* in args. If any expression
// evaluate to nullptr, return false. Return true on success.
bool ReadValueArgs(State* state, const std::vector<std::unique_ptr<Expr>>& argv,
                   std::vector<std::unique_ptr<Value>>* args) {
    return ReadValueArgs(state, argv, args, 0, argv.size());
}

bool ReadValueArgs(State* state, const std::vector<std::unique_ptr<Expr>>& argv,
                   std::vector<std::unique_ptr<Value>>* args, size_t start, size_t len) {
    if (args == nullptr) {
        return false;
    }
    if (len == 0 || start + len > argv.size()) {
        return false;
    }
    for (size_t i = start; i < start + len; ++i) {
        std::unique_ptr<Value> v(EvaluateValue(state, argv[i]));
        if (!v) {
            args->clear();
            return false;
        }
        args->push_back(std::move(v));
    }
    return true;
}

// Use printf-style arguments to compose an error message to put into
// *state.  Returns nullptr.
Value* ErrorAbort(State* state, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    android::base::StringAppendV(&state->errmsg, format, ap);
    va_end(ap);
    return nullptr;
}

Value* ErrorAbort(State* state, CauseCode cause_code, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    android::base::StringAppendV(&state->errmsg, format, ap);
    va_end(ap);
    state->cause_code = cause_code;
    return nullptr;
}

State::State(const std::string& script, void* cookie) :
    script(script),
    cookie(cookie) {
}

