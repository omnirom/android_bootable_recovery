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

#include <string>

#include <android-base/file.h>

#include "expr.h"

static void ExprDump(int depth, const Expr* n, const std::string& script) {
    printf("%*s", depth*2, "");
    printf("%s %p (%d-%d) \"%s\"\n",
           n->name == NULL ? "(NULL)" : n->name, n->fn, n->start, n->end,
           script.substr(n->start, n->end - n->start).c_str());
    for (int i = 0; i < n->argc; ++i) {
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

    Expr* root;
    int error_count = 0;
    int error = parse_string(buffer.data(), &root, &error_count);
    printf("parse returned %d; %d errors encountered\n", error, error_count);
    if (error == 0 || error_count > 0) {

        ExprDump(0, root, buffer);

        State state(buffer, nullptr);
        char* result = Evaluate(&state, root);
        if (result == NULL) {
            printf("result was NULL, message is: %s\n",
                   (state.errmsg.empty() ? "(NULL)" : state.errmsg.c_str()));
        } else {
            printf("result is [%s]\n", result);
        }
    }
    return 0;
}
