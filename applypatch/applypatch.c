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

#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include "mincrypt/sha.h"
#include "applypatch.h"
#include "mtdutils/mtdutils.h"

int SaveFileContents(const char* filename, FileContents file);
int LoadMTDContents(const char* filename, FileContents* file);
int ParseSha1(const char* str, uint8_t* digest);
ssize_t FileSink(unsigned char* data, ssize_t len, void* token);

static int mtd_partitions_scanned = 0;

// Read a file into memory; store it and its associated metadata in
// *file.  Return 0 on success.
int LoadFileContents(const char* filename, FileContents* file) {
  file->data = NULL;

  // A special 'filename' beginning with "MTD:" means to load the
  // contents of an MTD partition.
  if (strncmp(filename, "MTD:", 4) == 0) {
    return LoadMTDContents(filename, file);
  }

  if (stat(filename, &file->st) != 0) {
    printf("failed to stat \"%s\": %s\n", filename, strerror(errno));
    return -1;
  }

  file->size = file->st.st_size;
  file->data = malloc(file->size);

  FILE* f = fopen(filename, "rb");
  if (f == NULL) {
    printf("failed to open \"%s\": %s\n", filename, strerror(errno));
    free(file->data);
    file->data = NULL;
    return -1;
  }

  size_t bytes_read = fread(file->data, 1, file->size, f);
  if (bytes_read != file->size) {
    printf("short read of \"%s\" (%d bytes of %d)\n",
            filename, bytes_read, file->size);
    free(file->data);
    file->data = NULL;
    return -1;
  }
  fclose(f);

  SHA(file->data, file->size, file->sha1);
  return 0;
}

static size_t* size_array;
// comparison function for qsort()ing an int array of indexes into
// size_array[].
static int compare_size_indices(const void* a, const void* b) {
  int aa = *(int*)a;
  int bb = *(int*)b;
  if (size_array[aa] < size_array[bb]) {
    return -1;
  } else if (size_array[aa] > size_array[bb]) {
    return 1;
  } else {
    return 0;
  }
}

void FreeFileContents(FileContents* file) {
    if (file) free(file->data);
    free(file);
}

