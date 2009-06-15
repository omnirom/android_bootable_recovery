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
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cutils/misc.h"
#include "cutils/properties.h"
#include "edify/expr.h"
#include "minzip/DirUtil.h"
#include "mtdutils/mounts.h"
#include "mtdutils/mtdutils.h"
#include "updater.h"

char* ErrorAbort(State* state, char* format, ...) {
    char* buffer = malloc(4096);
    va_list v;
    va_start(v, format);
    vsnprintf(buffer, 4096, format, v);
    va_end(v);
    free(state->errmsg);
    state->errmsg = buffer;
    return NULL;
}


// mount(type, location, mount_point)
//
//   what:  type="MTD"   location="<partition>"            to mount a yaffs2 filesystem
//          type="vfat"  location="/dev/block/<whatever>"  to mount a device
char* MountFn(const char* name, State* state, int argc, Expr* argv[]) {
    char* result = NULL;
    if (argc != 3) {
        return ErrorAbort(state, "%s() expects 3 args, got %d", name, argc);
    }
    char* type;
    char* location;
    char* mount_point;
    if (ReadArgs(state, argv, 3, &type, &location, &mount_point) < 0) {
        return NULL;
    }

    if (strlen(type) == 0) {
        ErrorAbort(state, "type argument to %s() can't be empty", name);
        goto done;
    }
    if (strlen(location) == 0) {
        ErrorAbort(state, "location argument to %s() can't be empty", name);
        goto done;
    }
    if (strlen(mount_point) == 0) {
        ErrorAbort(state, "mount_point argument to %s() can't be empty", name);
        goto done;
    }

    mkdir(mount_point, 0755);

    if (strcmp(type, "MTD") == 0) {
        mtd_scan_partitions();
        const MtdPartition* mtd;
        mtd = mtd_find_partition_by_name(location);
        if (mtd == NULL) {
            fprintf(stderr, "%s: no mtd partition named \"%s\"",
                    name, location);
            result = strdup("");
            goto done;
        }
        if (mtd_mount_partition(mtd, mount_point, "yaffs2", 0 /* rw */) != 0) {
            fprintf(stderr, "mtd mount of %s failed: %s\n",
                    location, strerror(errno));
            result = strdup("");
            goto done;
        }
        result = mount_point;
    } else {
        if (mount(location, mount_point, type,
                  MS_NOATIME | MS_NODEV | MS_NODIRATIME, "") < 0) {
            result = strdup("");
        } else {
            result = mount_point;
        }
    }

done:
    free(type);
    free(location);
    if (result != mount_point) free(mount_point);
    return result;
}


// is_mounted(mount_point)
char* IsMountedFn(const char* name, State* state, int argc, Expr* argv[]) {
    char* result = NULL;
    if (argc != 1) {
        return ErrorAbort(state, "%s() expects 1 arg, got %d", name, argc);
    }
    char* mount_point;
    if (ReadArgs(state, argv, 1, &mount_point) < 0) {
        return NULL;
    }
    if (strlen(mount_point) == 0) {
        ErrorAbort(state, "mount_point argument to unmount() can't be empty");
        goto done;
    }

    scan_mounted_volumes();
    const MountedVolume* vol = find_mounted_volume_by_mount_point(mount_point);
    if (vol == NULL) {
        result = strdup("");
    } else {
        result = mount_point;
    }

done:
    if (result != mount_point) free(mount_point);
    return result;
}


char* UnmountFn(const char* name, State* state, int argc, Expr* argv[]) {
    char* result = NULL;
    if (argc != 1) {
        return ErrorAbort(state, "%s() expects 1 arg, got %d", name, argc);
    }
    char* mount_point;
    if (ReadArgs(state, argv, 1, &mount_point) < 0) {
        return NULL;
    }
    if (strlen(mount_point) == 0) {
        ErrorAbort(state, "mount_point argument to unmount() can't be empty");
        goto done;
    }

    scan_mounted_volumes();
    const MountedVolume* vol = find_mounted_volume_by_mount_point(mount_point);
    if (vol == NULL) {
        fprintf(stderr, "unmount of %s failed; no such volume\n", mount_point);
        result = strdup("");
    } else {
        unmount_mounted_volume(vol);
        result = mount_point;
    }

done:
    if (result != mount_point) free(mount_point);
    return result;
}


