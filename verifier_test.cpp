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

#include "common.h"
#include "verifier.h"
#include "ui.h"

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
        367251975, 810756730, -1941182952, 1175080310 },
      3
    };

RSAPublicKey test_f4_key =
    { 64, 0xc9bd1f21,
      { 293133087u, 3210546773u, 865313125u, 250921607u,
        3158780490u, 943703457u, 1242806226u, 2986289859u,
        2942743769u, 2457906415u, 2719374299u, 1783459420u,
        149579627u, 3081531591u, 3440738617u, 2788543742u,
        2758457512u, 1146764939u, 3699497403u, 2446203424u,
        1744968926u, 1159130537u, 2370028300u, 3978231572u,
        3392699980u, 1487782451u, 1180150567u, 2841334302u,
        3753960204u, 961373345u, 3333628321u, 748825784u,
        2978557276u, 1566596926u, 1613056060u, 2600292737u,
        1847226629u, 50398611u, 1890374404u, 2878700735u,
        2286201787u, 1401186359u, 619285059u, 731930817u,
        2340993166u, 1156490245u, 2992241729u, 151498140u,
        318782170u, 3480838990u, 2100383433u, 4223552555u,
        3628927011u, 4247846280u, 1759029513u, 4215632601u,
        2719154626u, 3490334597u, 1751299340u, 3487864726u,
        3668753795u, 4217506054u, 3748782284u, 3150295088u },
      { 1772626313u, 445326068u, 3477676155u, 1758201194u,
        2986784722u, 491035581u, 3922936562u, 702212696u,
        2979856666u, 3324974564u, 2488428922u, 3056318590u,
        1626954946u, 664714029u, 398585816u, 3964097931u,
        3356701905u, 2298377729u, 2040082097u, 3025491477u,
        539143308u, 3348777868u, 2995302452u, 3602465520u,
        212480763u, 2691021393u, 1307177300u, 704008044u,
        2031136606u, 1054106474u, 3838318865u, 2441343869u,
        1477566916u, 700949900u, 2534790355u, 3353533667u,
        336163563u, 4106790558u, 2701448228u, 1571536379u,
        1103842411u, 3623110423u, 1635278839u, 1577828979u,
        910322800u, 715583630u, 138128831u, 1017877531u,
        2289162787u, 447994798u, 1897243165u, 4121561445u,
        4150719842u, 2131821093u, 2262395396u, 3305771534u,
        980753571u, 3256525190u, 3128121808u, 1072869975u,
        3507939515u, 4229109952u, 118381341u, 2209831334u },
      65537
    };

RecoveryUI* ui = NULL;

// verifier expects to find a UI object; we provide one that does
// nothing but print.
class FakeUI : public RecoveryUI {
    void Init() { }
    void SetBackground(Icon icon) { }

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

int main(int argc, char **argv) {
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "Usage: %s [-f4 | -file <keys>] <package>\n", argv[0]);
        return 2;
    }

    RSAPublicKey* key = &test_key;
    int num_keys = 1;
    ++argv;
    if (strcmp(argv[0], "-f4") == 0) {
        ++argv;
        key = &test_f4_key;
    } else if (strcmp(argv[0], "-file") == 0) {
        ++argv;
        key = load_keys(argv[0], &num_keys);
        ++argv;
    }

    ui = new FakeUI();

    int result = verify_file(*argv, key, num_keys);
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