// Load the contents of an MTD partition into the provided
// FileContents.  filename should be a string of the form
// "MTD:<partition_name>:<size_1>:<sha1_1>:<size_2>:<sha1_2>:...".
// The smallest size_n bytes for which that prefix of the mtd contents
// has the corresponding sha1 hash will be loaded.  It is acceptable
// for a size value to be repeated with different sha1s.  Will return
// 0 on success.
//
// This complexity is needed because if an OTA installation is
// interrupted, the partition might contain either the source or the
// target data, which might be of different lengths.  We need to know
// the length in order to read from MTD (there is no "end-of-file"
// marker), so the caller must specify the possible lengths and the
// hash of the data, and we'll do the load expecting to find one of
// those hashes.
int LoadMTDContents(const char* filename, FileContents* file) {
  char* copy = strdup(filename);
  const char* magic = strtok(copy, ":");
  if (strcmp(magic, "MTD") != 0) {
    printf("LoadMTDContents called with bad filename (%s)\n",
            filename);
    return -1;
  }
  const char* partition = strtok(NULL, ":");

  int i;
  int colons = 0;
  for (i = 0; filename[i] != '\0'; ++i) {
    if (filename[i] == ':') {
      ++colons;
    }
  }
  if (colons < 3 || colons%2 == 0) {
    printf("LoadMTDContents called with bad filename (%s)\n",
            filename);
  }

  int pairs = (colons-1)/2;     // # of (size,sha1) pairs in filename
  int* index = malloc(pairs * sizeof(int));
  size_t* size = malloc(pairs * sizeof(size_t));
  char** sha1sum = malloc(pairs * sizeof(char*));

  for (i = 0; i < pairs; ++i) {
    const char* size_str = strtok(NULL, ":");
    size[i] = strtol(size_str, NULL, 10);
    if (size[i] == 0) {
      printf("LoadMTDContents called with bad size (%s)\n", filename);
      return -1;
    }
    sha1sum[i] = strtok(NULL, ":");
    index[i] = i;
  }

  // sort the index[] array so it indexes the pairs in order of
  // increasing size.
  size_array = size;
  qsort(index, pairs, sizeof(int), compare_size_indices);

  if (!mtd_partitions_scanned) {
    mtd_scan_partitions();
    mtd_partitions_scanned = 1;
  }

  const MtdPartition* mtd = mtd_find_partition_by_name(partition);
  if (mtd == NULL) {
    printf("mtd partition \"%s\" not found (loading %s)\n",
            partition, filename);
    return -1;
  }

  MtdReadContext* ctx = mtd_read_partition(mtd);
  if (ctx == NULL) {
    printf("failed to initialize read of mtd partition \"%s\"\n",
            partition);
    return -1;
  }

  SHA_CTX sha_ctx;
  SHA_init(&sha_ctx);
  uint8_t parsed_sha[SHA_DIGEST_SIZE];

  // allocate enough memory to hold the largest size.
  file->data = malloc(size[index[pairs-1]]);
  char* p = (char*)file->data;
  file->size = 0;                // # bytes read so far

  for (i = 0; i < pairs; ++i) {
    // Read enough additional bytes to get us up to the next size
    // (again, we're trying the possibilities in order of increasing
    // size).
    size_t next = size[index[i]] - file->size;
    size_t read = 0;
    if (next > 0) {
      read = mtd_read_data(ctx, p, next);
      if (next != read) {
        printf("short read (%d bytes of %d) for partition \"%s\"\n",
                read, next, partition);
        free(file->data);
        file->data = NULL;
        return -1;
      }
      SHA_update(&sha_ctx, p, read);
      file->size += read;
    }

    // Duplicate the SHA context and finalize the duplicate so we can
    // check it against this pair's expected hash.
    SHA_CTX temp_ctx;
    memcpy(&temp_ctx, &sha_ctx, sizeof(SHA_CTX));
    const uint8_t* sha_so_far = SHA_final(&temp_ctx);

    if (ParseSha1(sha1sum[index[i]], parsed_sha) != 0) {
      printf("failed to parse sha1 %s in %s\n",
              sha1sum[index[i]], filename);
      free(file->data);
      file->data = NULL;
      return -1;
    }

    if (memcmp(sha_so_far, parsed_sha, SHA_DIGEST_SIZE) == 0) {
      // we have a match.  stop reading the partition; we'll return
      // the data we've read so far.
      printf("mtd read matched size %d sha %s\n",
             size[index[i]], sha1sum[index[i]]);
      break;
    }

    p += read;
  }

  mtd_read_close(ctx);

  if (i == pairs) {
    // Ran off the end of the list of (size,sha1) pairs without
    // finding a match.
    printf("contents of MTD partition \"%s\" didn't match %s\n",
            partition, filename);
    free(file->data);
    file->data = NULL;
    return -1;
  }

  const uint8_t* sha_final = SHA_final(&sha_ctx);
  for (i = 0; i < SHA_DIGEST_SIZE; ++i) {
    file->sha1[i] = sha_final[i];
  }

  // Fake some stat() info.
  file->st.st_mode = 0644;
  file->st.st_uid = 0;
  file->st.st_gid = 0;

  free(copy);
  free(index);
  free(size);
  free(sha1sum);

  return 0;
}


