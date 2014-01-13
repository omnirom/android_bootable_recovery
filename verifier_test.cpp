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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "common.h"
#include "verifier.h"
#include "ui.h"
#include "mincrypt/sha.h"
#include "mincrypt/sha256.h"
#include "minzip/SysUtil.h"

// This is build/target/product/security/testkey.x509.pem after being
// dumped out by dumpkey.jar.
RSAPublicKey test_key =
    { 64, 0xc926ad21,
      { 0x6afee91fu, 0x7fa31d5bu, 0x38a0b217u, 0x99df9baeu,
        0xfe72991du, 0x727d3c04u, 0x20943f99u, 0xd08e7826u,
        0x69e7c8a2u, 0xdeeccc8eu, 0x6b9af76fu, 0x553311c4u,
        0x07b9e247u, 0x54c8bbcau, 0x6a540d81u, 0x48dbf567u,
        0x98c92877u, 0x134fbfdeu, 0x01b32564u, 0x24581948u,
        0x6cddc3b8u, 0x0cd444dau, 0xfe0381ccu, 0xf15818dfu,
        0xc06e6d42u, 0x2e2f6412u, 0x093a6737u, 0x94d83b31u,
        0xa466c87au, 0xb3f284a0u, 0xa694ec2cu, 0x053359e6u,
        0x9717ee6au, 0x0732e080u, 0x220d5008u, 0xdc4af350u,
        0x93d0a7c3u, 0xe330c9eau, 0xcac3da1eu, 0x8ebecf8fu,
        0xc2be387fu, 0x38a14e89u, 0x211586f0u, 0x18b846f5u,
        0x43be4c72u, 0xb578c204u, 0x1bbfb230u, 0xf1e267a8u,
        0xa2d3e656u, 0x64b8e4feu, 0xe7e83d4bu, 0x3e77a943u,
        0x3559ffd9u, 0x0ebb0f99u, 0x0aa76ce6u, 0xd3786ea7u,
        0xbca8cd6bu, 0x068ca8e8u, 0xeb1de2ffu, 0x3e3ecd6cu,
        0xe0d9d825u, 0xb1edc762u, 0xdec60b24u, 0xd6931904u},
      { 0xccdcb989u, 0xe19281f9u, 0xa6e80accu, 0xb7f40560u,
        0x0efb0bccu, 0x7f12b0bbu, 0x1e90531au, 0x136d95d0u,
        0x9e660665u, 0x7d54918fu, 0xe3b93ea2u, 0x2f415d10u,
        0x3d2df6e6u, 0x7a627ecfu, 0xa6f22d70u, 0xb995907au,
        0x09de16b2u, 0xfeb8bd61u, 0xf24ec294u, 0x716a427fu,
        0x2e12046fu, 0xeaf3d56au, 0xd9b873adu, 0x0ced340bu,
        0xbc9cec09u, 0x73c65903u, 0xee39ce9bu, 0x3eede25au,
        0x397633b7u, 0x2583c165u, 0x8514f97du, 0xe9166510u,
        0x0b6fae99u, 0xa47139fdu, 0xdb8352f0u, 0xb2ad7f2cu,
        0xa11552e2u, 0xd4d490a7u, 0xe11e8568u, 0xe9e484dau,
        0xd3ef8449u, 0xa47055dau, 0x4edd9557u, 0x03a78ba1u,
        0x770e130du, 0x16762facu, 0x0cbdfcc4u, 0xf3070540u,
        0x008b6515u, 0x60e7e1b7u, 0xa72cf7f9u, 0xaff86e39u,
        0x4296faadu, 0xfc90430eu, 0x6cc8f377u, 0xb398fd43u,
        0x423c5997u, 0x991d59c4u, 0x6464bf73u, 0x96431575u,
        0x15e3d207u, 0x30532a7au, 0x8c4be618u, 0x460a4d76u },
      3
    };

