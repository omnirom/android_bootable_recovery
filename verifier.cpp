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

#include "asn1_decoder.h"
#include "common.h"
#include "ui.h"
#include "verifier.h"

#include "mincrypt/dsa_sig.h"
#include "mincrypt/p256.h"
#include "mincrypt/p256_ecdsa.h"
#include "mincrypt/rsa.h"
#include "mincrypt/sha.h"
#include "mincrypt/sha256.h"

#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

extern RecoveryUI* ui;

/*
 * Simple version of PKCS#7 SignedData extraction. This extracts the
 * signature OCTET STRING to be used for signature verification.
 *
 * For full details, see http://www.ietf.org/rfc/rfc3852.txt
 *
 * The PKCS#7 structure looks like:
 *
 *   SEQUENCE (ContentInfo)
 *     OID (ContentType)
 *     [0] (content)
 *       SEQUENCE (SignedData)
 *         INTEGER (version CMSVersion)
 *         SET (DigestAlgorithmIdentifiers)
 *         SEQUENCE (EncapsulatedContentInfo)
 *         [0] (CertificateSet OPTIONAL)
 *         [1] (RevocationInfoChoices OPTIONAL)
 *         SET (SignerInfos)
 *           SEQUENCE (SignerInfo)
 *             INTEGER (CMSVersion)
 *             SEQUENCE (SignerIdentifier)
 *             SEQUENCE (DigestAlgorithmIdentifier)
 *             SEQUENCE (SignatureAlgorithmIdentifier)
 *             OCTET STRING (SignatureValue)
 */
static bool read_pkcs7(uint8_t* pkcs7_der, size_t pkcs7_der_len, uint8_t** sig_der,
        size_t* sig_der_length) {
    asn1_context_t* ctx = asn1_context_new(pkcs7_der, pkcs7_der_len);
    if (ctx == NULL) {
        return false;
    }

    asn1_context_t* pkcs7_seq = asn1_sequence_get(ctx);
    if (pkcs7_seq != NULL && asn1_sequence_next(pkcs7_seq)) {
        asn1_context_t *signed_data_app = asn1_constructed_get(pkcs7_seq);
        if (signed_data_app != NULL) {
            asn1_context_t* signed_data_seq = asn1_sequence_get(signed_data_app);
            if (signed_data_seq != NULL
                    && asn1_sequence_next(signed_data_seq)
                    && asn1_sequence_next(signed_data_seq)
                    && asn1_sequence_next(signed_data_seq)
                    && asn1_constructed_skip_all(signed_data_seq)) {
                asn1_context_t *sig_set = asn1_set_get(signed_data_seq);
                if (sig_set != NULL) {
                    asn1_context_t* sig_seq = asn1_sequence_get(sig_set);
                    if (sig_seq != NULL
                            && asn1_sequence_next(sig_seq)
                            && asn1_sequence_next(sig_seq)
                            && asn1_sequence_next(sig_seq)
                            && asn1_sequence_next(sig_seq)) {
                        uint8_t* sig_der_ptr;
                        if (asn1_octet_string_get(sig_seq, &sig_der_ptr, sig_der_length)) {
                            *sig_der = (uint8_t*) malloc(*sig_der_length);
                            if (*sig_der != NULL) {
                                memcpy(*sig_der, sig_der_ptr, *sig_der_length);
                            }
                        }
                        asn1_context_free(sig_seq);
                    }
                    asn1_context_free(sig_set);
                }
                asn1_context_free(signed_data_seq);
            }
            asn1_context_free(signed_data_app);
        }
        asn1_context_free(pkcs7_seq);
    }
    asn1_context_free(ctx);

    return *sig_der != NULL;
}

// Look for an RSA signature embedded in the .ZIP file comment given
// the path to the zip.  Verify it matches one of the given public
// keys.
//
// Return VERIFY_SUCCESS, VERIFY_FAILURE (if any error is encountered
// or no key matches the signature).

