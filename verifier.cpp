/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include "common.h"
#include "verifier.h"
#include "ui.h"

#include "mincrypt/rsa.h"
#include "mincrypt/sha.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>

extern RecoveryUI* ui;

// Look for an RSA signature embedded in the .ZIP file comment given
// the path to the zip.  Verify it matches one of the given public
// keys.
//
// Return VERIFY_SUCCESS, VERIFY_FAILURE (if any error is encountered
// or no key matches the signature).

int verify_file(const char* path, const RSAPublicKey *pKeys, unsigned int numKeys) {
    ui->SetProgress(0.0);

    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        LOGE("failed to open %s (%s)\n", path, strerror(errno));
        return VERIFY_FAILURE;
    }

    // An archive with a whole-file signature will end in six bytes:
    //
    //   (2-byte signature start) $ff $ff (2-byte comment size)
    //
    // (As far as the ZIP format is concerned, these are part of the
    // archive comment.)  We start by reading this footer, this tells
    // us how far back from the end we have to start reading to find
    // the whole comment.

#define FOOTER_SIZE 6

    if (fseek(f, -FOOTER_SIZE, SEEK_END) != 0) {
        LOGE("failed to seek in %s (%s)\n", path, strerror(errno));
        fclose(f);
        return VERIFY_FAILURE;
    }

    unsigned char footer[FOOTER_SIZE];
    if (fread(footer, 1, FOOTER_SIZE, f) != FOOTER_SIZE) {
        LOGE("failed to read footer from %s (%s)\n", path, strerror(errno));
        fclose(f);
        return VERIFY_FAILURE;
    }

    if (footer[2] != 0xff || footer[3] != 0xff) {
        fclose(f);
        return VERIFY_FAILURE;
    }

    size_t comment_size = footer[4] + (footer[5] << 8);
    size_t signature_start = footer[0] + (footer[1] << 8);
    LOGI("comment is %d bytes; signature %d bytes from end\n",
         comment_size, signature_start);

    if (signature_start - FOOTER_SIZE < RSANUMBYTES) {
        // "signature" block isn't big enough to contain an RSA block.
        LOGE("signature is too short\n");
        fclose(f);
        return VERIFY_FAILURE;
    }

#define EOCD_HEADER_SIZE 22

    // The end-of-central-directory record is 22 bytes plus any
    // comment length.
    size_t eocd_size = comment_size + EOCD_HEADER_SIZE;

    if (fseek(f, -eocd_size, SEEK_END) != 0) {
        LOGE("failed to seek in %s (%s)\n", path, strerror(errno));
        fclose(f);
        return VERIFY_FAILURE;
    }

    // Determine how much of the file is covered by the signature.
    // This is everything except the signature data and length, which
    // includes all of the EOCD except for the comment length field (2
    // bytes) and the comment data.
    size_t signed_len = ftell(f) + EOCD_HEADER_SIZE - 2;

    unsigned char* eocd = (unsigned char*)malloc(eocd_size);
    if (eocd == NULL) {
        LOGE("malloc for EOCD record failed\n");
        fclose(f);
        return VERIFY_FAILURE;
    }
    if (fread(eocd, 1, eocd_size, f) != eocd_size) {
        LOGE("failed to read eocd from %s (%s)\n", path, strerror(errno));
        fclose(f);
        return VERIFY_FAILURE;
    }

    // If this is really is the EOCD record, it will begin with the
    // magic number $50 $4b $05 $06.
    if (eocd[0] != 0x50 || eocd[1] != 0x4b ||
        eocd[2] != 0x05 || eocd[3] != 0x06) {
        LOGE("signature length doesn't match EOCD marker\n");
        fclose(f);
        return VERIFY_FAILURE;
    }

    size_t i;
    for (i = 4; i < eocd_size-3; ++i) {
        if (eocd[i  ] == 0x50 && eocd[i+1] == 0x4b &&
            eocd[i+2] == 0x05 && eocd[i+3] == 0x06) {
            // if the sequence $50 $4b $05 $06 appears anywhere after
            // the real one, minzip will find the later (wrong) one,
            // which could be exploitable.  Fail verification if
            // this sequence occurs anywhere after the real one.
            LOGE("EOCD marker occurs after start of EOCD\n");
            fclose(f);
            return VERIFY_FAILURE;
        }
    }

