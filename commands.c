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

#undef NDEBUG

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unistd.h>

#include "amend/commands.h"
#include "commands.h"
#include "common.h"
#include "cutils/misc.h"
#include "cutils/properties.h"
#include "firmware.h"
#include "minzip/DirUtil.h"
#include "minzip/Zip.h"
#include "roots.h"

static int gDidShowProgress = 0;

#define UNUSED(p)   ((void)(p))

#define CHECK_BOOL() \
    do { \
        assert(argv == NULL); \
        if (argv != NULL) return -1; \
        assert(argc == true || argc == false); \
        if (argc != true && argc != false) return -1; \
    } while (false)

#define CHECK_WORDS() \
    do { \
        assert(argc >= 0); \
        if (argc < 0) return -1; \
        assert(argc == 0 || argv != NULL); \
        if (argc != 0 && argv == NULL) return -1; \
    } while (false)

#define CHECK_FN() \
    do { \
        CHECK_WORDS(); \
        assert(result != NULL); \
        if (result == NULL) return -1; \
    } while (false)

/*
 * Command definitions
 */

/* assert <boolexpr>
 */
static int
cmd_assert(const char *name, void *cookie, int argc, const char *argv[])
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_BOOL();

    /* If our argument is false, return non-zero (failure)
     * If our argument is true, return zero (success)
     */
    if (argc) {
        return 0;
    } else {
        return 1;
    }
}

/* format <root>
 */
static int
cmd_format(const char *name, void *cookie, int argc, const char *argv[])
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_WORDS();

    if (argc != 1) {
        LOGE("Command %s requires exactly one argument\n", name);
        return 1;
    }
    const char *root = argv[0];
    ui_print("Formatting %s...\n", root);

    int ret = format_root_device(root);
    if (ret != 0) {
        LOGE("Can't format %s\n", root);
        return 1;
    }

    return 0;
}

/* delete <file1> [<fileN> ...]
 * delete_recursive <file-or-dir1> [<file-or-dirN> ...]
 *
 * Like "rm -f", will try to delete every named file/dir, even if
 * earlier ones fail.  Recursive deletes that fail halfway through
 * give up early.
 */
static int
cmd_delete(const char *name, void *cookie, int argc, const char *argv[])
{
    UNUSED(cookie);
    CHECK_WORDS();
    int nerr = 0;
    bool recurse;

    if (argc < 1) {
        LOGE("Command %s requires at least one argument\n", name);
        return 1;
    }

    recurse = (strcmp(name, "delete_recursive") == 0);
    ui_print("Deleting files...\n");

    int i;
    for (i = 0; i < argc; i++) {
        const char *root_path = argv[i];
        char pathbuf[PATH_MAX];
        const char *path;

        /* This guarantees that all paths use "SYSTEM:"-style roots;
         * plain paths won't make it through translate_root_path().
         */
        path = translate_root_path(root_path, pathbuf, sizeof(pathbuf));
        if (path != NULL) {
            int ret = ensure_root_path_mounted(root_path);
            if (ret < 0) {
                LOGW("Can't mount volume to delete \"%s\"\n", root_path);
                nerr++;
                continue;
            }
            if (recurse) {
                ret = dirUnlinkHierarchy(path);
            } else {
                ret = unlink(path);
            }
            if (ret != 0 && errno != ENOENT) {
                LOGW("Can't delete %s\n(%s)\n", path, strerror(errno));
                nerr++;
            }
        } else {
            nerr++;
        }
    }
//TODO: add a way to fail if a delete didn't work

    return 0;
}

typedef struct {
    int num_done;
    int num_total;
} ExtractContext;

static void extract_count_cb(const char *fn, void *cookie)
{
   ++((ExtractContext*) cookie)->num_total;
}

static void extract_cb(const char *fn, void *cookie)
{
    // minzip writes the filename to the log, so we don't need to
    ExtractContext *ctx = (ExtractContext*) cookie;
    ui_set_progress((float) ++ctx->num_done / ctx->num_total);
}