// Save the contents of the given FileContents object under the given
// filename.  Return 0 on success.
int SaveFileContents(const char* filename, FileContents file) {
  int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0) {
    printf("failed to open \"%s\" for write: %s\n",
            filename, strerror(errno));
    return -1;
  }

  size_t bytes_written = FileSink(file.data, file.size, &fd);
  if (bytes_written != file.size) {
    printf("short write of \"%s\" (%d bytes of %d) (%s)\n",
           filename, bytes_written, file.size, strerror(errno));
    close(fd);
    return -1;
  }
  fsync(fd);
  close(fd);

  if (chmod(filename, file.st.st_mode) != 0) {
    printf("chmod of \"%s\" failed: %s\n", filename, strerror(errno));
    return -1;
  }
  if (chown(filename, file.st.st_uid, file.st.st_gid) != 0) {
    printf("chown of \"%s\" failed: %s\n", filename, strerror(errno));
    return -1;
  }

  return 0;
}

// Write a memory buffer to target_mtd partition, a string of the form
// "MTD:<partition>[:...]".  Return 0 on success.
int WriteToMTDPartition(unsigned char* data, size_t len,
                        const char* target_mtd) {
  char* partition = strchr(target_mtd, ':');
  if (partition == NULL) {
    printf("bad MTD target name \"%s\"\n", target_mtd);
    return -1;
  }
  ++partition;
  // Trim off anything after a colon, eg "MTD:boot:blah:blah:blah...".
  // We want just the partition name "boot".
  partition = strdup(partition);
  char* end = strchr(partition, ':');
  if (end != NULL)
    *end = '\0';

  if (!mtd_partitions_scanned) {
    mtd_scan_partitions();
    mtd_partitions_scanned = 1;
  }

  const MtdPartition* mtd = mtd_find_partition_by_name(partition);
  if (mtd == NULL) {
    printf("mtd partition \"%s\" not found for writing\n", partition);
    return -1;
  }

  MtdWriteContext* ctx = mtd_write_partition(mtd);
  if (ctx == NULL) {
    printf("failed to init mtd partition \"%s\" for writing\n",
            partition);
    return -1;
  }

  size_t written = mtd_write_data(ctx, (char*)data, len);
  if (written != len) {
    printf("only wrote %d of %d bytes to MTD %s\n",
            written, len, partition);
    mtd_write_close(ctx);
    return -1;
  }

  if (mtd_erase_blocks(ctx, -1) < 0) {
    printf("error finishing mtd write of %s\n", partition);
    mtd_write_close(ctx);
    return -1;
  }

  if (mtd_write_close(ctx)) {
    printf("error closing mtd write of %s\n", partition);
    return -1;
  }

  free(partition);
  return 0;
}


// Take a string 'str' of 40 hex digits and parse it into the 20
// byte array 'digest'.  'str' may contain only the digest or be of
// the form "<digest>:<anything>".  Return 0 on success, -1 on any
// error.
int ParseSha1(const char* str, uint8_t* digest) {
  int i;
  const char* ps = str;
  uint8_t* pd = digest;
  for (i = 0; i < SHA_DIGEST_SIZE * 2; ++i, ++ps) {
    int digit;
    if (*ps >= '0' && *ps <= '9') {
      digit = *ps - '0';
    } else if (*ps >= 'a' && *ps <= 'f') {
      digit = *ps - 'a' + 10;
    } else if (*ps >= 'A' && *ps <= 'F') {
      digit = *ps - 'A' + 10;
    } else {
      return -1;
    }
    if (i % 2 == 0) {
      *pd = digit << 4;
    } else {
      *pd |= digit;
      ++pd;
    }
  }
  if (*ps != '\0' && *ps != ':') return -1;
  return 0;
}

// Parse arguments (which should be of the form "<sha1>" or
// "<sha1>:<filename>" into the array *patches, returning the number
// of Patch objects in *num_patches.  Return 0 on success.
int ParseShaArgs(int argc, char** argv, Patch** patches, int* num_patches) {
  *num_patches = argc;
  *patches = malloc(*num_patches * sizeof(Patch));

  int i;
  for (i = 0; i < *num_patches; ++i) {
    if (ParseSha1(argv[i], (*patches)[i].sha1) != 0) {
      printf("failed to parse sha1 \"%s\"\n", argv[i]);
      return -1;
    }
    if (argv[i][SHA_DIGEST_SIZE*2] == '\0') {
      (*patches)[i].patch_filename = NULL;
    } else if (argv[i][SHA_DIGEST_SIZE*2] == ':') {
      (*patches)[i].patch_filename = argv[i] + (SHA_DIGEST_SIZE*2+1);
    } else {
      printf("failed to parse filename \"%s\"\n", argv[i]);
      return -1;
    }
  }

  return 0;
}