#define BUFFER_SIZE 4096

    SHA_CTX ctx;
    SHA_init(&ctx);
    unsigned char* buffer = (unsigned char*)malloc(BUFFER_SIZE);
    if (buffer == NULL) {
        LOGE("failed to alloc memory for sha1 buffer\n");
        fclose(f);
        return VERIFY_FAILURE;
    }

    double frac = -1.0;
    size_t so_far = 0;
    fseek(f, 0, SEEK_SET);
    while (so_far < signed_len) {
        size_t size = BUFFER_SIZE;
        if (signed_len - so_far < size) size = signed_len - so_far;
        if (fread(buffer, 1, size, f) != size) {
            LOGE("failed to read data from %s (%s)\n", path, strerror(errno));
            fclose(f);
            return VERIFY_FAILURE;
        }
        SHA_update(&ctx, buffer, size);
        so_far += size;
        double f = so_far / (double)signed_len;
        if (f > frac + 0.02 || size == so_far) {
            ui->SetProgress(f);
            frac = f;
        }
    }
    fclose(f);
    free(buffer);

    const uint8_t* sha1 = SHA_final(&ctx);
    for (i = 0; i < numKeys; ++i) {
        // The 6 bytes is the "(signature_start) $ff $ff (comment_size)" that
        // the signing tool appends after the signature itself.
        if (RSA_verify(pKeys+i, eocd + eocd_size - 6 - RSANUMBYTES,
                       RSANUMBYTES, sha1)) {
            LOGI("whole-file signature verified against key %d\n", i);
            free(eocd);
            return VERIFY_SUCCESS;
        } else {
            LOGI("failed to verify against key %d\n", i);
        }
    }
    free(eocd);
    LOGE("failed to verify whole-file signature\n");
    return VERIFY_FAILURE;
}

// Reads a file containing one or more public keys as produced by
// DumpPublicKey:  this is an RSAPublicKey struct as it would appear
// as a C source literal, eg:
//
//  "{64,0xc926ad21,{1795090719,...,-695002876},{-857949815,...,1175080310}}"
//
// For key versions newer than the original 2048-bit e=3 keys
// supported by Android, the string is preceded by a version
// identifier, eg:
//
//  "v2 {64,0xc926ad21,{1795090719,...,-695002876},{-857949815,...,1175080310}}"
//
// (Note that the braces and commas in this example are actual
// characters the parser expects to find in the file; the ellipses
// indicate more numbers omitted from this example.)
//
// The file may contain multiple keys in this format, separated by
// commas.  The last key must not be followed by a comma.
//
// Returns NULL if the file failed to parse, or if it contain zero keys.
RSAPublicKey*
load_keys(const char* filename, int* numKeys) {
    RSAPublicKey* out = NULL;
    *numKeys = 0;

    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        LOGE("opening %s: %s\n", filename, strerror(errno));
        goto exit;
    }

    {
        int i;
        bool done = false;
        while (!done) {
            ++*numKeys;
            out = (RSAPublicKey*)realloc(out, *numKeys * sizeof(RSAPublicKey));
            RSAPublicKey* key = out + (*numKeys - 1);

            char start_char;
            if (fscanf(f, " %c", &start_char) != 1) goto exit;
            if (start_char == '{') {
                // a version 1 key has no version specifier.
                key->exponent = 3;
            } else if (start_char == 'v') {
                int version;
                if (fscanf(f, "%d {", &version) != 1) goto exit;
                if (version == 2) {
                    key->exponent = 65537;
                } else {
                    goto exit;
                }
            }

            if (fscanf(f, " %i , 0x%x , { %u",
                       &(key->len), &(key->n0inv), &(key->n[0])) != 3) {
                goto exit;
            }
            if (key->len != RSANUMWORDS) {
                LOGE("key length (%d) does not match expected size\n", key->len);
                goto exit;
            }
            for (i = 1; i < key->len; ++i) {
                if (fscanf(f, " , %u", &(key->n[i])) != 1) goto exit;
            }
            if (fscanf(f, " } , { %u", &(key->rr[0])) != 1) goto exit;
            for (i = 1; i < key->len; ++i) {
                if (fscanf(f, " , %u", &(key->rr[i])) != 1) goto exit;
            }
            fscanf(f, " } } ");

            // if the line ends in a comma, this file has more keys.
            switch (fgetc(f)) {
            case ',':
                // more keys to come.
                break;

            case EOF:
                done = true;
                break;

            default:
                LOGE("unexpected character between keys\n");
                goto exit;
            }

            LOGI("read key e=%d\n", key->exponent);
        }
    }

    fclose(f);
    return out;

exit:
    if (f) fclose(f);
    free(out);
    *numKeys = 0;
    return NULL;
}
