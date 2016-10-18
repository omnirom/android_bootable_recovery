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

bool Evaluate(State* state, Expr* expr, std::string* result) {
    if (result == nullptr) {
        return false;
    }

    std::unique_ptr<Value> v(expr->fn(expr->name, state, expr->argc, expr->argv));
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

Value* EvaluateValue(State* state, Expr* expr) {
    return expr->fn(expr->name, state, expr->argc, expr->argv);
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

Value* ConcatFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc == 0) {
        return StringValue("");
    }
    std::string result;
    for (int i = 0; i < argc; ++i) {
        std::string str;
        if (!Evaluate(state, argv[i], &str)) {
            return nullptr;
        }
        result += str;
    }

    return StringValue(result);
}

Value* IfElseFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc != 2 && argc != 3) {
        state->errmsg = "ifelse expects 2 or 3 arguments";
        return nullptr;
    }

    std::string cond;
    if (!Evaluate(state, argv[0], &cond)) {
        return nullptr;
    }

    if (!cond.empty()) {
        return EvaluateValue(state, argv[1]);
    } else if (argc == 3) {
        return EvaluateValue(state, argv[2]);
    }

    return StringValue("");
}

Value* AbortFn(const char* name, State* state, int argc, Expr* argv[]) {
    std::string msg;
    if (argc > 0 && Evaluate(state, argv[0], &msg)) {
        state->errmsg = msg;
    } else {
        state->errmsg = "called abort()";
    }
    return nullptr;
}