// format(type, location)
//
//    type="MTD"  location=partition
char* FormatFn(const char* name, State* state, int argc, Expr* argv[]) {
    char* result = NULL;
    if (argc != 2) {
        return ErrorAbort(state, "%s() expects 2 args, got %d", name, argc);
    }
    char* type;
    char* location;
    if (ReadArgs(state, argv, 2, &type, &location) < 0) {
        return NULL;
    }

    if (strlen(type) == 0) {
        ErrorAbort(state, "type argument to %s() can't be empty", name);
        goto done;
    }
    if (strlen(location) == 0) {
        ErrorAbort(state, "location argument to %s() can't be empty", name);
        goto done;
    }

    if (strcmp(type, "MTD") == 0) {
        mtd_scan_partitions();
        const MtdPartition* mtd = mtd_find_partition_by_name(location);
        if (mtd == NULL) {
            fprintf(stderr, "%s: no mtd partition named \"%s\"",
                    name, location);
            result = strdup("");
            goto done;
        }
        MtdWriteContext* ctx = mtd_write_partition(mtd);
        if (ctx == NULL) {
            fprintf(stderr, "%s: can't write \"%s\"", name, location);
            result = strdup("");
            goto done;
        }
        if (mtd_erase_blocks(ctx, -1) == -1) {
            mtd_write_close(ctx);
            fprintf(stderr, "%s: failed to erase \"%s\"", name, location);
            result = strdup("");
            goto done;
        }
        if (mtd_write_close(ctx) != 0) {
            fprintf(stderr, "%s: failed to close \"%s\"", name, location);
            result = strdup("");
            goto done;
        }
        result = location;
    } else {
        fprintf(stderr, "%s: unsupported type \"%s\"", name, type);
    }

done:
    free(type);
    if (result != location) free(location);
    return result;
}


char* DeleteFn(const char* name, State* state, int argc, Expr* argv[]) {
    char** paths = malloc(argc * sizeof(char*));
    int i;
    for (i = 0; i < argc; ++i) {
        paths[i] = Evaluate(state, argv[i]);
        if (paths[i] == NULL) {
            int j;
            for (j = 0; j < i; ++i) {
                free(paths[j]);
            }
            free(paths);
            return NULL;
        }
    }

    bool recursive = (strcmp(name, "delete_recursive") == 0);

    int success = 0;
    for (i = 0; i < argc; ++i) {
        if ((recursive ? dirUnlinkHierarchy(paths[i]) : unlink(paths[i])) == 0)
            ++success;
        free(paths[i]);
    }
    free(paths);

    char buffer[10];
    sprintf(buffer, "%d", success);
    return strdup(buffer);
}


char* ShowProgressFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc != 2) {
        return ErrorAbort(state, "%s() expects 2 args, got %d", name, argc);
    }
    char* frac_str;
    char* sec_str;
    if (ReadArgs(state, argv, 2, &frac_str, &sec_str) < 0) {
        return NULL;
    }

    double frac = strtod(frac_str, NULL);
    int sec = strtol(sec_str, NULL, 10);

    UpdaterInfo* ui = (UpdaterInfo*)(state->cookie);
    fprintf(ui->cmd_pipe, "progress %f %d\n", frac, sec);

    free(frac_str);
    free(sec_str);
    return strdup("");
}

