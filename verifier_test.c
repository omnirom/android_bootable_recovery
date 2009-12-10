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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "verifier.h"

// This is build/target/product/security/testkey.x509.pem after being
// dumped out by dumpkey.jar.
RSAPublicKey test_key =
    { 64, 0xc926ad21,
      { 1795090719, 2141396315, 950055447, -1713398866,
        -26044131, 1920809988, 546586521, -795969498,
        1776797858, -554906482, 1805317999, 1429410244,
        129622599, 1422441418, 1783893377, 1222374759,
        -1731647369, 323993566, 28517732, 609753416,
        1826472888, 215237850, -33324596, -245884705,
        -1066504894, 774857746, 154822455, -1797768399,
        -1536767878, -1275951968, -1500189652, 87251430,
        -1760039318, 120774784, 571297800, -599067824,
        -1815042109, -483341846, -893134306, -1900097649,
        -1027721089, 950095497, 555058928, 414729973,
        1136544882, -1250377212, 465547824, -236820568,
        -1563171242, 1689838846, -404210357, 1048029507,
        895090649, 247140249, 178744550, -747082073,
        -1129788053, 109881576, -350362881, 1044303212,
        -522594267, -1309816990, -557446364, -695002876},
      { -857949815, -510492167, -1494742324, -1208744608,
        251333580, 2131931323, 512774938, 325948880,
        -1637480859, 2102694287, -474399070, 792812816,
        1026422502, 2053275343, -1494078096, -1181380486,
        165549746, -21447327, -229719404, 1902789247,
        772932719, -353118870, -642223187, 216871947,
        -1130566647, 1942378755, -298201445, 1055777370,
        964047799, 629391717, -2062222979, -384408304,
        191868569, -1536083459, -612150544, -1297252564,
        -1592438046, -724266841, -518093464, -370899750,
        -739277751, -1536141862, 1323144535, 61311905,
        1997411085, 376844204, 213777604, -217643712,
        9135381, 1625809335, -1490225159, -1342673351,
        1117190829, -57654514, 1825108855, -1281819325,
        1111251351, -1726129724, 1684324211, -1773988491,
        367251975, 810756730, -1941182952, 1175080310 }
    };

void ui_print(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, 256, fmt, ap);
    va_end(ap);

    fputs(buf, stderr);
}

void ui_set_progress(float fraction) {
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <package>\n", argv[0]);
        return 2;
    }

    int result = verify_file(argv[1], &test_key, 1);
    if (result == VERIFY_SUCCESS) {
        printf("SUCCESS\n");
        return 0;
    } else if (result == VERIFY_FAILURE) {
        printf("FAILURE\n");
        return 1;
    } else {
        printf("bad return value\n");
        return 3;
    }
}
