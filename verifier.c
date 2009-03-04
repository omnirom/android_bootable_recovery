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

#include "minzip/Zip.h"
#include "mincrypt/rsa.h"
#include "mincrypt/sha.h"

#include <netinet/in.h>  /* required for resolv.h */
#include <resolv.h>      /* for base64 codec */
#include <string.h>

/* Return an allocated buffer with the contents of a zip file entry. */
static char *slurpEntry(const ZipArchive *pArchive, const ZipEntry *pEntry) {
    if (!mzIsZipEntryIntact(pArchive, pEntry)) {
        UnterminatedString fn = mzGetZipEntryFileName(pEntry);
        LOGE("Invalid %.*s\n", fn.len, fn.str);
        return NULL;
    }

    int len = mzGetZipEntryUncompLen(pEntry);
    char *buf = malloc(len + 1);
    if (buf == NULL) {
        UnterminatedString fn = mzGetZipEntryFileName(pEntry);
        LOGE("Can't allocate %d bytes for %.*s\n", len, fn.len, fn.str);
        return NULL;
    }

    if (!mzReadZipEntry(pArchive, pEntry, buf, len)) {
        UnterminatedString fn = mzGetZipEntryFileName(pEntry);
        LOGE("Can't read %.*s\n", fn.len, fn.str);
        free(buf);
        return NULL;
    }

    buf[len] = '\0';
    return buf;
}


struct DigestContext {
    SHA_CTX digest;
    unsigned *doneBytes;
    unsigned totalBytes;
};


/* mzProcessZipEntryContents callback to update an SHA-1 hash context. */
static bool updateHash(const unsigned char *data, int dataLen, void *cookie) {
    struct DigestContext *context = (struct DigestContext *) cookie;
    SHA_update(&context->digest, data, dataLen);
    if (context->doneBytes != NULL) {
        *context->doneBytes += dataLen;
        if (context->totalBytes > 0) {
            ui_set_progress(*context->doneBytes * 1.0 / context->totalBytes);
        }
    }
    return true;
}


/* Get the SHA-1 digest of a zip file entry. */
static bool digestEntry(const ZipArchive *pArchive, const ZipEntry *pEntry,
        unsigned *doneBytes, unsigned totalBytes,
        uint8_t digest[SHA_DIGEST_SIZE]) {
    struct DigestContext context;
    SHA_init(&context.digest);
    context.doneBytes = doneBytes;
    context.totalBytes = totalBytes;
    if (!mzProcessZipEntryContents(pArchive, pEntry, updateHash, &context)) {
        UnterminatedString fn = mzGetZipEntryFileName(pEntry);
        LOGE("Can't digest %.*s\n", fn.len, fn.str);
        return false;
    }

    memcpy(digest, SHA_final(&context.digest), SHA_DIGEST_SIZE);

#ifdef LOG_VERBOSE
    UnterminatedString fn = mzGetZipEntryFileName(pEntry);
    char base64[SHA_DIGEST_SIZE * 3];
    b64_ntop(digest, SHA_DIGEST_SIZE, base64, sizeof(base64));
    LOGV("sha1(%.*s) = %s\n", fn.len, fn.str, base64);
#endif

    return true;
}


/* Find a /META-INF/xxx.SF signature file signed by a matching xxx.RSA file. */
static const ZipEntry *verifySignature(const ZipArchive *pArchive,
        const RSAPublicKey *pKeys, unsigned int numKeys) {
    static const char prefix[] = "META-INF/";
    static const char rsa[] = ".RSA", sf[] = ".SF";

    unsigned int i, j;
    for (i = 0; i < mzZipEntryCount(pArchive); ++i) {
        const ZipEntry *rsaEntry = mzGetZipEntryAt(pArchive, i);
        UnterminatedString rsaName = mzGetZipEntryFileName(rsaEntry);
        int rsaLen = mzGetZipEntryUncompLen(rsaEntry);
        if (rsaLen >= RSANUMBYTES && rsaName.len > sizeof(prefix) &&
                !strncmp(rsaName.str, prefix, sizeof(prefix) - 1) &&
                !strncmp(rsaName.str + rsaName.len - sizeof(rsa) + 1,
                         rsa, sizeof(rsa) - 1)) {
            char *sfName = malloc(rsaName.len - sizeof(rsa) + sizeof(sf) + 1);
            if (sfName == NULL) {
                LOGE("Can't allocate %d bytes for filename\n", rsaName.len);
                continue;
            }

            /* Replace .RSA with .SF */
            strncpy(sfName, rsaName.str, rsaName.len - sizeof(rsa) + 1);
            strcpy(sfName + rsaName.len - sizeof(rsa) + 1, sf);
            const ZipEntry *sfEntry = mzFindZipEntry(pArchive, sfName);

            if (sfEntry == NULL) {
                LOGW("Missing signature file %s\n", sfName);
                free(sfName);
                continue;
            }

            free(sfName);

            uint8_t sfDigest[SHA_DIGEST_SIZE];
            if (!digestEntry(pArchive, sfEntry, NULL, 0, sfDigest)) continue;

            char *rsaBuf = slurpEntry(pArchive, rsaEntry);
            if (rsaBuf == NULL) continue;

            /* Try to verify the signature with all the keys. */
            uint8_t *sig = (uint8_t *) rsaBuf + rsaLen - RSANUMBYTES;
            for (j = 0; j < numKeys; ++j) {
                if (RSA_verify(&pKeys[j], sig, RSANUMBYTES, sfDigest)) {
                    free(rsaBuf);
                    LOGI("Verified %.*s\n", rsaName.len, rsaName.str);
                    return sfEntry;
                }
            }

            free(rsaBuf);
            LOGW("Can't verify %.*s\n", rsaName.len, rsaName.str);
        }
    }

    LOGE("No signature (%d files)\n", mzZipEntryCount(pArchive));
    return NULL;
}