/* copy_dir <src-dir> <dst-dir> [<timestamp>]
 *
 * The contents of <src-dir> will become the contents of <dst-dir>.
 * The original contents of <dst-dir> are preserved unless something
 * in <src-dir> overwrote them.
 *
 * e.g., for "copy_dir PKG:system SYSTEM:", the file "PKG:system/a"
 * would be copied to "SYSTEM:a".
 *
 * The specified timestamp (in decimal seconds since 1970) will be used,
 * or a fixed default timestamp will be supplied otherwise.
 */
static int
cmd_copy_dir(const char *name, void *cookie, int argc, const char *argv[])
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_WORDS();

    // To create a consistent system image, never use the clock for timestamps.
    struct utimbuf timestamp = { 1217592000, 1217592000 };  // 8/1/2008 default
    if (argc == 3) {
        char *end;
        time_t value = strtoul(argv[2], &end, 0);
        if (value == 0 || end[0] != '\0') {
            LOGE("Command %s: invalid timestamp \"%s\"\n", name, argv[2]);
            return 1;
        } else if (value < timestamp.modtime) {
            LOGE("Command %s: timestamp \"%s\" too early\n", name, argv[2]);
            return 1;
        }
        timestamp.modtime = timestamp.actime = value;
    } else if (argc != 2) {
        LOGE("Command %s requires exactly two arguments\n", name);
        return 1;
    }

    // Use 40% of the progress bar (80% post-verification) by default
    ui_print("Copying files...\n");
    if (!gDidShowProgress) ui_show_progress(DEFAULT_FILES_PROGRESS_FRACTION, 0);

    /* Mount the destination volume if it isn't already.
     */
    const char *dst_root_path = argv[1];
    int ret = ensure_root_path_mounted(dst_root_path);
    if (ret < 0) {
        LOGE("Can't mount %s\n", dst_root_path);
        return 1;
    }

    /* Get the real target path.
     */
    char dstpathbuf[PATH_MAX];
    const char *dst_path;
    dst_path = translate_root_path(dst_root_path,
            dstpathbuf, sizeof(dstpathbuf));
    if (dst_path == NULL) {
        LOGE("Command %s: bad destination path \"%s\"\n", name, dst_root_path);
        return 1;
    }

    /* Try to copy the directory.  The source may be inside a package.
     */
    const char *src_root_path = argv[0];
    char srcpathbuf[PATH_MAX];
    const char *src_path;
    if (is_package_root_path(src_root_path)) {
        const ZipArchive *package;
        src_path = translate_package_root_path(src_root_path,
                srcpathbuf, sizeof(srcpathbuf), &package);
        if (src_path == NULL) {
            LOGE("Command %s: bad source path \"%s\"\n", name, src_root_path);
            return 1;
        }

        /* Extract the files.  Set MZ_EXTRACT_FILES_ONLY, because only files
         * are validated by the signature.  Do a dry run first to count how
         * many there are (and find some errors early).
         */
        ExtractContext ctx;
        ctx.num_done = 0;
        ctx.num_total = 0;

        if (!mzExtractRecursive(package, src_path, dst_path,
                    MZ_EXTRACT_FILES_ONLY | MZ_EXTRACT_DRY_RUN,
                    &timestamp, extract_count_cb, (void *) &ctx) ||
            !mzExtractRecursive(package, src_path, dst_path,
                    MZ_EXTRACT_FILES_ONLY,
                    &timestamp, extract_cb, (void *) &ctx)) {
            LOGW("Command %s: couldn't extract \"%s\" to \"%s\"\n",
                    name, src_root_path, dst_root_path);
            return 1;
        }
    } else {
        LOGE("Command %s: non-package source path \"%s\" not yet supported\n",
                name, src_root_path);
//xxx mount the src volume
//xxx
        return 255;
    }

    return 0;
}

/* run_program <program-file> [<args> ...]
 *
 * Run an external program included in the update package.
 */