// Search an array of Patch objects for one matching the given sha1.
// Return the Patch object on success, or NULL if no match is found.
const Patch* FindMatchingPatch(uint8_t* sha1, Patch* patches, int num_patches) {
  int i;
  for (i = 0; i < num_patches; ++i) {
    if (memcmp(patches[i].sha1, sha1, SHA_DIGEST_SIZE) == 0) {
      return patches+i;
    }
  }
  return NULL;
}

// Returns 0 if the contents of the file (argv[2]) or the cached file
// match any of the sha1's on the command line (argv[3:]).  Returns
// nonzero otherwise.
int CheckMode(int argc, char** argv) {
  if (argc < 3) {
    printf("no filename given\n");
    return 2;
  }

  int num_patches;
  Patch* patches;
  if (ParseShaArgs(argc-3, argv+3, &patches, &num_patches) != 0) { return 1; }

  FileContents file;
  file.data = NULL;

  // It's okay to specify no sha1s; the check will pass if the
  // LoadFileContents is successful.  (Useful for reading MTD
  // partitions, where the filename encodes the sha1s; no need to
  // check them twice.)
  if (LoadFileContents(argv[2], &file) != 0 ||
      (num_patches > 0 &&
       FindMatchingPatch(file.sha1, patches, num_patches) == NULL)) {
    printf("file \"%s\" doesn't have any of expected "
            "sha1 sums; checking cache\n", argv[2]);

    free(file.data);

    // If the source file is missing or corrupted, it might be because
    // we were killed in the middle of patching it.  A copy of it
    // should have been made in CACHE_TEMP_SOURCE.  If that file
    // exists and matches the sha1 we're looking for, the check still
    // passes.

    if (LoadFileContents(CACHE_TEMP_SOURCE, &file) != 0) {
      printf("failed to load cache file\n");
      return 1;
    }

    if (FindMatchingPatch(file.sha1, patches, num_patches) == NULL) {
      printf("cache bits don't match any sha1 for \"%s\"\n",
              argv[2]);
      return 1;
    }
  }

  free(file.data);
  return 0;
}

int ShowLicenses() {
  ShowBSDiffLicense();
  return 0;
}

ssize_t FileSink(unsigned char* data, ssize_t len, void* token) {
  int fd = *(int *)token;
  ssize_t done = 0;
  ssize_t wrote;
  while (done < (ssize_t) len) {
    wrote = write(fd, data+done, len-done);
    if (wrote <= 0) {
      printf("error writing %d bytes: %s\n", (int)(len-done), strerror(errno));
      return done;
    }
    done += wrote;
  }
  printf("wrote %d bytes to output\n", (int)done);
  return done;
}

typedef struct {
  unsigned char* buffer;
  ssize_t size;
  ssize_t pos;
} MemorySinkInfo;

ssize_t MemorySink(unsigned char* data, ssize_t len, void* token) {
  MemorySinkInfo* msi = (MemorySinkInfo*)token;
  if (msi->size - msi->pos < len) {
    return -1;
  }
  memcpy(msi->buffer + msi->pos, data, len);
  msi->pos += len;
  return len;
}

// Return the amount of free space (in bytes) on the filesystem
// containing filename.  filename must exist.  Return -1 on error.
size_t FreeSpaceForFile(const char* filename) {
  struct statfs sf;
  if (statfs(filename, &sf) != 0) {
    printf("failed to statfs %s: %s\n", filename, strerror(errno));
    return -1;
  }
  return sf.f_bsize * sf.f_bfree;
}

