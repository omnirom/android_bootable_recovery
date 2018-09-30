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

#include "applypatch_modes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <openssl/sha.h>

#include "applypatch/applypatch.h"
#include "edify/expr.h"

static int CheckMode(int argc, const char** argv) {
    if (argc < 3) {
        return 2;
    }
    std::vector<std::string> sha1;
    for (int i = 3; i < argc; i++) {
        sha1.push_back(argv[i]);
    }

    return applypatch_check(argv[2], sha1);
}

// Parse arguments (which should be of the form "<sha1>:<filename>" into the
// new parallel arrays *sha1s and *files. Returns true on success.
static bool ParsePatchArgs(int argc, const char** argv, std::vector<std::string>* sha1s,
                           std::vector<FileContents>* files) {
    if (sha1s == nullptr) {
        return false;
    }
    for (int i = 0; i < argc; ++i) {
        std::vector<std::string> pieces = android::base::Split(argv[i], ":");
        if (pieces.size() != 2) {
            printf("failed to parse patch argument \"%s\"\n", argv[i]);
            return false;
        }

        uint8_t digest[SHA_DIGEST_LENGTH];
        if (ParseSha1(pieces[0].c_str(), digest) != 0) {
            printf("failed to parse sha1 \"%s\"\n", argv[i]);
            return false;
        }

        sha1s->push_back(pieces[0]);
        FileContents fc;
        if (LoadFileContents(pieces[1].c_str(), &fc) != 0) {
            return false;
        }
        files->push_back(std::move(fc));
    }
    return true;
}

static int FlashMode(const char* src_filename, const char* tgt_filename,
                     const char* tgt_sha1, size_t tgt_size) {
    return applypatch_flash(src_filename, tgt_filename, tgt_sha1, tgt_size);
}

static int PatchMode(int argc, const char** argv) {
    FileContents bonusFc;
    Value bonus(VAL_INVALID, "");

    if (argc >= 3 && strcmp(argv[1], "-b") == 0) {
        if (LoadFileContents(argv[2], &bonusFc) != 0) {
            printf("failed to load bonus file %s\n", argv[2]);
            return 1;
        }
        bonus.type = VAL_BLOB;
        bonus.data = std::string(bonusFc.data.cbegin(), bonusFc.data.cend());
        argc -= 2;
        argv += 2;
    }

    if (argc < 4) {
        return 2;
    }

    size_t target_size;
    if (!android::base::ParseUint(argv[4], &target_size) || target_size == 0) {
        printf("can't parse \"%s\" as byte count\n\n", argv[4]);
        return 1;
    }

    // If no <src-sha1>:<patch> is provided, it is in flash mode.
    if (argc == 5) {
        if (bonus.type != VAL_INVALID) {
            printf("bonus file not supported in flash mode\n");
            return 1;
        }
        return FlashMode(argv[1], argv[2], argv[3], target_size);
    }

    std::vector<std::string> sha1s;
    std::vector<FileContents> files;
    if (!ParsePatchArgs(argc-5, argv+5, &sha1s, &files)) {
        printf("failed to parse patch args\n");
        return 1;
    }

    std::vector<std::unique_ptr<Value>> patches;
    for (size_t i = 0; i < files.size(); ++i) {
        patches.push_back(std::make_unique<Value>(
                VAL_BLOB, std::string(files[i].data.cbegin(), files[i].data.cend())));
    }
    return applypatch(argv[1], argv[2], argv[3], target_size, sha1s, patches, &bonus);
}

// This program (applypatch) applies binary patches to files in a way that
// is safe (the original file is not touched until we have the desired
// replacement for it) and idempotent (it's okay to run this program
// multiple times).
//
// - if the sha1 hash of <tgt-file> is <tgt-sha1>, does nothing and exits
//   successfully.
//
// - otherwise, if no <src-sha1>:<patch> is provided, flashes <tgt-file> with
//   <src-file>. <tgt-file> must be a partition name, while <src-file> must
//   be a regular image file. <src-file> will not be deleted on success.
//
// - otherwise, if the sha1 hash of <src-file> is <src-sha1>, applies the
//   bsdiff <patch> to <src-file> to produce a new file (the type of patch
//   is automatically detected from the file header).  If that new
//   file has sha1 hash <tgt-sha1>, moves it to replace <tgt-file>, and
//   exits successfully.  Note that if <src-file> and <tgt-file> are
//   not the same, <src-file> is NOT deleted on success.  <tgt-file>
//   may be the string "-" to mean "the same as src-file".
//
// - otherwise, or if any error is encountered, exits with non-zero
//   status.
//
// <src-file> (or <file> in check mode) may refer to an EMMC partition
// to read the source data.  See the comments for the
// LoadPartitionContents() function for the format of such a filename.

int applypatch_modes(int argc, const char** argv) {
    if (argc < 2) {
      usage:
        printf(
            "usage: %s [-b <bonus-file>] <src-file> <tgt-file> <tgt-sha1> <tgt-size> "
            "[<src-sha1>:<patch> ...]\n"
            "   or  %s -c <file> [<sha1> ...]\n"
            "   or  %s -l\n"
            "\n"
            "Filenames may be of the form\n"
            "  EMMC:<partition>:<len_1>:<sha1_1>:<len_2>:<sha1_2>:...\n"
            "to specify reading from or writing to an EMMC partition.\n\n",
            argv[0], argv[0], argv[0]);
        return 2;
    }

    int result;

    if (strncmp(argv[1], "-l", 3) == 0) {
        result = ShowLicenses();
    } else if (strncmp(argv[1], "-c", 3) == 0) {
        result = CheckMode(argc, argv);
    } else {
        result = PatchMode(argc, argv);
    }

    if (result == 2) {
        goto usage;
    }
    return result;
}