static int
cmd_run_program(const char *name, void *cookie, int argc, const char *argv[])
{
    UNUSED(cookie);
    CHECK_WORDS();

    if (argc < 1) {
        LOGE("Command %s requires at least one argument\n", name);
        return 1;
    }

    // Copy the program file to temporary storage.
    if (!is_package_root_path(argv[0])) {
        LOGE("Command %s: non-package program file \"%s\" not supported\n",
                name, argv[0]);
        return 1;
    }

    char path[PATH_MAX];
    const ZipArchive *package;
    if (!translate_package_root_path(argv[0], path, sizeof(path), &package)) {
        LOGE("Command %s: bad source path \"%s\"\n", name, argv[0]);
        return 1;
    }

    const ZipEntry *entry = mzFindZipEntry(package, path);
    if (entry == NULL) {
        LOGE("Can't find %s\n", path);
        return 1;
    }

    static const char *binary = "/tmp/run_program_binary";
    unlink(binary);  // just to be sure
    int fd = creat(binary, 0755);
    if (fd < 0) {
        LOGE("Can't make %s\n", binary);
        return 1;
    }
    bool ok = mzExtractZipEntryToFile(package, entry, fd);
    close(fd);

    if (!ok) {
        LOGE("Can't copy %s\n", path);
        return 1;
    }

    // Create a copy of argv to NULL-terminate it, as execv requires
    char **args = (char **) malloc(sizeof(char*) * (argc + 1));
    memcpy(args, argv, sizeof(char*) * argc);
    args[argc] = NULL;

    pid_t pid = fork();
    if (pid == 0) {
        execv(binary, args);
        fprintf(stderr, "E:Can't run %s\n(%s)\n", binary, strerror(errno));
        _exit(-1);
    }

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return 0;
    } else {
        LOGE("Error in %s\n(Status %d)\n", path, status);
        return 1;
    }
}

/* set_perm <uid> <gid> <mode> <path> [... <pathN>]
 * set_perm_recursive <uid> <gid> <dir-mode> <file-mode> <path> [... <pathN>]
 *
 * Like "chmod", "chown" and "chgrp" all in one, set ownership and permissions
 * of single files or entire directory trees.  Any error causes failure.
 * User, group, and modes must all be integer values (hex or octal OK).
 */
static int
cmd_set_perm(const char *name, void *cookie, int argc, const char *argv[])
{
    UNUSED(cookie);
    CHECK_WORDS();
    bool recurse = !strcmp(name, "set_perm_recursive");

    int min_args = 4 + (recurse ? 1 : 0);
    if (argc < min_args) {
        LOGE("Command %s requires at least %d args\n", name, min_args);
        return 1;
    }

    // All the arguments except the path(s) are numeric.
    int i, n[min_args - 1];
    for (i = 0; i < min_args - 1; ++i) {
        char *end;
        n[i] = strtoul(argv[i], &end, 0);
        if (end[0] != '\0' || argv[i][0] == '\0') {
            LOGE("Command %s: invalid argument \"%s\"\n", name, argv[i]);
            return 1;
        }
    }

    for (i = min_args - 1; i < min_args; ++i) {
        char path[PATH_MAX];
        if (translate_root_path(argv[i], path, sizeof(path)) == NULL) {
            LOGE("Command %s: bad path \"%s\"\n", name, argv[i]);
            return 1;
        }

        if (ensure_root_path_mounted(argv[i])) {
            LOGE("Can't mount %s\n", argv[i]);
            return 1;
        }

        if (recurse
                ? dirSetHierarchyPermissions(path, n[0], n[1], n[2], n[3])
                : (chown(path, n[0], n[1]) || chmod(path, n[2]))) {
           LOGE("Can't chown/mod %s\n(%s)\n", path, strerror(errno));
           return 1;
        }
    }

    return 0;
}

/* show_progress <fraction> <duration>
 *
 * Use <fraction> of the on-screen progress meter for the next operation,
 * automatically advancing the meter over <duration> seconds (or more rapidly
 * if the actual rate of progress can be determined).
 */