Value* AssertFn(const char* name, State* state, int argc, Expr* argv[]) {
    for (int i = 0; i < argc; ++i) {
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

Value* SleepFn(const char* name, State* state, int argc, Expr* argv[]) {
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

Value* StdoutFn(const char* name, State* state, int argc, Expr* argv[]) {
    for (int i = 0; i < argc; ++i) {
        std::string v;
        if (!Evaluate(state, argv[i], &v)) {
            return nullptr;
        }
        fputs(v.c_str(), stdout);
    }
    return StringValue("");
}

Value* LogicalAndFn(const char* name, State* state,
                   int argc, Expr* argv[]) {
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
                   int argc, Expr* argv[]) {
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
                    int argc, Expr* argv[]) {
    std::string val;
    if (!Evaluate(state, argv[0], &val)) {
        return nullptr;
    }

    return StringValue(BooleanString(val) ? "" : "t");
}

Value* SubstringFn(const char* name, State* state,
                   int argc, Expr* argv[]) {
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

Value* EqualityFn(const char* name, State* state, int argc, Expr* argv[]) {
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

Value* InequalityFn(const char* name, State* state, int argc, Expr* argv[]) {
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

Value* SequenceFn(const char* name, State* state, int argc, Expr* argv[]) {
    std::unique_ptr<Value> left(EvaluateValue(state, argv[0]));
    if (!left) {
        return nullptr;
    }
    return EvaluateValue(state, argv[1]);
}

Value* LessThanIntFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc != 2) {
        state->errmsg = "less_than_int expects 2 arguments";
        return nullptr;
    }

    char* left;
    char* right;
    if (ReadArgs(state, argv, 2, &left, &right) < 0) return nullptr;

    bool result = false;
    char* end;

    // Parse up to at least long long or 64-bit integers.
    int64_t l_int = static_cast<int64_t>(strtoll(left, &end, 10));
    if (left[0] == '\0' || *end != '\0') {
        goto done;
    }

    int64_t r_int;
    r_int = static_cast<int64_t>(strtoll(right, &end, 10));
    if (right[0] == '\0' || *end != '\0') {
        goto done;
    }

    result = l_int < r_int;

  done:
    free(left);
    free(right);
    return StringValue(result ? "t" : "");
}

Value* GreaterThanIntFn(const char* name, State* state,
                        int argc, Expr* argv[]) {
    if (argc != 2) {
        state->errmsg = "greater_than_int expects 2 arguments";
        return nullptr;
    }

    Expr* temp[2];
    temp[0] = argv[1];
    temp[1] = argv[0];

    return LessThanIntFn(name, state, 2, temp);
}

Value* Literal(const char* name, State* state, int argc, Expr* argv[]) {
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

// Evaluate the expressions in argv, and put the results of strings in
// args. If any expression evaluates to nullptr, free the rest and return
// false. Return true on success.
bool ReadArgs(State* state, int argc, Expr* argv[], std::vector<std::string>* args) {
    if (args == nullptr) {
        return false;
    }
    for (int i = 0; i < argc; ++i) {
        std::string var;
        if (!Evaluate(state, argv[i], &var)) {
            args->clear();
            return false;
        }
        args->push_back(var);
    }
    return true;
}

// Evaluate the expressions in argv, and put the results of Value* in
// args. If any expression evaluate to nullptr, free the rest and return
// false. Return true on success.
bool ReadValueArgs(State* state, int argc, Expr* argv[],
                   std::vector<std::unique_ptr<Value>>* args) {
    if (args == nullptr) {
        return false;
    }
    for (int i = 0; i < argc; ++i) {
        std::unique_ptr<Value> v(EvaluateValue(state, argv[i]));
        if (!v) {
            args->clear();
            return false;
        }
        args->push_back(std::move(v));
    }
    return true;
}

// Evaluate the expressions in argv, giving 'count' char* (the ... is
// zero or more char** to put them in).  If any expression evaluates
// to NULL, free the rest and return -1.  Return 0 on success.
int ReadArgs(State* state, Expr* argv[], int count, ...) {
    char** args = reinterpret_cast<char**>(malloc(count * sizeof(char*)));
    va_list v;
    va_start(v, count);
    int i;
    for (i = 0; i < count; ++i) {
        std::string str;
        if (!Evaluate(state, argv[i], &str) ||
                (args[i] = strdup(str.c_str())) == nullptr) {
            va_end(v);
            int j;
            for (j = 0; j < i; ++j) {
                free(args[j]);
            }
            free(args);
            return -1;
        }
        *(va_arg(v, char**)) = args[i];
    }
    va_end(v);
    free(args);
    return 0;
}

// Evaluate the expressions in argv, giving 'count' Value* (the ... is
// zero or more Value** to put them in).  If any expression evaluates
// to NULL, free the rest and return -1.  Return 0 on success.
int ReadValueArgs(State* state, Expr* argv[], int count, ...) {
    Value** args = new Value*[count];
    va_list v;
    va_start(v, count);
    for (int i = 0; i < count; ++i) {
        args[i] = EvaluateValue(state, argv[i]);
        if (args[i] == NULL) {
            va_end(v);
            int j;
            for (j = 0; j < i; ++j) {
                delete args[j];
            }
            delete[] args;
            return -1;
        }
        *(va_arg(v, Value**)) = args[i];
    }
    va_end(v);
    delete[] args;
    return 0;
}

// Evaluate the expressions in argv, returning an array of char*
// results.  If any evaluate to NULL, free the rest and return NULL.
// The caller is responsible for freeing the returned array and the
// strings it contains.
char** ReadVarArgs(State* state, int argc, Expr* argv[]) {
    char** args = (char**)malloc(argc * sizeof(char*));
    for (int i = 0; i < argc; ++i) {
        std::string str;
        if (!Evaluate(state, argv[i], &str) ||
                (args[i] = strdup(str.c_str())) == nullptr) {
            for (int j = 0; j < i; ++j) {
                free(args[j]);
            }
            free(args);
            return NULL;
        }
    }
    return args;
}

// Evaluate the expressions in argv, returning an array of Value*
// results.  If any evaluate to NULL, free the rest and return NULL.
// The caller is responsible for freeing the returned array and the
// Values it contains.
Value** ReadValueVarArgs(State* state, int argc, Expr* argv[]) {
    Value** args = new Value*[argc];
    int i = 0;
    for (i = 0; i < argc; ++i) {
        args[i] = EvaluateValue(state, argv[i]);
        if (args[i] == NULL) {
            int j;
            for (j = 0; j < i; ++j) {
                delete args[j];
            }
            delete[] args;
            return NULL;
        }
    }
    return args;
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