/* Verify /META-INF/MANIFEST.MF against the digest in a signature file. */
static const ZipEntry *verifyManifest(const ZipArchive *pArchive,
        const ZipEntry *sfEntry) {
    static const char prefix[] = "SHA1-Digest-Manifest: ", eol[] = "\r\n";
    uint8_t expected[SHA_DIGEST_SIZE + 3], actual[SHA_DIGEST_SIZE];

    char *sfBuf = slurpEntry(pArchive, sfEntry);
    if (sfBuf == NULL) return NULL;

    char *line, *save;
    for (line = strtok_r(sfBuf, eol, &save); line != NULL;
         line = strtok_r(NULL, eol, &save)) {
        if (!strncasecmp(prefix, line, sizeof(prefix) - 1)) {
            UnterminatedString fn = mzGetZipEntryFileName(sfEntry);
            const char *digest = line + sizeof(prefix) - 1;
            int n = b64_pton(digest, expected, sizeof(expected));
            if (n != SHA_DIGEST_SIZE) {
                LOGE("Invalid base64 in %.*s: %s (%d)\n",
                        fn.len, fn.str, digest, n);
                line = NULL;
            }
            break;
        }
    }

    free(sfBuf);

    if (line == NULL) {
        LOGE("No digest manifest in signature file\n");
        return false;
    }

    const char *mfName = "META-INF/MANIFEST.MF";
    const ZipEntry *mfEntry = mzFindZipEntry(pArchive, mfName);
    if (mfEntry == NULL) {
        LOGE("No manifest file %s\n", mfName);
        return NULL;
    }

    if (!digestEntry(pArchive, mfEntry, NULL, 0, actual)) return NULL;
    if (memcmp(expected, actual, SHA_DIGEST_SIZE)) {
        UnterminatedString fn = mzGetZipEntryFileName(sfEntry);
        LOGE("Wrong digest for %s in %.*s\n", mfName, fn.len, fn.str);
        return NULL;
    }

    LOGI("Verified %s\n", mfName);
    return mfEntry;
}


