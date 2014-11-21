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
#include "verifierold.h"
#include "ui.h"

#include "mincrypt/rsa.h"
#include "mincrypt/sha.h"
#include "mincrypt/sha256.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>

//extern RecoveryUI* ui;

#define PUBLIC_KEYS_FILE "/res/keys"

// Look for an RSA signature embedded in the .ZIP file comment given
// the path to the zip.  Verify it matches one of the given public
// keys.
//
// Return VERIFY_SUCCESS, VERIFY_FAILURE (if any error is encountered
// or no key matches the signature).
int verify_file(const char* path) {
    //ui->SetProgress(0.0);

    int numKeys;
    Certificate* pKeys = load_keys(PUBLIC_KEYS_FILE, &numKeys);
    if (pKeys == NULL) {
        LOGE("Failed to load keys\n");
        return INSTALL_CORRUPT;
    }
    LOGI("%d key(s) loaded from %s\n", numKeys, PUBLIC_KEYS_FILE);

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
        LOGE("footer is wrong\n");
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

    bool need_sha1 = false;
    bool need_sha256 = false;
    for (i = 0; i < numKeys; ++i) {
        switch (pKeys[i].hash_len) {
            case SHA_DIGEST_SIZE: need_sha1 = true; break;
            case SHA256_DIGEST_SIZE: need_sha256 = true; break;
        }
    }

    SHA_CTX sha1_ctx;
    SHA256_CTX sha256_ctx;
    SHA_init(&sha1_ctx);
    SHA256_init(&sha256_ctx);
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
        if (need_sha1) SHA_update(&sha1_ctx, buffer, size);
        if (need_sha256) SHA256_update(&sha256_ctx, buffer, size);
        so_far += size;
        double f = so_far / (double)signed_len;
        if (f > frac + 0.02 || size == so_far) {
            //ui->SetProgress(f);
            frac = f;
        }
    }
    fclose(f);
    free(buffer);

    const uint8_t* sha1 = SHA_final(&sha1_ctx);
    const uint8_t* sha256 = SHA256_final(&sha256_ctx);

    for (i = 0; i < numKeys; ++i) {
        const uint8_t* hash;
        switch (pKeys[i].hash_len) {
            case SHA_DIGEST_SIZE: hash = sha1; break;
            case SHA256_DIGEST_SIZE: hash = sha256; break;
            default: continue;
        }

        // The 6 bytes is the "(signature_start) $ff $ff (comment_size)" that
        // the signing tool appends after the signature itself.
        if (RSA_verify(pKeys[i].public_key, eocd + eocd_size - 6 - RSANUMBYTES,
                       RSANUMBYTES, hash, pKeys[i].hash_len)) {
            LOGI("whole-file signature verified against key %d\n", i);
            free(eocd);
            return VERIFY_SUCCESS;
        } else {
            LOGI("failed to verify against key %d\n", i);
        }
		LOGI("i: %i, eocd_size: %i, RSANUMBYTES: %i\n", i, eocd_size, RSANUMBYTES);
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
// A Certificate is a pair of an RSAPublicKey and a particular hash
// (we support SHA-1 and SHA-256; we store the hash length to signify
// which is being used).  The hash used is implied by the version number.
//
//       1: 2048-bit RSA key with e=3 and SHA-1 hash
//       2: 2048-bit RSA key with e=65537 and SHA-1 hash
//       3: 2048-bit RSA key with e=3 and SHA-256 hash
//       4: 2048-bit RSA key with e=65537 and SHA-256 hash
//
// Returns NULL if the file failed to parse, or if it contain zero keys.
Certificate*
load_keys(const char* filename, int* numKeys) {
    Certificate* out = NULL;
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
            out = (Certificate*)realloc(out, *numKeys * sizeof(Certificate));
            Certificate* cert = out + (*numKeys - 1);
            cert->public_key = (RSAPublicKey*)malloc(sizeof(RSAPublicKey));

            char start_char;
            if (fscanf(f, " %c", &start_char) != 1) goto exit;
            if (start_char == '{') {
                // a version 1 key has no version specifier.
                cert->public_key->exponent = 3;
                cert->hash_len = SHA_DIGEST_SIZE;
            } else if (start_char == 'v') {
                int version;
                if (fscanf(f, "%d {", &version) != 1) goto exit;
                switch (version) {
                    case 2:
                        cert->public_key->exponent = 65537;
                        cert->hash_len = SHA_DIGEST_SIZE;
                        break;
                    case 3:
                        cert->public_key->exponent = 3;
                        cert->hash_len = SHA256_DIGEST_SIZE;
                        break;
                    case 4:
                        cert->public_key->exponent = 65537;
                        cert->hash_len = SHA256_DIGEST_SIZE;
                        break;
                    default:
                        goto exit;
                }
            }

            RSAPublicKey* key = cert->public_key;
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
            LOGI("read key e=%d hash=%d\n", key->exponent, cert->hash_len);
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
