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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

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
#include "firmware.h"

#define ASSUMED_UPDATE_SCRIPT_NAME  "META-INF/com/google/android/update-script"
#define ASSUMED_UPDATE_BINARY_NAME  "META-INF/com/google/android/update-binary"
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
        char *line = NULL, *next = script_data;
        while (next != NULL && ret-- > 0) {
            line = next;
            next = memchr(line, '\n', script_data + script_len - line);
            if (next != NULL) *next++ = '\0';
        }
        LOGE("Failure at line %d:\n%s\n", num, next ? line : "(not found)");
        return INSTALL_ERROR;
    }

    LOGI("Installation complete.\n");
    return INSTALL_SUCCESS;
}

// The update binary ask us to install a firmware file on reboot.  Set
// that up.  Takes ownership of type and filename.
static int
handle_firmware_update(char* type, char* filename) {
    struct stat st_data;
    if (stat(filename, &st_data) < 0) {
        LOGE("Error stat'ing %s: %s\n", filename, strerror(errno));
        return INSTALL_ERROR;
    }

    LOGI("type is [%s]\n", type);

    char* data = malloc(st_data.st_size);
    if (data == NULL) {
        LOGE("Can't allocate %d bytes for firmware data\n", st_data.st_size);
        return INSTALL_ERROR;
    }

    FILE* f = fopen(filename, "rb");
    if (f == NULL) {
        LOGE("Failed to open %s: %s\n", filename, strerror(errno));
        return INSTALL_ERROR;
    }
    if (fread(data, 1, st_data.st_size, f) != st_data.st_size) {
        LOGE("Failed to read firmware data: %s\n", strerror(errno));
        return INSTALL_ERROR;
    }
    fclose(f);

    if (remember_firmware_update(type, data, st_data.st_size)) {
        LOGE("Can't store %s image\n", type);
        free(data);
        return INSTALL_ERROR;
    }
    free(filename);

    return INSTALL_SUCCESS;
}

// If the package contains an update binary, extract it and run it.
static int
try_update_binary(const char *path, ZipArchive *zip) {
    const ZipEntry* binary_entry =
            mzFindZipEntry(zip, ASSUMED_UPDATE_BINARY_NAME);
    if (binary_entry == NULL) {
        return INSTALL_CORRUPT;
    }

    char* binary = "/tmp/update_binary";
    unlink(binary);
    int fd = creat(binary, 0755);
    if (fd < 0) {
        LOGE("Can't make %s\n", binary);
        return 1;
    }
    bool ok = mzExtractZipEntryToFile(zip, binary_entry, fd);
    close(fd);

    if (!ok) {
        LOGE("Can't copy %s\n", ASSUMED_UPDATE_BINARY_NAME);
        return 1;
    }

    int pipefd[2];
    pipe(pipefd);

    // When executing the update binary contained in the package, the
    // arguments passed are:
    //
    //   - the version number for this interface (currently 1)
    //
    //   - an fd to which the program can write in order to update the
    //     progress bar.  The program can write single-line commands:
    //
    //        progress <frac> <secs>
    //            fill up <frac> of the progress bar over <secs> seconds.
    //
    //        firmware <"hboot"|"radio"> <filename>
    //            arrange to install the contents of <filename> in the
    //            given partition on reboot.
    //
    //   - the name of the package zip file.
    //

    char** args = malloc(sizeof(char*) * 5);
    args[0] = binary;
    args[1] = "1";
    args[2] = malloc(10);
    sprintf(args[2], "%d", pipefd[1]);
    args[3] = (char*)path;
    args[4] = NULL;

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        execv(binary, args);
        fprintf(stderr, "E:Can't run %s (%s)\n", binary, strerror(errno));
        _exit(-1);
    }
    close(pipefd[1]);

    char* firmware_type = NULL;
    char* firmware_filename = NULL;

    char buffer[81];
    FILE* from_child = fdopen(pipefd[0], "r");
    while (fgets(buffer, sizeof(buffer), from_child) != NULL) {
        LOGI("read: %s", buffer);

        char* command = strtok(buffer, " \n");
        if (command == NULL) {
            continue;
        } else if (strcmp(command, "progress") == 0) {
            char* fraction_s = strtok(NULL, " \n");
            char* seconds_s = strtok(NULL, " \n");

            float fraction = strtof(fraction_s, NULL);
            int seconds = strtol(seconds_s, NULL, 10);

            ui_show_progress(fraction * (1-VERIFICATION_PROGRESS_FRACTION),
                             seconds);
        } else if (strcmp(command, "firmware") == 0) {
            char* type = strtok(NULL, " \n");
            char* filename = strtok(NULL, " \n");

            if (type != NULL && filename != NULL) {
                if (firmware_type != NULL) {
                    LOGE("ignoring attempt to do multiple firmware updates");
                } else {
                    firmware_type = strdup(type);
                    firmware_filename = strdup(filename);
                }
            }
        } else {
            LOGE("unknown command [%s]\n", command);
        }
    }
    fclose(from_child);

    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        LOGE("Error in %s\n(Status %d)\n", path, WEXITSTATUS(status));
        return INSTALL_ERROR;
    }

    if (firmware_type != NULL) {
        return handle_firmware_update(firmware_type, firmware_filename);
    } else {
        return INSTALL_SUCCESS;
    }
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

    int result = try_update_binary(path, zip);
    if (result == INSTALL_SUCCESS || result == INSTALL_ERROR) {
        register_package_root(NULL, NULL);  // Unregister package root
        return result;
    }

    // if INSTALL_CORRUPT is returned, this package doesn't have an
    // update binary.  Fall back to the older mechanism of looking for
    // an update script.

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