// This program applies binary patches to files in a way that is safe
// (the original file is not touched until we have the desired
// replacement for it) and idempotent (it's okay to run this program
// multiple times).
//
// - if the sha1 hash of <tgt-file> is <tgt-sha1>, does nothing and exits
//   successfully.
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
// <src-file> (or <file> in check mode) may refer to an MTD partition
// to read the source data.  See the comments for the
// LoadMTDContents() function above for the format of such a filename.
//
//
// As you might guess from the arguments, this function used to be
// main(); it was split out this way so applypatch could be built as a
// static library and linked into other executables as well.  In the
// future only the library form will exist; we will not need to build
// this as a standalone executable.
//
// The arguments to this function are just the command-line of the
// standalone executable:
//
// <src-file> <tgt-file> <tgt-sha1> <tgt-size> [<src-sha1>:<patch> ...]
//    to apply a patch.  Returns 0 on success, 1 on failure.
//
// "-c" <file> [<sha1> ...]
//    to check a file's contents against zero or more sha1s.  Returns
//    0 if it matches any of them, 1 if it doesn't.
//
// "-s" <bytes>
//    returns 0 if enough free space is available on /cache; 1 if it
//    does not.
//
// "-l"
//    shows open-source license information and returns 0.
//
// This function returns 2 if the arguments are not understood (in the
// standalone executable, this causes the usage message to be
// printed).
//
// TODO: make the interface more sensible for use as a library.