RSAPublicKey test_f4_key =
    { 64, 0xc9bd1f21,
      { 0x1178db1fu, 0xbf5d0e55u, 0x3393a165u, 0x0ef4c287u,
        0xbc472a4au, 0x383fc5a1u, 0x4a13b7d2u, 0xb1ff2ac3u,
        0xaf66b4d9u, 0x9280acefu, 0xa2165bdbu, 0x6a4d6e5cu,
        0x08ea676bu, 0xb7ac70c7u, 0xcd158139u, 0xa635ccfeu,
        0xa46ab8a8u, 0x445a3e8bu, 0xdc81d9bbu, 0x91ce1a20u,
        0x68021cdeu, 0x4516eda9u, 0x8d43c30cu, 0xed1eff14u,
        0xca387e4cu, 0x58adc233u, 0x4657ab27u, 0xa95b521eu,
        0xdfc0e30cu, 0x394d64a1u, 0xc6b321a1u, 0x2ca22cb8u,
        0xb1892d5cu, 0x5d605f3eu, 0x6025483cu, 0x9afd5181u,
        0x6e1a7105u, 0x03010593u, 0x70acd304u, 0xab957cbfu,
        0x8844abbbu, 0x53846837u, 0x24e98a43u, 0x2ba060c1u,
        0x8b88b88eu, 0x44eea405u, 0xb259fc41u, 0x0907ad9cu,
        0x13003adau, 0xcf79634eu, 0x7d314ec9u, 0xfbbe4c2bu,
        0xd84d0823u, 0xfd30fd88u, 0x68d8a909u, 0xfb4572d9u,
        0xa21301c2u, 0xd00a4785u, 0x6862b50cu, 0xcfe49796u,
        0xdaacbd83u, 0xfb620906u, 0xdf71e0ccu, 0xbbc5b030u },
      { 0x69a82189u, 0x1a8b22f4u, 0xcf49207bu, 0x68cc056au,
        0xb206b7d2u, 0x1d449bbdu, 0xe9d342f2u, 0x29daea58u,
        0xb19d011au, 0xc62f15e4u, 0x9452697au, 0xb62bb87eu,
        0x60f95cc2u, 0x279ebb2du, 0x17c1efd8u, 0xec47558bu,
        0xc81334d1u, 0x88fe7601u, 0x79992eb1u, 0xb4555615u,
        0x2022ac8cu, 0xc79a4b8cu, 0xb288b034u, 0xd6b942f0u,
        0x0caa32fbu, 0xa065ba51u, 0x4de9f154u, 0x29f64f6cu,
        0x7910af5eu, 0x3ed4636au, 0xe4c81911u, 0x9183f37du,
        0x5811e1c4u, 0x29c7a58cu, 0x9715d4d3u, 0xc7e2dce3u,
        0x140972ebu, 0xf4c8a69eu, 0xa104d424u, 0x5dabbdfbu,
        0x41cb4c6bu, 0xd7f44717u, 0x61785ff7u, 0x5e0bc273u,
        0x36426c70u, 0x2aa6f08eu, 0x083badbfu, 0x3cab941bu,
        0x8871da23u, 0x1ab3dbaeu, 0x7115a21du, 0xf5aa0965u,
        0xf766f562u, 0x7f110225u, 0x86d96a04u, 0xc50a120eu,
        0x3a751ca3u, 0xc21aa186u, 0xba7359d0u, 0x3ff2b257u,
        0xd116e8bbu, 0xfc1318c0u, 0x070e5b1du, 0x83b759a6u },
      65537
    };

ECPublicKey test_ec_key =
    {
       {
         {0xd656fa24u, 0x931416cau, 0x1c0278c6u, 0x174ebe4cu,
          0x6018236au, 0x45ba1656u, 0xe8c05d84u, 0x670ed500u}
      },
      {
        {0x0d179adeu, 0x4c16827du, 0x9f8cb992u, 0x8f69ff8au,
         0x481b1020u, 0x798d91afu, 0x184db8e9u, 0xb5848dd9u}
      }
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

static Certificate* add_certificate(Certificate** certsp, int* num_keys,
        Certificate::KeyType key_type) {
    int i = *num_keys;
    *num_keys = *num_keys + 1;
    *certsp = (Certificate*) realloc(*certsp, *num_keys * sizeof(Certificate));
    Certificate* certs = *certsp;
    certs[i].rsa = NULL;
    certs[i].ec = NULL;
    certs[i].key_type = key_type;
    certs[i].hash_len = SHA_DIGEST_SIZE;
    return &certs[i];
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [-sha256] [-ec | -f4 | -file <keys>] <package>\n", argv[0]);
        return 2;
    }
    Certificate* certs = NULL;
    int num_keys = 0;

    int argn = 1;
    while (argn < argc) {
        if (strcmp(argv[argn], "-sha256") == 0) {
            if (num_keys == 0) {
                fprintf(stderr, "May only specify -sha256 after key type\n");
                return 2;
            }
            ++argn;
            Certificate* cert = &certs[num_keys - 1];
            cert->hash_len = SHA256_DIGEST_SIZE;
        } else if (strcmp(argv[argn], "-ec") == 0) {
            ++argn;
            Certificate* cert = add_certificate(&certs, &num_keys, Certificate::EC);
            cert->ec = &test_ec_key;
        } else if (strcmp(argv[argn], "-e3") == 0) {
            ++argn;
            Certificate* cert = add_certificate(&certs, &num_keys, Certificate::RSA);
            cert->rsa = &test_key;
        } else if (strcmp(argv[argn], "-f4") == 0) {
            ++argn;
            Certificate* cert = add_certificate(&certs, &num_keys, Certificate::RSA);
            cert->rsa = &test_f4_key;
        } else if (strcmp(argv[argn], "-file") == 0) {
            if (certs != NULL) {
                fprintf(stderr, "Cannot specify -file with other certs specified\n");
                return 2;
            }
            ++argn;
            certs = load_keys(argv[argn], &num_keys);
            ++argn;
        } else if (argv[argn][0] == '-') {
            fprintf(stderr, "Unknown argument %s\n", argv[argn]);
            return 2;
        } else {
            break;
        }
    }

    if (argn == argc) {
        fprintf(stderr, "Must specify package to verify\n");
        return 2;
    }

    if (num_keys == 0) {
        certs = (Certificate*) calloc(1, sizeof(Certificate));
        if (certs == NULL) {
            fprintf(stderr, "Failure allocating memory for default certificate\n");
            return 1;
        }
        certs->key_type = Certificate::RSA;
        certs->rsa = &test_key;
        certs->ec = NULL;
        certs->hash_len = SHA_DIGEST_SIZE;
        num_keys = 1;
    }

    ui = new FakeUI();

    MemMapping map;
    if (sysMapFile(argv[argn], &map) != 0) {
        fprintf(stderr, "failed to mmap %s: %s\n", argv[argn], strerror(errno));
        return 4;
    }

    int result = verify_file(map.addr, map.length, certs, num_keys);
    if (result == VERIFY_SUCCESS) {
        printf("VERIFIED\n");
        return 0;
    } else if (result == VERIFY_FAILURE) {
        printf("NOT VERIFIED\n");
        return 1;
    } else {
        printf("bad return value\n");
        return 3;
    }
}
