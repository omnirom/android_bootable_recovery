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

/**
 * This is a host-side tool for validating a given edify script file.
 *
 * We used to have edify test cases here, which have been moved to
 * tests/component/edify_test.cpp.
 *
 * Caveat: It doesn't recognize functions defined through updater, which
 * makes the tool less useful. We should either extend the tool or remove it.
 */

#include <errno.h>
#include <stdio.h>

#include <memory>
#include <string>

#include <android-base/file.h>

#include "expr.h"

static void ExprDump(int depth, const std::unique_ptr<Expr>& n, const std::string& script) {
    printf("%*s", depth*2, "");
    printf("%s %p (%d-%d) \"%s\"\n",
           n->name.c_str(), n->fn, n->start, n->end,
           script.substr(n->start, n->end - n->start).c_str());
    for (size_t i = 0; i < n->argv.size(); ++i) {
        ExprDump(depth+1, n->argv[i], script);
    }
}

int main(int argc, char** argv) {
    RegisterBuiltins();

    if (argc != 2) {
        printf("Usage: %s <edify script>\n", argv[0]);
        return 1;
    }

    std::string buffer;
    if (!android::base::ReadFileToString(argv[1], &buffer)) {
        printf("%s: failed to read %s: %s\n", argv[0], argv[1], strerror(errno));
        return 1;
    }

    std::unique_ptr<Expr> root;
    int error_count = 0;
    int error = parse_string(buffer.data(), &root, &error_count);
    printf("parse returned %d; %d errors encountered\n", error, error_count);
    if (error == 0 || error_count > 0) {

        ExprDump(0, root, buffer);

        State state(buffer, nullptr);
        std::string result;
        if (!Evaluate(&state, root, &result)) {
            printf("result was NULL, message is: %s\n",
                   (state.errmsg.empty() ? "(NULL)" : state.errmsg.c_str()));
        } else {
            printf("result is [%s]\n", result.c_str());
        }
    }
    return 0;
}