int verify_file(unsigned char* addr, size_t length,
                const std::vector<Certificate>& keys) {
    ui->SetProgress(0.0);

    // An archive with a whole-file signature will end in six bytes:
    //
    //   (2-byte signature start) $ff $ff (2-byte comment size)
    //
    // (As far as the ZIP format is concerned, these are part of the
    // archive comment.)  We start by reading this footer, this tells
    // us how far back from the end we have to start reading to find
    // the whole comment.

#define FOOTER_SIZE 6

    if (length < FOOTER_SIZE) {
        LOGE("not big enough to contain footer\n");
        return VERIFY_FAILURE;
    }

    unsigned char* footer = addr + length - FOOTER_SIZE;

    if (footer[2] != 0xff || footer[3] != 0xff) {
        LOGE("footer is wrong\n");
        return VERIFY_FAILURE;
    }

    size_t comment_size = footer[4] + (footer[5] << 8);
    size_t signature_start = footer[0] + (footer[1] << 8);
    LOGI("comment is %zu bytes; signature %zu bytes from end\n",
         comment_size, signature_start);

    if (signature_start <= FOOTER_SIZE) {
        LOGE("Signature start is in the footer");
        return VERIFY_FAILURE;
    }

#define EOCD_HEADER_SIZE 22

    // The end-of-central-directory record is 22 bytes plus any
    // comment length.
    size_t eocd_size = comment_size + EOCD_HEADER_SIZE;

    if (length < eocd_size) {
        LOGE("not big enough to contain EOCD\n");
        return VERIFY_FAILURE;
    }

    // Determine how much of the file is covered by the signature.
    // This is everything except the signature data and length, which
    // includes all of the EOCD except for the comment length field (2
    // bytes) and the comment data.
    size_t signed_len = length - eocd_size + EOCD_HEADER_SIZE - 2;

    unsigned char* eocd = addr + length - eocd_size;

    // If this is really is the EOCD record, it will begin with the
    // magic number $50 $4b $05 $06.
    if (eocd[0] != 0x50 || eocd[1] != 0x4b ||
        eocd[2] != 0x05 || eocd[3] != 0x06) {
        LOGE("signature length doesn't match EOCD marker\n");
        return VERIFY_FAILURE;
    }

    for (size_t i = 4; i < eocd_size-3; ++i) {
        if (eocd[i  ] == 0x50 && eocd[i+1] == 0x4b &&
            eocd[i+2] == 0x05 && eocd[i+3] == 0x06) {
            // if the sequence $50 $4b $05 $06 appears anywhere after
            // the real one, minzip will find the later (wrong) one,
            // which could be exploitable.  Fail verification if
            // this sequence occurs anywhere after the real one.
            LOGE("EOCD marker occurs after start of EOCD\n");
            return VERIFY_FAILURE;
        }
    }

#define BUFFER_SIZE 4096

    bool need_sha1 = false;
    bool need_sha256 = false;
    for (const auto& key : keys) {
        switch (key.hash_len) {
            case SHA_DIGEST_SIZE: need_sha1 = true; break;
            case SHA256_DIGEST_SIZE: need_sha256 = true; break;
        }
    }

    SHA_CTX sha1_ctx;
    SHA256_CTX sha256_ctx;
    SHA_init(&sha1_ctx);
    SHA256_init(&sha256_ctx);

    double frac = -1.0;
    size_t so_far = 0;
    while (so_far < signed_len) {
        size_t size = signed_len - so_far;
        if (size > BUFFER_SIZE) size = BUFFER_SIZE;

        if (need_sha1) SHA_update(&sha1_ctx, addr + so_far, size);
        if (need_sha256) SHA256_update(&sha256_ctx, addr + so_far, size);
        so_far += size;

        double f = so_far / (double)signed_len;
        if (f > frac + 0.02 || size == so_far) {
            ui->SetProgress(f);
            frac = f;
        }
    }

    const uint8_t* sha1 = SHA_final(&sha1_ctx);
    const uint8_t* sha256 = SHA256_final(&sha256_ctx);

    uint8_t* sig_der = nullptr;
    size_t sig_der_length = 0;

    size_t signature_size = signature_start - FOOTER_SIZE;
    if (!read_pkcs7(eocd + eocd_size - signature_start, signature_size, &sig_der,
            &sig_der_length)) {
        LOGE("Could not find signature DER block\n");
        return VERIFY_FAILURE;
    }

    /*
     * Check to make sure at least one of the keys matches the signature. Since
     * any key can match, we need to try each before determining a verification
     * failure has happened.
     */
    size_t i = 0;
    for (const auto& key : keys) {
        const uint8_t* hash;
        switch (key.hash_len) {
            case SHA_DIGEST_SIZE: hash = sha1; break;
            case SHA256_DIGEST_SIZE: hash = sha256; break;
            default: continue;
        }

        // The 6 bytes is the "(signature_start) $ff $ff (comment_size)" that
        // the signing tool appends after the signature itself.
        if (key.key_type == Certificate::RSA) {
            if (sig_der_length < RSANUMBYTES) {
                // "signature" block isn't big enough to contain an RSA block.
                LOGI("signature is too short for RSA key %zu\n", i);
                continue;
            }

            if (!RSA_verify(key.rsa.get(), sig_der, RSANUMBYTES,
                            hash, key.hash_len)) {
                LOGI("failed to verify against RSA key %zu\n", i);
                continue;
            }

            LOGI("whole-file signature verified against RSA key %zu\n", i);
            free(sig_der);
            return VERIFY_SUCCESS;
        } else if (key.key_type == Certificate::EC
                && key.hash_len == SHA256_DIGEST_SIZE) {
            p256_int r, s;
            if (!dsa_sig_unpack(sig_der, sig_der_length, &r, &s)) {
                LOGI("Not a DSA signature block for EC key %zu\n", i);
                continue;
            }

            p256_int p256_hash;
            p256_from_bin(hash, &p256_hash);
            if (!p256_ecdsa_verify(&(key.ec->x), &(key.ec->y),
                                   &p256_hash, &r, &s)) {
                LOGI("failed to verify against EC key %zu\n", i);
                continue;
            }

            LOGI("whole-file signature verified against EC key %zu\n", i);
            free(sig_der);
            return VERIFY_SUCCESS;
        } else {
            LOGI("Unknown key type %d\n", key.key_type);
        }
        i++;
    }
    free(sig_der);
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
//       5: 256-bit EC key using the NIST P-256 curve parameters and SHA-256 hash
//
// Returns true on success, and appends the found keys (at least one) to certs.
// Otherwise returns false if the file failed to parse, or if it contains zero
// keys. The contents in certs would be unspecified on failure.
bool load_keys(const char* filename, std::vector<Certificate>& certs) {
    std::unique_ptr<FILE, decltype(&fclose)> f(fopen(filename, "r"), fclose);
    if (!f) {
        LOGE("opening %s: %s\n", filename, strerror(errno));
        return false;
    }

    while (true) {
        certs.emplace_back(0, Certificate::RSA, nullptr, nullptr);
        Certificate& cert = certs.back();

        char start_char;
        if (fscanf(f.get(), " %c", &start_char) != 1) return false;
        if (start_char == '{') {
            // a version 1 key has no version specifier.
            cert.key_type = Certificate::RSA;
            cert.rsa = std::unique_ptr<RSAPublicKey>(new RSAPublicKey);
            cert.rsa->exponent = 3;
            cert.hash_len = SHA_DIGEST_SIZE;
        } else if (start_char == 'v') {
            int version;
            if (fscanf(f.get(), "%d {", &version) != 1) return false;
            switch (version) {
                case 2:
                    cert.key_type = Certificate::RSA;
                    cert.rsa = std::unique_ptr<RSAPublicKey>(new RSAPublicKey);
                    cert.rsa->exponent = 65537;
                    cert.hash_len = SHA_DIGEST_SIZE;
                    break;
                case 3:
                    cert.key_type = Certificate::RSA;
                    cert.rsa = std::unique_ptr<RSAPublicKey>(new RSAPublicKey);
                    cert.rsa->exponent = 3;
                    cert.hash_len = SHA256_DIGEST_SIZE;
                    break;
                case 4:
                    cert.key_type = Certificate::RSA;
                    cert.rsa = std::unique_ptr<RSAPublicKey>(new RSAPublicKey);
                    cert.rsa->exponent = 65537;
                    cert.hash_len = SHA256_DIGEST_SIZE;
                    break;
                case 5:
                    cert.key_type = Certificate::EC;
                    cert.ec = std::unique_ptr<ECPublicKey>(new ECPublicKey);
                    cert.hash_len = SHA256_DIGEST_SIZE;
                    break;
                default:
                    return false;
            }
        }

        if (cert.key_type == Certificate::RSA) {
            RSAPublicKey* key = cert.rsa.get();
            if (fscanf(f.get(), " %i , 0x%x , { %u", &(key->len), &(key->n0inv),
                    &(key->n[0])) != 3) {
                return false;
            }
            if (key->len != RSANUMWORDS) {
                LOGE("key length (%d) does not match expected size\n", key->len);
                return false;
            }
            for (int i = 1; i < key->len; ++i) {
                if (fscanf(f.get(), " , %u", &(key->n[i])) != 1) return false;
            }
            if (fscanf(f.get(), " } , { %u", &(key->rr[0])) != 1) return false;
            for (int i = 1; i < key->len; ++i) {
                if (fscanf(f.get(), " , %u", &(key->rr[i])) != 1) return false;
            }
            fscanf(f.get(), " } } ");

            LOGI("read key e=%d hash=%d\n", key->exponent, cert.hash_len);
        } else if (cert.key_type == Certificate::EC) {
            ECPublicKey* key = cert.ec.get();
            int key_len;
            unsigned int byte;
            uint8_t x_bytes[P256_NBYTES];
            uint8_t y_bytes[P256_NBYTES];
            if (fscanf(f.get(), " %i , { %u", &key_len, &byte) != 2) return false;
            if (key_len != P256_NBYTES) {
                LOGE("Key length (%d) does not match expected size %d\n", key_len, P256_NBYTES);
                return false;
            }
            x_bytes[P256_NBYTES - 1] = byte;
            for (int i = P256_NBYTES - 2; i >= 0; --i) {
                if (fscanf(f.get(), " , %u", &byte) != 1) return false;
                x_bytes[i] = byte;
            }
            if (fscanf(f.get(), " } , { %u", &byte) != 1) return false;
            y_bytes[P256_NBYTES - 1] = byte;
            for (int i = P256_NBYTES - 2; i >= 0; --i) {
                if (fscanf(f.get(), " , %u", &byte) != 1) return false;
                y_bytes[i] = byte;
            }
            fscanf(f.get(), " } } ");
            p256_from_bin(x_bytes, &key->x);
            p256_from_bin(y_bytes, &key->y);
        } else {
            LOGE("Unknown key type %d\n", cert.key_type);
            return false;
        }

        // if the line ends in a comma, this file has more keys.
        int ch = fgetc(f.get());
        if (ch == ',') {
            // more keys to come.
            continue;
        } else if (ch == EOF) {
            break;
        } else {
            LOGE("unexpected character between keys\n");
            return false;
        }
    }

    return true;
}
