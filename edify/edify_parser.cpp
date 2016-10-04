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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expr.h"
#include "parser.h"

void ExprDump(int depth, Expr* n, char* script) {
    printf("%*s", depth*2, "");
    char temp = script[n->end];
    script[n->end] = '\0';
    printf("%s %p (%d-%d) \"%s\"\n",
           n->name == NULL ? "(NULL)" : n->name, n->fn, n->start, n->end,
           script+n->start);
    script[n->end] = temp;
    for (int i = 0; i < n->argc; ++i) {
        ExprDump(depth+1, n->argv[i], script);
    }
}

int main(int argc, char** argv) {
    RegisterBuiltins();
    FinishRegistration();

    if (argc != 2) {
        printf("Usage: %s <edify script>\n", argv[0]);
        return 1;
    }

    FILE* f = fopen(argv[1], "r");
    if (f == NULL) {
        printf("%s: %s: No such file or directory\n", argv[0], argv[1]);
        return 1;
    }
    char buffer[8192];
    int size = fread(buffer, 1, 8191, f);
    fclose(f);
    buffer[size] = '\0';

    Expr* root;
    int error_count = 0;
    int error = parse_string(buffer, &root, &error_count);
    printf("parse returned %d; %d errors encountered\n", error, error_count);
    if (error == 0 || error_count > 0) {

        ExprDump(0, root, buffer);

        State state;
        state.cookie = NULL;
        state.script = buffer;
        state.errmsg = NULL;

        char* result = Evaluate(&state, root);
        if (result == NULL) {
            printf("result was NULL, message is: %s\n",
                   (state.errmsg == NULL ? "(NULL)" : state.errmsg));
            free(state.errmsg);
        } else {
            printf("result is [%s]\n", result);
        }
    }
    return 0;
}