static int
cmd_show_progress(const char *name, void *cookie, int argc, const char *argv[])
{
    UNUSED(cookie);
    CHECK_WORDS();

    if (argc != 2) {
        LOGE("Command %s requires exactly two arguments\n", name);
        return 1;
    }

    char *end;
    double fraction = strtod(argv[0], &end);
    if (end[0] != '\0' || argv[0][0] == '\0' || fraction < 0 || fraction > 1) {
        LOGE("Command %s: invalid fraction \"%s\"\n", name, argv[0]);
        return 1;
    }

    int duration = strtoul(argv[1], &end, 0);
    if (end[0] != '\0' || argv[0][0] == '\0') {
        LOGE("Command %s: invalid duration \"%s\"\n", name, argv[1]);
        return 1;
    }

    // Half of the progress bar is taken by verification,
    // so everything that happens during installation is scaled.
    ui_show_progress(fraction * (1 - VERIFICATION_PROGRESS_FRACTION), duration);
    gDidShowProgress = 1;
    return 0;
}

/* symlink <link-target> <link-path>
 *
 * Create a symlink, like "ln -s".  The link path must not exist already.
 * Note that <link-path> is in root:path format, but <link-target> is
 * for the target filesystem (and may be relative).
 */
static int
cmd_symlink(const char *name, void *cookie, int argc, const char *argv[])
{
    UNUSED(cookie);
    CHECK_WORDS();

    if (argc != 2) {
        LOGE("Command %s requires exactly two arguments\n", name);
        return 1;
    }

    char path[PATH_MAX];
    if (translate_root_path(argv[1], path, sizeof(path)) == NULL) {
        LOGE("Command %s: bad path \"%s\"\n", name, argv[1]);
        return 1;
    }

    if (ensure_root_path_mounted(argv[1])) {
        LOGE("Can't mount %s\n", argv[1]);
        return 1;
    }

    if (symlink(argv[0], path)) {
        LOGE("Can't symlink %s\n", path);
        return 1;
    }

    return 0;
}

struct FirmwareContext {
    size_t total_bytes, done_bytes;
    char *data;
};

static bool firmware_fn(const unsigned char *data, int data_len, void *cookie)
{
    struct FirmwareContext *context = (struct FirmwareContext*) cookie;
    if (context->done_bytes + data_len > context->total_bytes) {
        LOGE("Data overrun in firmware\n");
        return false;  // Should not happen, but let's be safe.
    }

    memcpy(context->data + context->done_bytes, data, data_len);
    context->done_bytes += data_len;
    ui_set_progress(context->done_bytes * 1.0 / context->total_bytes);
    return true;
}

/* write_radio_image <src-image>
 * write_hboot_image <src-image>
 * Doesn't actually take effect until the rest of installation finishes.
 */
static int
cmd_write_firmware_image(const char *name, void *cookie,
        int argc, const char *argv[])
{
    UNUSED(cookie);
    CHECK_WORDS();

    if (argc != 1) {
        LOGE("Command %s requires exactly one argument\n", name);
        return 1;
    }

    const char *type;
    if (!strcmp(name, "write_radio_image")) {
        type = "radio";
    } else if (!strcmp(name, "write_hboot_image")) {
        type = "hboot";
    } else {
        LOGE("Unknown firmware update command %s\n", name);
        return 1;
    }

    if (!is_package_root_path(argv[0])) {
        LOGE("Command %s: non-package image file \"%s\" not supported\n",
                name, argv[0]);
        return 1;
    }

    ui_print("Extracting %s image...\n", type);
    char path[PATH_MAX];
    const ZipArchive *package;
    if (!translate_package_root_path(argv[0], path, sizeof(path), &package)) {
        LOGE("Command %s: bad source path \"%s\"\n", name, argv[0]);
        return 1;
    }

    const ZipEntry *entry = mzFindZipEntry(package, path);
    if (entry == NULL) {
        LOGE("Can't find %s\n", path);
        return 1;
    }

    // Load the update image into RAM.
    struct FirmwareContext context;
    context.total_bytes = mzGetZipEntryUncompLen(entry);
    context.done_bytes = 0;
    context.data = malloc(context.total_bytes);
    if (context.data == NULL) {
        LOGE("Can't allocate %d bytes for %s\n", context.total_bytes, argv[0]);
        return 1;
    }

    if (!mzProcessZipEntryContents(package, entry, firmware_fn, &context) ||
        context.done_bytes != context.total_bytes) {
        LOGE("Can't read %s\n", argv[0]);
        free(context.data);
        return 1;
    }

    if (remember_firmware_update(type, context.data, context.total_bytes)) {
        LOGE("Can't store %s image\n", type);
        free(context.data);
        return 1;
    }

    return 0;
}