// package_extract_dir(package_path, destination_path)
char* PackageExtractDirFn(const char* name, State* state,
                          int argc, Expr* argv[]) {
    if (argc != 2) {
        return ErrorAbort(state, "%s() expects 2 args, got %d", name, argc);
    }
    char* zip_path;
    char* dest_path;
    if (ReadArgs(state, argv, 2, &zip_path, &dest_path) < 0) return NULL;

    ZipArchive* za = ((UpdaterInfo*)(state->cookie))->package_zip;

    // To create a consistent system image, never use the clock for timestamps.
    struct utimbuf timestamp = { 1217592000, 1217592000 };  // 8/1/2008 default

    bool success = mzExtractRecursive(za, zip_path, dest_path,
                                      MZ_EXTRACT_FILES_ONLY, &timestamp,
                                      NULL, NULL);
    free(zip_path);
    free(dest_path);
    return strdup(success ? "t" : "");
}


// package_extract_file(package_path, destination_path)
char* PackageExtractFileFn(const char* name, State* state,
                           int argc, Expr* argv[]) {
    if (argc != 2) {
        return ErrorAbort(state, "%s() expects 2 args, got %d", name, argc);
    }
    char* zip_path;
    char* dest_path;
    if (ReadArgs(state, argv, 2, &zip_path, &dest_path) < 0) return NULL;

    bool success = false;

    ZipArchive* za = ((UpdaterInfo*)(state->cookie))->package_zip;
    const ZipEntry* entry = mzFindZipEntry(za, zip_path);
    if (entry == NULL) {
        fprintf(stderr, "%s: no %s in package\n", name, zip_path);
        goto done;
    }

    FILE* f = fopen(dest_path, "wb");
    if (f == NULL) {
        fprintf(stderr, "%s: can't open %s for write: %s\n",
                name, dest_path, strerror(errno));
        goto done;
    }
    success = mzExtractZipEntryToFile(za, entry, fileno(f));
    fclose(f);

  done:
    free(zip_path);
    free(dest_path);
    return strdup(success ? "t" : "");
}


// symlink target src1 src2 ...
char* SymlinkFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc == 0) {
        return ErrorAbort(state, "%s() expects 1+ args, got %d", name, argc);
    }
    char* target;
    target = Evaluate(state, argv[0]);
    if (target == NULL) return NULL;

    char** srcs = ReadVarArgs(state, argc-1, argv+1);
    if (srcs == NULL) {
        free(target);
        return NULL;
    }

    int i;
    for (i = 0; i < argc-1; ++i) {
        symlink(target, srcs[i]);
        free(srcs[i]);
    }
    free(srcs);
    return strdup("");
}


char* SetPermFn(const char* name, State* state, int argc, Expr* argv[]) {
    char* result = NULL;
    bool recursive = (strcmp(name, "set_perm_recursive") == 0);

    int min_args = 4 + (recursive ? 1 : 0);
    if (argc < min_args) {
        return ErrorAbort(state, "%s() expects %d+ args, got %d", name, argc);
    }

    char** args = ReadVarArgs(state, argc, argv);
    if (args == NULL) return NULL;

    char* end;
    int i;

    int uid = strtoul(args[0], &end, 0);
    if (*end != '\0' || args[0][0] == 0) {
        ErrorAbort(state, "%s: \"%s\" not a valid uid", name, args[0]);
        goto done;
    }

    int gid = strtoul(args[1], &end, 0);
    if (*end != '\0' || args[1][0] == 0) {
        ErrorAbort(state, "%s: \"%s\" not a valid gid", name, args[1]);
        goto done;
    }

    if (recursive) {
        int dir_mode = strtoul(args[2], &end, 0);
        if (*end != '\0' || args[2][0] == 0) {
            ErrorAbort(state, "%s: \"%s\" not a valid dirmode", name, args[2]);
            goto done;
        }

        int file_mode = strtoul(args[3], &end, 0);
        if (*end != '\0' || args[3][0] == 0) {
            ErrorAbort(state, "%s: \"%s\" not a valid filemode",
                       name, args[3]);
            goto done;
        }

        for (i = 4; i < argc; ++i) {
            dirSetHierarchyPermissions(args[i], uid, gid, dir_mode, file_mode);
        }
    } else {
        int mode = strtoul(args[2], &end, 0);
        if (*end != '\0' || args[2][0] == 0) {
            ErrorAbort(state, "%s: \"%s\" not a valid mode", name, args[2]);
            goto done;
        }

        for (i = 4; i < argc; ++i) {
            chown(args[i], uid, gid);
            chmod(args[i], mode);
        }
    }
    result = strdup("");

done:
    for (i = 0; i < argc; ++i) {
        free(args[i]);
    }
    free(args);

    return result;
}


