/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>

#include "amend/amend.h"
#include "common.h"
#include "install.h"
#include "mincrypt/rsa.h"
#include "minui/minui.h"
#include "minzip/SysUtil.h"
#include "minzip/Zip.h"
#include "mtdutils/mounts.h"
#include "mtdutils/mtdutils.h"
#include "roots.h"
#include "verifier.h"

#define ASSUMED_UPDATE_SCRIPT_NAME  "META-INF/com/google/android/update-script"
#define PUBLIC_KEYS_FILE "/res/keys"

static const ZipEntry *
find_update_script(ZipArchive *zip)
{
//TODO: Get the location of this script from the MANIFEST.MF file
    return mzFindZipEntry(zip, ASSUMED_UPDATE_SCRIPT_NAME);
}

static int read_data(ZipArchive *zip, const ZipEntry *entry,
        char** ppData, int* pLength) {
    int len = (int)mzGetZipEntryUncompLen(entry);
    if (len <= 0) {
        LOGE("Bad data length %d\n", len);
        return -1;
    }
    char *data = malloc(len + 1);
    if (data == NULL) {
        LOGE("Can't allocate %d bytes for data\n", len + 1);
        return -2;
    }
    bool ok = mzReadZipEntry(zip, entry, data, len);
    if (!ok) {
        LOGE("Error while reading data\n");
        free(data);
        return -3;
    }
    data[len] = '\0';     // not necessary, but just to be safe
    *ppData = data;
    if (pLength) {
        *pLength = len;
    }
    return 0;
}

static int
handle_update_script(ZipArchive *zip, const ZipEntry *update_script_entry)
{
    /* Read the entire script into a buffer.
     */
    int script_len;
    char* script_data;
    if (read_data(zip, update_script_entry, &script_data, &script_len) < 0) {
        LOGE("Can't read update script\n");
        return INSTALL_ERROR;
    }

    /* Parse the script.  Note that the script and parse tree are never freed.
     */
    const AmCommandList *commands = parseAmendScript(script_data, script_len);
    if (commands == NULL) {
        LOGE("Syntax error in update script\n");
        return INSTALL_ERROR;
    } else {
        UnterminatedString name = mzGetZipEntryFileName(update_script_entry);
        LOGI("Parsed %.*s\n", name.len, name.str);
    }

    /* Execute the script.
     */
    int ret = execCommandList((ExecContext *)1, commands);
    if (ret != 0) {
        int num = ret;
        char *line, *next = script_data;
        while (next != NULL && ret-- > 0) {
            line = next;
            next = memchr(line, '\n', script_data + script_len - line);
            if (next != NULL) *next++ = '\0';
        }
        LOGE("Failure at line %d:\n%s\n", num, next ? line : "(not found)");
        return INSTALL_ERROR;
    }

    ui_print("Installation complete.\n");
    return INSTALL_SUCCESS;
}

static int
handle_update_package(const char *path, ZipArchive *zip,
                      const RSAPublicKey *keys, int numKeys)
{
    // Give verification half the progress bar...
    ui_print("Verifying update package...\n");
    ui_show_progress(
            VERIFICATION_PROGRESS_FRACTION,
            VERIFICATION_PROGRESS_TIME);

    if (!verify_jar_signature(zip, keys, numKeys)) {
        LOGE("Verification failed\n");
        return INSTALL_CORRUPT;
    }

    // Update should take the rest of the progress bar.
    ui_print("Installing update...\n");

    const ZipEntry *script_entry;
    script_entry = find_update_script(zip);
    if (script_entry == NULL) {
        LOGE("Can't find update script\n");
        return INSTALL_CORRUPT;
    }

    if (register_package_root(zip, path) < 0) {
        LOGE("Can't register package root\n");
        return INSTALL_ERROR;
    }

    int ret = handle_update_script(zip, script_entry);
    register_package_root(NULL, NULL);  // Unregister package root
    return ret;
}

// Reads a file containing one or more public keys as produced by
// DumpPublicKey:  this is an RSAPublicKey struct as it would appear
// as a C source literal, eg:
//
//  "{64,0xc926ad21,{1795090719,...,-695002876},{-857949815,...,1175080310}}"
//
// (Note that the braces and commas in this example are actual
// characters the parser expects to find in the file; the ellipses
// indicate more numbers omitted from this example.)
//
// The file may contain multiple keys in this format, separated by
// commas.  The last key must not be followed by a comma.
//
// Returns NULL if the file failed to parse, or if it contain zero keys.
static RSAPublicKey*
load_keys(const char* filename, int* numKeys) {
    RSAPublicKey* out = NULL;
    *numKeys = 0;

    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        LOGE("opening %s: %s\n", filename, strerror(errno));
        goto exit;
    }

    int i;
    bool done = false;
    while (!done) {
        ++*numKeys;
        out = realloc(out, *numKeys * sizeof(RSAPublicKey));
        RSAPublicKey* key = out + (*numKeys - 1);
        if (fscanf(f, " { %i , %i , { %i",
                   &(key->len), &(key->n0inv), &(key->n[0])) != 3) {
            goto exit;
        }
        if (key->len != RSANUMWORDS) {
            LOGE("key length (%d) does not match expected size\n", key->len);
            goto exit;
        }
        for (i = 1; i < key->len; ++i) {
            if (fscanf(f, " , %i", &(key->n[i])) != 1) goto exit;
        }
        if (fscanf(f, " } , { %i", &(key->rr[0])) != 1) goto exit;
        for (i = 1; i < key->len; ++i) {
            if (fscanf(f, " , %i", &(key->rr[i])) != 1) goto exit;
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
    }

    fclose(f);
    return out;

exit:
    if (f) fclose(f);
    free(out);
    *numKeys = 0;
    return NULL;
}

int
install_package(const char *root_path)
{
    ui_set_background(BACKGROUND_ICON_INSTALLING);
    ui_print("Finding update package...\n");
    ui_show_indeterminate_progress();
    LOGI("Update location: %s\n", root_path);

    if (ensure_root_path_mounted(root_path) != 0) {
        LOGE("Can't mount %s\n", root_path);
        return INSTALL_CORRUPT;
    }

    char path[PATH_MAX] = "";
    if (translate_root_path(root_path, path, sizeof(path)) == NULL) {
        LOGE("Bad path %s\n", root_path);
        return INSTALL_CORRUPT;
    }

    ui_print("Opening update package...\n");
    LOGI("Update file path: %s\n", path);

    int numKeys;
    RSAPublicKey* loadedKeys = load_keys(PUBLIC_KEYS_FILE, &numKeys);
    if (loadedKeys == NULL) {
        LOGE("Failed to load keys\n");
        return INSTALL_CORRUPT;
    }
    LOGI("%d key(s) loaded from %s\n", numKeys, PUBLIC_KEYS_FILE);

    /* Try to open the package.
     */
    ZipArchive zip;
    int err = mzOpenZipArchive(path, &zip);
    if (err != 0) {
        LOGE("Can't open %s\n(%s)\n", path, err != -1 ? strerror(err) : "bad");
        return INSTALL_CORRUPT;
    }

    /* Verify and install the contents of the package.
     */
    int status = handle_update_package(path, &zip, loadedKeys, numKeys);
    mzCloseZipArchive(&zip);
    free(loadedKeys);
    return status;
}