static bool write_raw_image_process_fn(
        const unsigned char *data,
        int data_len, void *ctx)
{
    int r = mtd_write_data((MtdWriteContext*)ctx, (const char *)data, data_len);
    if (r == data_len) return true;
    LOGE("%s\n", strerror(errno));
    return false;
}

/* write_raw_image <src-image> <dest-root>
 */
static int
cmd_write_raw_image(const char *name, void *cookie,
        int argc, const char *argv[])
{
    UNUSED(cookie);
    CHECK_WORDS();

    if (argc != 2) {
        LOGE("Command %s requires exactly two arguments\n", name);
        return 1;
    }

    // Use 10% of the progress bar (20% post-verification) by default
    const char *src_root_path = argv[0];
    const char *dst_root_path = argv[1];
    ui_print("Writing %s...\n", dst_root_path);
    if (!gDidShowProgress) ui_show_progress(DEFAULT_IMAGE_PROGRESS_FRACTION, 0);

    /* Find the source image, which is probably in a package.
     */
    if (!is_package_root_path(src_root_path)) {
        LOGE("Command %s: non-package source path \"%s\" not yet supported\n",
                name, src_root_path);
        return 255;
    }

    /* Get the package.
     */
    char srcpathbuf[PATH_MAX];
    const char *src_path;
    const ZipArchive *package;
    src_path = translate_package_root_path(src_root_path,
            srcpathbuf, sizeof(srcpathbuf), &package);
    if (src_path == NULL) {
        LOGE("Command %s: bad source path \"%s\"\n", name, src_root_path);
        return 1;
    }

    /* Get the entry.
     */
    const ZipEntry *entry = mzFindZipEntry(package, src_path);
    if (entry == NULL) {
        LOGE("Missing file %s\n", src_path);
        return 1;
    }

    /* Unmount the destination root if it isn't already.
     */
    int ret = ensure_root_path_unmounted(dst_root_path);
    if (ret < 0) {
        LOGE("Can't unmount %s\n", dst_root_path);
        return 1;
    }

    /* Open the partition for writing.
     */
    const MtdPartition *partition = get_root_mtd_partition(dst_root_path);
    if (partition == NULL) {
        LOGE("Can't find %s\n", dst_root_path);
        return 1;
    }
    MtdWriteContext *context = mtd_write_partition(partition);
    if (context == NULL) {
        LOGE("Can't open %s\n", dst_root_path);
        return 1;
    }

    /* Extract and write the image.
     */
    bool ok = mzProcessZipEntryContents(package, entry,
            write_raw_image_process_fn, context);
    if (!ok) {
        LOGE("Error writing %s\n", dst_root_path);
        mtd_write_close(context);
        return 1;
    }

    if (mtd_erase_blocks(context, -1) == (off_t) -1) {
        LOGE("Error finishing %s\n", dst_root_path);
        mtd_write_close(context);
        return -1;
    }

    if (mtd_write_close(context)) {
        LOGE("Error closing %s\n", dst_root_path);
        return -1;
    }
    return 0;
}

/* mark <resource> dirty|clean
 */
static int
cmd_mark(const char *name, void *cookie, int argc, const char *argv[])
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_WORDS();
//xxx when marking, save the top-level hash at the mark point
//    so we can retry on failure.  Otherwise the hashes won't match,
//    or someone could intentionally dirty the FS to force a downgrade
//xxx
    return -1;
}

/* done
 */
static int
cmd_done(const char *name, void *cookie, int argc, const char *argv[])
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_WORDS();
//xxx
    return -1;
}


/*
 * Function definitions
 */

/* compatible_with(<version>)
 *
 * Returns "true" if this version of the script parser and command
 * set supports the named version.
 */