char* GetPropFn(const char* name, State* state, int argc, Expr* argv[]) {
    if (argc != 1) {
        return ErrorAbort(state, "%s() expects 1 arg, got %d", name, argc);
    }
    char* key;
    key = Evaluate(state, argv[0]);
    if (key == NULL) return NULL;

    char value[PROPERTY_VALUE_MAX];
    property_get(key, value, "");
    free(key);

    return strdup(value);
}


static bool write_raw_image_cb(const unsigned char* data,
                               int data_len, void* ctx) {
    int r = mtd_write_data((MtdWriteContext*)ctx, (const char *)data, data_len);
    if (r == data_len) return true;
    fprintf(stderr, "%s\n", strerror(errno));
    return false;
}

// write_raw_image(file, partition)
char* WriteRawImageFn(const char* name, State* state, int argc, Expr* argv[]) {
    char* result = NULL;

    char* partition;
    char* filename;
    if (ReadArgs(state, argv, 2, &filename, &partition) < 0) {
        return NULL;
    }

    if (strlen(partition) == 0) {
        ErrorAbort(state, "partition argument to %s can't be empty", name);
        goto done;
    }
    if (strlen(filename) == 0) {
        ErrorAbort(state, "file argument to %s can't be empty", name);
        goto done;
    }

    mtd_scan_partitions();
    const MtdPartition* mtd = mtd_find_partition_by_name(partition);
    if (mtd == NULL) {
        fprintf(stderr, "%s: no mtd partition named \"%s\"\n", name, partition);
        result = strdup("");
        goto done;
    }

    MtdWriteContext* ctx = mtd_write_partition(mtd);
    if (ctx == NULL) {
        fprintf(stderr, "%s: can't write mtd partition \"%s\"\n",
                name, partition);
        result = strdup("");
        goto done;
    }

    bool success;

    FILE* f = fopen(filename, "rb");
    if (f == NULL) {
        fprintf(stderr, "%s: can't open %s: %s\n",
                name, filename, strerror(errno));
        result = strdup("");
        goto done;
    }

    success = true;
    char* buffer = malloc(BUFSIZ);
    int read;
    while (success && (read = fread(buffer, 1, BUFSIZ, f)) > 0) {
        int wrote = mtd_write_data(ctx, buffer, read);
        success = success && (wrote == read);
        if (!success) {
            fprintf(stderr, "mtd_write_data to %s failed: %s\n",
                    partition, strerror(errno));
        }
    }
    free(buffer);
    fclose(f);

    if (mtd_erase_blocks(ctx, -1) == -1) {
        fprintf(stderr, "%s: error erasing blocks of %s\n", name, partition);
    }
    if (mtd_write_close(ctx) != 0) {
        fprintf(stderr, "%s: error closing write of %s\n", name, partition);
    }

    printf("%s %s partition from %s\n",
           success ? "wrote" : "failed to write", partition, filename);

    result = success ? partition : strdup("");

done:
    if (result != partition) free(partition);
    free(filename);
    return result;
}

// write_firmware_image(file, partition)
//
//    partition is "radio" or "hboot"
//    file is not used until after updater exits
//
// TODO: this should live in some HTC-specific library
char* WriteFirmwareImageFn(const char* name, State* state,
                           int argc, Expr* argv[]) {
    char* result = NULL;

    char* partition;
    char* filename;
    if (ReadArgs(state, argv, 2, &filename, &partition) < 0) {
        return NULL;
    }

    if (strlen(partition) == 0) {
        ErrorAbort(state, "partition argument to %s can't be empty", name);
        goto done;
    }
    if (strlen(filename) == 0) {
        ErrorAbort(state, "file argument to %s can't be empty", name);
        goto done;
    }

    FILE* cmd = ((UpdaterInfo*)(state->cookie))->cmd_pipe;
    fprintf(cmd, "firmware %s %s\n", partition, filename);

    printf("will write %s firmware from %s\n", partition, filename);
    result = partition;

done:
    if (result != partition) free(partition);
    free(filename);
    return result;
}