/* Verify all the files in a Zip archive against the manifest. */
static bool verifyArchive(const ZipArchive *pArchive, const ZipEntry *mfEntry) {
    static const char namePrefix[] = "Name: ";
    static const char contPrefix[] = " ";  // Continuation of the filename
    static const char digestPrefix[] = "SHA1-Digest: ";
    static const char eol[] = "\r\n";

    char *mfBuf = slurpEntry(pArchive, mfEntry);
    if (mfBuf == NULL) return false;

    /* we're using calloc() here, so the initial state of the array is false */
    bool *unverified = (bool *) calloc(mzZipEntryCount(pArchive), sizeof(bool));
    if (unverified == NULL) {
        LOGE("Can't allocate valid flags\n");
        free(mfBuf);
        return false;
    }

    /* Mark all the files in the archive that need to be verified.
     * As we scan the manifest and check signatures, we'll unset these flags.
     * At the end, we'll make sure that all the flags are unset.
     */

    unsigned i, totalBytes = 0;
    for (i = 0; i < mzZipEntryCount(pArchive); ++i) {
        const ZipEntry *entry = mzGetZipEntryAt(pArchive, i);
        UnterminatedString fn = mzGetZipEntryFileName(entry);
        int len = mzGetZipEntryUncompLen(entry);

        // Don't validate: directories, the manifest, *.RSA, and *.SF.

        if (entry == mfEntry) {
            LOGV("Skipping manifest %.*s\n", fn.len, fn.str);
        } else if (fn.len > 0 && fn.str[fn.len-1] == '/' && len == 0) {
            LOGV("Skipping directory %.*s\n", fn.len, fn.str);
        } else if (!strncasecmp(fn.str, "META-INF/", 9) && (
                !strncasecmp(fn.str + fn.len - 4, ".RSA", 4) ||
                !strncasecmp(fn.str + fn.len - 3, ".SF", 3))) {
            LOGV("Skipping signature %.*s\n", fn.len, fn.str);
        } else {
            unverified[i] = true;
            totalBytes += len;
        }
    }

    unsigned doneBytes = 0;
    char *line, *save, *name = NULL;
    for (line = strtok_r(mfBuf, eol, &save); line != NULL;
         line = strtok_r(NULL, eol, &save)) {
        if (!strncasecmp(line, namePrefix, sizeof(namePrefix) - 1)) {
            // "Name:" introducing a new stanza
            if (name != NULL) {
                LOGE("No digest:\n  %s\n", name);
                break;
            }

            name = strdup(line + sizeof(namePrefix) - 1);
            if (name == NULL) {
                LOGE("Can't copy filename in %s\n", line);
                break;
            }
        } else if (!strncasecmp(line, contPrefix, sizeof(contPrefix) - 1)) {
            // Continuing a long name (nothing else should be continued)
            const char *tail = line + sizeof(contPrefix) - 1;
            if (name == NULL) {
                LOGE("Unexpected continuation:\n  %s\n", tail);
            }

            char *concat;
            if (asprintf(&concat, "%s%s", name, tail) < 0) {
                LOGE("Can't append continuation %s\n", tail);
                break;
            }
            free(name);
            name = concat;
        } else if (!strncasecmp(line, digestPrefix, sizeof(digestPrefix) - 1)) {
            // "Digest:" supplying a hash code for the current stanza
            const char *base64 = line + sizeof(digestPrefix) - 1;
            if (name == NULL) {
                LOGE("Unexpected digest:\n  %s\n", base64);
                break;
            }

            const ZipEntry *entry = mzFindZipEntry(pArchive, name);
            if (entry == NULL) {
                LOGE("Missing file:\n  %s\n", name);
                break;
            }
            if (!mzIsZipEntryIntact(pArchive, entry)) {
                LOGE("Corrupt file:\n  %s\n", name);
                break;
            }
            if (!unverified[mzGetZipEntryIndex(pArchive, entry)]) {
                LOGE("Unexpected file:\n  %s\n", name);
                break;
            }

            uint8_t expected[SHA_DIGEST_SIZE + 3], actual[SHA_DIGEST_SIZE];
            int n = b64_pton(base64, expected, sizeof(expected));
            if (n != SHA_DIGEST_SIZE) {
                LOGE("Invalid base64:\n  %s\n  %s\n", name, base64);
                break;
            }

            if (!digestEntry(pArchive, entry, &doneBytes, totalBytes, actual) ||
                memcmp(expected, actual, SHA_DIGEST_SIZE) != 0) {
                LOGE("Wrong digest:\n  %s\n", name);
                break;
            }

            LOGI("Verified %s\n", name);
            unverified[mzGetZipEntryIndex(pArchive, entry)] = false;
            free(name);
            name = NULL;
        }
    }

    if (name != NULL) free(name);
    free(mfBuf);

    for (i = 0; i < mzZipEntryCount(pArchive) && !unverified[i]; ++i) ;
    free(unverified);

    // This means we didn't get to the end of the manifest successfully.
    if (line != NULL) return false;

    if (i < mzZipEntryCount(pArchive)) {
        const ZipEntry *entry = mzGetZipEntryAt(pArchive, i);
        UnterminatedString fn = mzGetZipEntryFileName(entry);
        LOGE("No digest for %.*s\n", fn.len, fn.str);
        return false;
    }

    return true;
}


bool verify_jar_signature(const ZipArchive *pArchive,
        const RSAPublicKey *pKeys, int numKeys) {
    const ZipEntry *sfEntry = verifySignature(pArchive, pKeys, numKeys);
    if (sfEntry == NULL) return false;

    const ZipEntry *mfEntry = verifyManifest(pArchive, sfEntry);
    if (mfEntry == NULL) return false;

    return verifyArchive(pArchive, mfEntry);
}