static int
fn_compatible_with(const char *name, void *cookie, int argc, const char *argv[],
        char **result, size_t *resultLen)
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_FN();

    if (argc != 1) {
        fprintf(stderr, "%s: wrong number of arguments (%d)\n",
                name, argc);
        return 1;
    }

    if (!strcmp(argv[0], "0.1") || !strcmp(argv[0], "0.2")) {
        *result = strdup("true");
    } else {
        *result = strdup("");
    }
    if (resultLen != NULL) {
        *resultLen = strlen(*result);
    }
    return 0;
}

/* update_forced()
 *
 * Returns "true" if some system setting has determined that
 * the update should happen no matter what.
 */
static int
fn_update_forced(const char *name, void *cookie, int argc, const char *argv[],
        char **result, size_t *resultLen)
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_FN();

    if (argc != 0) {
        fprintf(stderr, "%s: wrong number of arguments (%d)\n",
                name, argc);
        return 1;
    }

    //xxx check some global or property
    bool force = true;
    if (force) {
        *result = strdup("true");
    } else {
        *result = strdup("");
    }
    if (resultLen != NULL) {
        *resultLen = strlen(*result);
    }

    return 0;
}

/* get_mark(<resource>)
 *
 * Returns the current mark associated with the provided resource.
 */
static int
fn_get_mark(const char *name, void *cookie, int argc, const char *argv[],
        char **result, size_t *resultLen)
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_FN();

    if (argc != 1) {
        fprintf(stderr, "%s: wrong number of arguments (%d)\n",
                name, argc);
        return 1;
    }

    //xxx look up the value
    *result = strdup("");
    if (resultLen != NULL) {
        *resultLen = strlen(*result);
    }

    return 0;
}

/* hash_dir(<path-to-directory>)
 */
static int
fn_hash_dir(const char *name, void *cookie, int argc, const char *argv[],
        char **result, size_t *resultLen)
{
    int ret = -1;

    UNUSED(name);
    UNUSED(cookie);
    CHECK_FN();

    const char *dir;
    if (argc != 1) {
        fprintf(stderr, "%s: wrong number of arguments (%d)\n",
                name, argc);
        return 1;
    } else {
        dir = argv[0];
    }

    return ret;
}

/* matches(<str>, <str1> [, <strN>...])
 * If <str> matches (strcmp) any of <str1>...<strN>, returns <str>,
 * otherwise returns "".
 *
 * E.g., assert matches(hash_dir("/path"), "hash1", "hash2")
 */
static int
fn_matches(const char *name, void *cookie, int argc, const char *argv[],
        char **result, size_t *resultLen)
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_FN();

    if (argc < 2) {
        fprintf(stderr, "%s: not enough arguments (%d < 2)\n",
                name, argc);
        return 1;
    }

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[0], argv[i]) == 0) {
            *result = strdup(argv[0]);
            if (resultLen != NULL) {
                *resultLen = strlen(*result);
            }
            return 0;
        }
    }

    *result = strdup("");
    if (resultLen != NULL) {
        *resultLen = 1;
    }
    return 0;
}

/* concat(<str>, <str1> [, <strN>...])
 * Returns the concatenation of all strings.
 */
static int
fn_concat(const char *name, void *cookie, int argc, const char *argv[],
        char **result, size_t *resultLen)
{
    UNUSED(name);
    UNUSED(cookie);
    CHECK_FN();

    size_t totalLen = 0;
    int i;
    for (i = 0; i < argc; i++) {
        totalLen += strlen(argv[i]);
    }

    char *s = (char *)malloc(totalLen + 1);
    if (s == NULL) {
        return -1;
    }
    s[totalLen] = '\0';
    for (i = 0; i < argc; i++) {
        //TODO: keep track of the end to avoid walking the string each time
        strcat(s, argv[i]);
    }
    *result = s;
    if (resultLen != NULL) {
        *resultLen = strlen(s);
    }

    return 0;
}

/* getprop(<property>)
 * Returns the named Android system property value, or "" if not set.
 */