extern int applypatch(int argc, char** argv);

// apply_patch(srcfile, tgtfile, tgtsha1, tgtsize, sha1:patch, ...)
// apply_patch_check(file, sha1, ...)
// apply_patch_space(bytes)
char* ApplyPatchFn(const char* name, State* state, int argc, Expr* argv[]) {
    printf("in applypatchfn (%s)\n", name);

    char* prepend = NULL;
    if (strstr(name, "check") != NULL) {
        prepend = "-c";
    } else if (strstr(name, "space") != NULL) {
        prepend = "-s";
    }

    char** args = ReadVarArgs(state, argc, argv);
    if (args == NULL) return NULL;

    // insert the "program name" argv[0] and a copy of the "prepend"
    // string (if any) at the start of the args.

    int extra = 1 + (prepend != NULL ? 1 : 0);
    char** temp = malloc((argc+extra) * sizeof(char*));
    memcpy(temp+extra, args, argc * sizeof(char*));
    temp[0] = strdup("updater");
    if (prepend) {
        temp[1] = strdup(prepend);
    }
    free(args);
    args = temp;
    argc += extra;

    printf("calling applypatch\n");
    fflush(stdout);
    int result = applypatch(argc, args);
    printf("applypatch returned %d\n", result);

    int i;
    for (i = 0; i < argc; ++i) {
        free(args[i]);
    }
    free(args);

    switch (result) {
        case 0:   return strdup("t");
        case 1:   return strdup("");
        default:  return ErrorAbort(state, "applypatch couldn't parse args");
    }
}

char* UIPrintFn(const char* name, State* state, int argc, Expr* argv[]) {
    char** args = ReadVarArgs(state, argc, argv);
    if (args == NULL) {
        return NULL;
    }

    int size = 0;
    int i;
    for (i = 0; i < argc; ++i) {
        size += strlen(args[i]);
    }
    char* buffer = malloc(size+1);
    size = 0;
    for (i = 0; i < argc; ++i) {
        strcpy(buffer+size, args[i]);
        size += strlen(args[i]);
        free(args[i]);
    }
    free(args);
    buffer[size] = '\0';

    char* line = strtok(buffer, "\n");
    while (line) {
        fprintf(((UpdaterInfo*)(state->cookie))->cmd_pipe,
                "ui_print %s\n", line);
        line = strtok(NULL, "\n");
    }
    fprintf(((UpdaterInfo*)(state->cookie))->cmd_pipe, "ui_print\n");

    return buffer;
}


void RegisterInstallFunctions() {
    RegisterFunction("mount", MountFn);
    RegisterFunction("is_mounted", IsMountedFn);
    RegisterFunction("unmount", UnmountFn);
    RegisterFunction("format", FormatFn);
    RegisterFunction("show_progress", ShowProgressFn);
    RegisterFunction("delete", DeleteFn);
    RegisterFunction("delete_recursive", DeleteFn);
    RegisterFunction("package_extract_dir", PackageExtractDirFn);
    RegisterFunction("package_extract_file", PackageExtractFileFn);
    RegisterFunction("symlink", SymlinkFn);
    RegisterFunction("set_perm", SetPermFn);
    RegisterFunction("set_perm_recursive", SetPermFn);

    RegisterFunction("getprop", GetPropFn);
    RegisterFunction("write_raw_image", WriteRawImageFn);
    RegisterFunction("write_firmware_image", WriteFirmwareImageFn);

    RegisterFunction("apply_patch", ApplyPatchFn);
    RegisterFunction("apply_patch_check", ApplyPatchFn);
    RegisterFunction("apply_patch_space", ApplyPatchFn);

    RegisterFunction("ui_print", UIPrintFn);
}