int applypatch(int argc, char** argv) {
  if (argc < 2) {
    return 2;
  }

  if (strncmp(argv[1], "-l", 3) == 0) {
    return ShowLicenses();
  }

  if (strncmp(argv[1], "-c", 3) == 0) {
    return CheckMode(argc, argv);
  }

  if (strncmp(argv[1], "-s", 3) == 0) {
    if (argc != 3) {
      return 2;
    }
    size_t bytes = strtol(argv[2], NULL, 10);
    if (MakeFreeSpaceOnCache(bytes) < 0) {
      printf("unable to make %ld bytes available on /cache\n", (long)bytes);
      return 1;
    } else {
      return 0;
    }
  }

  uint8_t target_sha1[SHA_DIGEST_SIZE];

  const char* source_filename = argv[1];
  const char* target_filename = argv[2];
  if (target_filename[0] == '-' &&
      target_filename[1] == '\0') {
    target_filename = source_filename;
  }

  printf("\napplying patch to %s\n", source_filename);

  if (ParseSha1(argv[3], target_sha1) != 0) {
    printf("failed to parse tgt-sha1 \"%s\"\n", argv[3]);
    return 1;
  }

  unsigned long target_size = strtoul(argv[4], NULL, 0);

  int num_patches;
  Patch* patches;
  if (ParseShaArgs(argc-5, argv+5, &patches, &num_patches) < 0) { return 1; }

  FileContents copy_file;
  FileContents source_file;
  const char* source_patch_filename = NULL;
  const char* copy_patch_filename = NULL;
  int made_copy = 0;

  // We try to load the target file into the source_file object.
  if (LoadFileContents(target_filename, &source_file) == 0) {
    if (memcmp(source_file.sha1, target_sha1, SHA_DIGEST_SIZE) == 0) {
      // The early-exit case:  the patch was already applied, this file
      // has the desired hash, nothing for us to do.
      printf("\"%s\" is already target; no patch needed\n",
              target_filename);
      return 0;
    }
  }

  if (source_file.data == NULL ||
      (target_filename != source_filename &&
       strcmp(target_filename, source_filename) != 0)) {
    // Need to load the source file:  either we failed to load the
    // target file, or we did but it's different from the source file.
    free(source_file.data);
    LoadFileContents(source_filename, &source_file);
  }

  if (source_file.data != NULL) {
    const Patch* to_use =
        FindMatchingPatch(source_file.sha1, patches, num_patches);
    if (to_use != NULL) {
      source_patch_filename = to_use->patch_filename;
    }
  }

  if (source_patch_filename == NULL) {
    free(source_file.data);
    printf("source file is bad; trying copy\n");

    if (LoadFileContents(CACHE_TEMP_SOURCE, &copy_file) < 0) {
      // fail.
      printf("failed to read copy file\n");
      return 1;
    }

    const Patch* to_use =
        FindMatchingPatch(copy_file.sha1, patches, num_patches);
    if (to_use != NULL) {
      copy_patch_filename = to_use->patch_filename;
    }

    if (copy_patch_filename == NULL) {
      // fail.
      printf("copy file doesn't match source SHA-1s either\n");
      return 1;
    }
  }

  int retry = 1;
  SHA_CTX ctx;
  int output;
  MemorySinkInfo msi;
  FileContents* source_to_use;
  char* outname;

  // assume that target_filename (eg "/system/app/Foo.apk") is located
  // on the same filesystem as its top-level directory ("/system").
  // We need something that exists for calling statfs().
  char target_fs[strlen(target_filename)+1];
  char* slash = strchr(target_filename+1, '/');
  if (slash != NULL) {
    int count = slash - target_filename;
    strncpy(target_fs, target_filename, count);
    target_fs[count] = '\0';
  } else {
    strcpy(target_fs, target_filename);
  }

  do {
    // Is there enough room in the target filesystem to hold the patched
    // file?

    if (strncmp(target_filename, "MTD:", 4) == 0) {
      // If the target is an MTD partition, we're actually going to
      // write the output to /tmp and then copy it to the partition.
      // statfs() always returns 0 blocks free for /tmp, so instead
      // we'll just assume that /tmp has enough space to hold the file.

      // We still write the original source to cache, in case the MTD
      // write is interrupted.
      if (MakeFreeSpaceOnCache(source_file.size) < 0) {
        printf("not enough free space on /cache\n");
        return 1;
      }
      if (SaveFileContents(CACHE_TEMP_SOURCE, source_file) < 0) {
        printf("failed to back up source file\n");
        return 1;
      }
      made_copy = 1;
      retry = 0;
    } else {
      int enough_space = 0;
      if (retry > 0) {
        size_t free_space = FreeSpaceForFile(target_fs);
        int enough_space =
          (free_space > (target_size * 3 / 2));  // 50% margin of error
        printf("target %ld bytes; free space %ld bytes; retry %d; enough %d\n",
               (long)target_size, (long)free_space, retry, enough_space);
      }

      if (!enough_space) {
        retry = 0;
      }

      if (!enough_space && source_patch_filename != NULL) {
        // Using the original source, but not enough free space.  First
        // copy the source file to cache, then delete it from the original
        // location.

        if (strncmp(source_filename, "MTD:", 4) == 0) {
          // It's impossible to free space on the target filesystem by
          // deleting the source if the source is an MTD partition.  If
          // we're ever in a state where we need to do this, fail.
          printf("not enough free space for target but source is MTD\n");
          return 1;
        }

        if (MakeFreeSpaceOnCache(source_file.size) < 0) {
          printf("not enough free space on /cache\n");
          return 1;
        }

        if (SaveFileContents(CACHE_TEMP_SOURCE, source_file) < 0) {
          printf("failed to back up source file\n");
          return 1;
        }
        made_copy = 1;
        unlink(source_filename);

        size_t free_space = FreeSpaceForFile(target_fs);
        printf("(now %ld bytes free for target)\n", (long)free_space);
      }
    }

    const char* patch_filename;
    if (source_patch_filename != NULL) {
      source_to_use = &source_file;
      patch_filename = source_patch_filename;
    } else {
      source_to_use = &copy_file;
      patch_filename = copy_patch_filename;
    }

    SinkFn sink = NULL;
    void* token = NULL;
    output = -1;
    outname = NULL;
    if (strncmp(target_filename, "MTD:", 4) == 0) {
      // We store the decoded output in memory.
      msi.buffer = malloc(target_size);
      if (msi.buffer == NULL) {
        printf("failed to alloc %ld bytes for output\n",
               (long)target_size);
        return 1;
      }
      msi.pos = 0;
      msi.size = target_size;
      sink = MemorySink;
      token = &msi;
    } else {
      // We write the decoded output to "<tgt-file>.patch".
      outname = (char*)malloc(strlen(target_filename) + 10);
      strcpy(outname, target_filename);
      strcat(outname, ".patch");

      output = open(outname, O_WRONLY | O_CREAT | O_TRUNC);
      if (output < 0) {
        printf("failed to open output file %s: %s\n",
               outname, strerror(errno));
        return 1;
      }
      sink = FileSink;
      token = &output;
    }

#define MAX_HEADER_LENGTH 8
    unsigned char header[MAX_HEADER_LENGTH];
    FILE* patchf = fopen(patch_filename, "rb");
    if (patchf == NULL) {
      printf("failed to open patch file %s: %s\n",
             patch_filename, strerror(errno));
      return 1;
    }
    int header_bytes_read = fread(header, 1, MAX_HEADER_LENGTH, patchf);
    fclose(patchf);

    SHA_init(&ctx);

    int result;

    if (header_bytes_read >= 4 &&
        header[0] == 0xd6 && header[1] == 0xc3 &&
        header[2] == 0xc4 && header[3] == 0) {
      // xdelta3 patches begin "VCD" (with the high bits set) followed
      // by a zero byte (the version number).
      printf("error:  xdelta3 patches no longer supported\n");
      return 1;
    } else if (header_bytes_read >= 8 &&
               memcmp(header, "BSDIFF40", 8) == 0) {
      result = ApplyBSDiffPatch(source_to_use->data, source_to_use->size,
                                    patch_filename, 0, sink, token, &ctx);
    } else if (header_bytes_read >= 8 &&
               memcmp(header, "IMGDIFF", 7) == 0 &&
               (header[7] == '1' || header[7] == '2')) {
      result = ApplyImagePatch(source_to_use->data, source_to_use->size,
                                   patch_filename, sink, token, &ctx);
    } else {
      printf("Unknown patch file format\n");
      return 1;
    }

    if (output >= 0) {
      fsync(output);
      close(output);
    }

    if (result != 0) {
      if (retry == 0) {
        printf("applying patch failed\n");
        return result != 0;
      } else {
        printf("applying patch failed; retrying\n");
      }
      if (outname != NULL) {
        unlink(outname);
      }
    } else {
      // succeeded; no need to retry
      break;
    }
  } while (retry-- > 0);

  const uint8_t* current_target_sha1 = SHA_final(&ctx);
  if (memcmp(current_target_sha1, target_sha1, SHA_DIGEST_SIZE) != 0) {
    printf("patch did not produce expected sha1\n");
    return 1;
  }

  if (output < 0) {
    // Copy the temp file to the MTD partition.
    if (WriteToMTDPartition(msi.buffer, msi.pos, target_filename) != 0) {
      printf("write of patched data to %s failed\n", target_filename);
      return 1;
    }
    free(msi.buffer);
  } else {
    // Give the .patch file the same owner, group, and mode of the
    // original source file.
    if (chmod(outname, source_to_use->st.st_mode) != 0) {
      printf("chmod of \"%s\" failed: %s\n", outname, strerror(errno));
      return 1;
    }
    if (chown(outname, source_to_use->st.st_uid,
              source_to_use->st.st_gid) != 0) {
      printf("chown of \"%s\" failed: %s\n", outname, strerror(errno));
      return 1;
    }

    // Finally, rename the .patch file to replace the target file.
    if (rename(outname, target_filename) != 0) {
      printf("rename of .patch to \"%s\" failed: %s\n",
              target_filename, strerror(errno));
      return 1;
    }
  }

  // If this run of applypatch created the copy, and we're here, we
  // can delete it.
  if (made_copy) unlink(CACHE_TEMP_SOURCE);

  // Success!
  return 0;
}