static int
fn_getprop(const char *name, void *cookie, int argc, const char *argv[],
        char **result, size_t *resultLen)
{
    UNUSED(cookie);
    CHECK_FN();

    if (argc != 1) {
        LOGE("Command %s requires exactly one argument\n", name);
        return 1;
    }

    char value[PROPERTY_VALUE_MAX];
    property_get(argv[0], value, "");

    *result = strdup(value);
    if (resultLen != NULL) {
        *resultLen = strlen(*result);
    }

    return 0;
}

/* file_contains(<filename>, <substring>)
 * Returns "true" if the file exists and contains the specified substring.
 */
static int
fn_file_contains(const char *name, void *cookie, int argc, const char *argv[],
        char **result, size_t *resultLen)
{
    UNUSED(cookie);
    CHECK_FN();

    if (argc != 2) {
        LOGE("Command %s requires exactly two arguments\n", name);
        return 1;
    }

    char pathbuf[PATH_MAX];
    const char *root_path = argv[0];
    const char *path = translate_root_path(root_path, pathbuf, sizeof(pathbuf));
    if (path == NULL) {
        LOGE("Command %s: bad path \"%s\"\n", name, root_path);
        return 1;
    }

    if (ensure_root_path_mounted(root_path)) {
        LOGE("Can't mount %s\n", root_path);
        return 1;
    }

    const char *needle = argv[1];
    char *haystack = (char*) load_file(path, NULL);
    if (haystack == NULL) {
        LOGI("%s: Can't read \"%s\" (%s)\n", name, path, strerror(errno));
        *result = "";  /* File not found is not an error. */
    } else if (strstr(haystack, needle) == NULL) {
        LOGI("%s: Can't find \"%s\" in \"%s\"\n", name, needle, path);
        *result = strdup("");
        free(haystack);
    } else {
        *result = strdup("true");
        free(haystack);
    }

    if (resultLen != NULL) {
        *resultLen = strlen(*result);
    }
    return 0;
}

int
register_update_commands(RecoveryCommandContext *ctx)
{
    int ret;

    ret = commandInit();
    if (ret < 0) return ret;

    /*
     * Commands
     */

    ret = registerCommand("assert", CMD_ARGS_BOOLEAN, cmd_assert, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerCommand("delete", CMD_ARGS_WORDS, cmd_delete, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerCommand("delete_recursive", CMD_ARGS_WORDS, cmd_delete,
            (void *)ctx);
    if (ret < 0) return ret;

    ret = registerCommand("copy_dir", CMD_ARGS_WORDS,
            cmd_copy_dir, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerCommand("run_program", CMD_ARGS_WORDS,
            cmd_run_program, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerCommand("set_perm", CMD_ARGS_WORDS,
            cmd_set_perm, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerCommand("set_perm_recursive", CMD_ARGS_WORDS,
            cmd_set_perm, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerCommand("show_progress", CMD_ARGS_WORDS,
            cmd_show_progress, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerCommand("symlink", CMD_ARGS_WORDS, cmd_symlink, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerCommand("format", CMD_ARGS_WORDS, cmd_format, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerCommand("write_radio_image", CMD_ARGS_WORDS,
            cmd_write_firmware_image, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerCommand("write_hboot_image", CMD_ARGS_WORDS,
            cmd_write_firmware_image, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerCommand("write_raw_image", CMD_ARGS_WORDS,
            cmd_write_raw_image, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerCommand("mark", CMD_ARGS_WORDS, cmd_mark, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerCommand("done", CMD_ARGS_WORDS, cmd_done, (void *)ctx);
    if (ret < 0) return ret;

    /*
     * Functions
     */

    ret = registerFunction("compatible_with", fn_compatible_with, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerFunction("update_forced", fn_update_forced, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerFunction("get_mark", fn_get_mark, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerFunction("hash_dir", fn_hash_dir, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerFunction("matches", fn_matches, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerFunction("concat", fn_concat, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerFunction("getprop", fn_getprop, (void *)ctx);
    if (ret < 0) return ret;

    ret = registerFunction("file_contains", fn_file_contains, (void *)ctx);
    if (ret < 0) return ret;

    return 0;
}
