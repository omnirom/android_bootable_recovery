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

/*
 * This program constructs binary patches for images -- such as boot.img
 * and recovery.img -- that consist primarily of large chunks of gzipped
 * data interspersed with uncompressed data.  Doing a naive bsdiff of
 * these files is not useful because small changes in the data lead to
 * large changes in the compressed bitstream; bsdiff patches of gzipped
 * data are typically as large as the data itself.
 *
 * To patch these usefully, we break the source and target images up into
 * chunks of two types: "normal" and "gzip".  Normal chunks are simply
 * patched using a plain bsdiff.  Gzip chunks are first expanded, then a
 * bsdiff is applied to the uncompressed data, then the patched data is
 * gzipped using the same encoder parameters.  Patched chunks are
 * concatenated together to create the output file; the output image
 * should be *exactly* the same series of bytes as the target image used
 * originally to generate the patch.
 *
 * To work well with this tool, the gzipped sections of the target
 * image must have been generated using the same deflate encoder that
 * is available in applypatch, namely, the one in the zlib library.
 * In practice this means that images should be compressed using the
 * "minigzip" tool included in the zlib distribution, not the GNU gzip
 * program.
 *
 * An "imgdiff" patch consists of a header describing the chunk structure
 * of the file and any encoding parameters needed for the gzipped
 * chunks, followed by N bsdiff patches, one per chunk.
 *
 * For a diff to be generated, the source and target images must have the
 * same "chunk" structure: that is, the same number of gzipped and normal
 * chunks in the same order.  Android boot and recovery images currently
 * consist of five chunks:  a small normal header, a gzipped kernel, a
 * small normal section, a gzipped ramdisk, and finally a small normal
 * footer.
 *
 * Caveats:  we locate gzipped sections within the source and target
 * images by searching for the byte sequence 1f8b0800:  1f8b is the gzip
 * magic number; 08 specifies the "deflate" encoding [the only encoding
 * supported by the gzip standard]; and 00 is the flags byte.  We do not
 * currently support any extra header fields (which would be indicated by
 * a nonzero flags byte).  We also don't handle the case when that byte
 * sequence appears spuriously in the file.  (Note that it would have to
 * occur spuriously within a normal chunk to be a problem.)
 *
 *
 * The imgdiff patch header looks like this:
 *
 *    "IMGDIFF1"                  (8)   [magic number and version]
 *    chunk count                 (4)
 *    for each chunk:
 *        chunk type              (4)   [CHUNK_{NORMAL, GZIP, DEFLATE, RAW}]
 *        if chunk type == CHUNK_NORMAL:
 *           source start         (8)
 *           source len           (8)
 *           bsdiff patch offset  (8)   [from start of patch file]
 *        if chunk type == CHUNK_GZIP:      (version 1 only)
 *           source start         (8)
 *           source len           (8)
 *           bsdiff patch offset  (8)   [from start of patch file]
 *           source expanded len  (8)   [size of uncompressed source]
 *           target expected len  (8)   [size of uncompressed target]
 *           gzip level           (4)
 *                method          (4)
 *                windowBits      (4)
 *                memLevel        (4)
 *                strategy        (4)
 *           gzip header len      (4)
 *           gzip header          (gzip header len)
 *           gzip footer          (8)
 *        if chunk type == CHUNK_DEFLATE:   (version 2 only)
 *           source start         (8)
 *           source len           (8)
 *           bsdiff patch offset  (8)   [from start of patch file]
 *           source expanded len  (8)   [size of uncompressed source]
 *           target expected len  (8)   [size of uncompressed target]
 *           gzip level           (4)
 *                method          (4)
 *                windowBits      (4)
 *                memLevel        (4)
 *                strategy        (4)
 *        if chunk type == RAW:             (version 2 only)
 *           target len           (4)
 *           data                 (target len)
 *
 * All integers are little-endian.  "source start" and "source len"
 * specify the section of the input image that comprises this chunk,
 * including the gzip header and footer for gzip chunks.  "source
 * expanded len" is the size of the uncompressed source data.  "target
 * expected len" is the size of the uncompressed data after applying
 * the bsdiff patch.  The next five parameters specify the zlib
 * parameters to be used when compressing the patched data, and the
 * next three specify the header and footer to be wrapped around the
 * compressed data to create the output chunk (so that header contents
 * like the timestamp are recreated exactly).
 *
 * After the header there are 'chunk count' bsdiff patches; the offset
 * of each from the beginning of the file is specified in the header.
 *
 * This tool can take an optional file of "bonus data".  This is an
 * extra file of data that is appended to chunk #1 after it is
 * compressed (it must be a CHUNK_DEFLATE chunk).  The same file must
 * be available (and passed to applypatch with -b) when applying the
 * patch.  This is used to reduce the size of recovery-from-boot
 * patches by combining the boot image with recovery ramdisk
 * information that is stored on the system partition.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>

#include "zlib.h"
#include "imgdiff.h"
#include "utils.h"

typedef struct {
  int type;             // CHUNK_NORMAL, CHUNK_DEFLATE
  size_t start;         // offset of chunk in original image file

  size_t len;
  unsigned char* data;  // data to be patched (uncompressed, for deflate chunks)

  size_t source_start;
  size_t source_len;

  off_t* I;             // used by bsdiff

  // --- for CHUNK_DEFLATE chunks only: ---

  // original (compressed) deflate data
  size_t deflate_len;
  unsigned char* deflate_data;

  char* filename;       // used for zip entries

  // deflate encoder parameters
  int level, method, windowBits, memLevel, strategy;

  size_t source_uncompressed_len;
} ImageChunk;

typedef struct {
  int data_offset;
  int deflate_len;
  int uncomp_len;
  char* filename;
} ZipFileEntry;

static int fileentry_compare(const void* a, const void* b) {
  int ao = ((ZipFileEntry*)a)->data_offset;
  int bo = ((ZipFileEntry*)b)->data_offset;
  if (ao < bo) {
    return -1;
  } else if (ao > bo) {
    return 1;
  } else {
    return 0;
  }
}

// from bsdiff.c
int bsdiff(u_char* old, off_t oldsize, off_t** IP, u_char* newdata, off_t newsize,
           const char* patch_filename);

unsigned char* ReadZip(const char* filename,
                       int* num_chunks, ImageChunk** chunks,
                       int include_pseudo_chunk) {
  struct stat st;
  if (stat(filename, &st) != 0) {
    printf("failed to stat \"%s\": %s\n", filename, strerror(errno));
    return NULL;
  }

  size_t sz = static_cast<size_t>(st.st_size);
  unsigned char* img = reinterpret_cast<unsigned char*>(malloc(sz));
  FILE* f = fopen(filename, "rb");
  if (fread(img, 1, sz, f) != sz) {
    printf("failed to read \"%s\" %s\n", filename, strerror(errno));
    fclose(f);
    return NULL;
  }
  fclose(f);

  // look for the end-of-central-directory record.

  int i;
  for (i = st.st_size-20; i >= 0 && i > st.st_size - 65600; --i) {
    if (img[i] == 0x50 && img[i+1] == 0x4b &&
        img[i+2] == 0x05 && img[i+3] == 0x06) {
      break;
    }
  }
  // double-check: this archive consists of a single "disk"
  if (!(img[i+4] == 0 && img[i+5] == 0 && img[i+6] == 0 && img[i+7] == 0)) {
    printf("can't process multi-disk archive\n");
    return NULL;
  }

  int cdcount = Read2(img+i+8);
  int cdoffset = Read4(img+i+16);

  ZipFileEntry* temp_entries = reinterpret_cast<ZipFileEntry*>(malloc(
      cdcount * sizeof(ZipFileEntry)));
  int entrycount = 0;

  unsigned char* cd = img+cdoffset;
  for (i = 0; i < cdcount; ++i) {
    if (!(cd[0] == 0x50 && cd[1] == 0x4b && cd[2] == 0x01 && cd[3] == 0x02)) {
      printf("bad central directory entry %d\n", i);
      return NULL;
    }

    int clen = Read4(cd+20);   // compressed len
    int ulen = Read4(cd+24);   // uncompressed len
    int nlen = Read2(cd+28);   // filename len
    int xlen = Read2(cd+30);   // extra field len
    int mlen = Read2(cd+32);   // file comment len
    int hoffset = Read4(cd+42);   // local header offset

    char* filename = reinterpret_cast<char*>(malloc(nlen+1));
    memcpy(filename, cd+46, nlen);
    filename[nlen] = '\0';

    int method = Read2(cd+10);

    cd += 46 + nlen + xlen + mlen;

    if (method != 8) {  // 8 == deflate
      free(filename);
      continue;
    }

    unsigned char* lh = img + hoffset;

    if (!(lh[0] == 0x50 && lh[1] == 0x4b && lh[2] == 0x03 && lh[3] == 0x04)) {
      printf("bad local file header entry %d\n", i);
      return NULL;
    }

    if (Read2(lh+26) != nlen || memcmp(lh+30, filename, nlen) != 0) {
      printf("central dir filename doesn't match local header\n");
      return NULL;
    }

    xlen = Read2(lh+28);   // extra field len; might be different from CD entry?

    temp_entries[entrycount].data_offset = hoffset+30+nlen+xlen;
    temp_entries[entrycount].deflate_len = clen;
    temp_entries[entrycount].uncomp_len = ulen;
    temp_entries[entrycount].filename = filename;
    ++entrycount;
  }

  qsort(temp_entries, entrycount, sizeof(ZipFileEntry), fileentry_compare);

#if 0
  printf("found %d deflated entries\n", entrycount);
  for (i = 0; i < entrycount; ++i) {
    printf("off %10d  len %10d unlen %10d   %p %s\n",
           temp_entries[i].data_offset,
           temp_entries[i].deflate_len,
           temp_entries[i].uncomp_len,
           temp_entries[i].filename,
           temp_entries[i].filename);
  }
#endif

  *num_chunks = 0;
  *chunks = reinterpret_cast<ImageChunk*>(malloc((entrycount*2+2) * sizeof(ImageChunk)));
  ImageChunk* curr = *chunks;

  if (include_pseudo_chunk) {
    curr->type = CHUNK_NORMAL;
    curr->start = 0;
    curr->len = st.st_size;
    curr->data = img;
    curr->filename = NULL;
    curr->I = NULL;
    ++curr;
    ++*num_chunks;
  }

  int pos = 0;
  int nextentry = 0;

  while (pos < st.st_size) {
    if (nextentry < entrycount && pos == temp_entries[nextentry].data_offset) {
      curr->type = CHUNK_DEFLATE;
      curr->start = pos;
      curr->deflate_len = temp_entries[nextentry].deflate_len;
      curr->deflate_data = img + pos;
      curr->filename = temp_entries[nextentry].filename;
      curr->I = NULL;

      curr->len = temp_entries[nextentry].uncomp_len;
      curr->data = reinterpret_cast<unsigned char*>(malloc(curr->len));

      z_stream strm;
      strm.zalloc = Z_NULL;
      strm.zfree = Z_NULL;
      strm.opaque = Z_NULL;
      strm.avail_in = curr->deflate_len;
      strm.next_in = curr->deflate_data;

      // -15 means we are decoding a 'raw' deflate stream; zlib will
      // not expect zlib headers.
      int ret = inflateInit2(&strm, -15);

      strm.avail_out = curr->len;
      strm.next_out = curr->data;
      ret = inflate(&strm, Z_NO_FLUSH);
      if (ret != Z_STREAM_END) {
        printf("failed to inflate \"%s\"; %d\n", curr->filename, ret);
        return NULL;
      }

      inflateEnd(&strm);

      pos += curr->deflate_len;
      ++nextentry;
      ++*num_chunks;
      ++curr;
      continue;
    }

    // use a normal chunk to take all the data up to the start of the
    // next deflate section.

    curr->type = CHUNK_NORMAL;
    curr->start = pos;
    if (nextentry < entrycount) {
      curr->len = temp_entries[nextentry].data_offset - pos;
    } else {
      curr->len = st.st_size - pos;
    }
    curr->data = img + pos;
    curr->filename = NULL;
    curr->I = NULL;
    pos += curr->len;

    ++*num_chunks;
    ++curr;
  }

  free(temp_entries);
  return img;
}

/*
 * Read the given file and break it up into chunks, putting the number
 * of chunks and their info in *num_chunks and **chunks,
 * respectively.  Returns a malloc'd block of memory containing the
 * contents of the file; various pointers in the output chunk array
 * will point into this block of memory.  The caller should free the
 * return value when done with all the chunks.  Returns NULL on
 * failure.
 */
unsigned char* ReadImage(const char* filename,
                         int* num_chunks, ImageChunk** chunks) {
  struct stat st;
  if (stat(filename, &st) != 0) {
    printf("failed to stat \"%s\": %s\n", filename, strerror(errno));
    return NULL;
  }

  size_t sz = static_cast<size_t>(st.st_size);
  unsigned char* img = reinterpret_cast<unsigned char*>(malloc(sz + 4));
  FILE* f = fopen(filename, "rb");
  if (fread(img, 1, sz, f) != sz) {
    printf("failed to read \"%s\" %s\n", filename, strerror(errno));
    fclose(f);
    return NULL;
  }
  fclose(f);

  // append 4 zero bytes to the data so we can always search for the
  // four-byte string 1f8b0800 starting at any point in the actual
  // file data, without special-casing the end of the data.
  memset(img+sz, 0, 4);

  size_t pos = 0;

  *num_chunks = 0;
  *chunks = NULL;

  while (pos < sz) {
    unsigned char* p = img+pos;

    bool processed_deflate = false;
    if (sz - pos >= 4 &&
        p[0] == 0x1f && p[1] == 0x8b &&
        p[2] == 0x08 &&    // deflate compression
        p[3] == 0x00) {    // no header flags
      // 'pos' is the offset of the start of a gzip chunk.
      size_t chunk_offset = pos;

      *num_chunks += 3;
      *chunks = reinterpret_cast<ImageChunk*>(realloc(*chunks,
          *num_chunks * sizeof(ImageChunk)));
      ImageChunk* curr = *chunks + (*num_chunks-3);

      // create a normal chunk for the header.
      curr->start = pos;
      curr->type = CHUNK_NORMAL;
      curr->len = GZIP_HEADER_LEN;
      curr->data = p;
      curr->I = NULL;

      pos += curr->len;
      p += curr->len;
      ++curr;

      curr->type = CHUNK_DEFLATE;
      curr->filename = NULL;
      curr->I = NULL;

      // We must decompress this chunk in order to discover where it
      // ends, and so we can put the uncompressed data and its length
      // into curr->data and curr->len.

      size_t allocated = 32768;
      curr->len = 0;
      curr->data = reinterpret_cast<unsigned char*>(malloc(allocated));
      curr->start = pos;
      curr->deflate_data = p;

      z_stream strm;
      strm.zalloc = Z_NULL;
      strm.zfree = Z_NULL;
      strm.opaque = Z_NULL;
      strm.avail_in = sz - pos;
      strm.next_in = p;

      // -15 means we are decoding a 'raw' deflate stream; zlib will
      // not expect zlib headers.
      int ret = inflateInit2(&strm, -15);

      do {
        strm.avail_out = allocated - curr->len;
        strm.next_out = curr->data + curr->len;
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret < 0) {
          if (!processed_deflate) {
            // This is the first chunk, assume that it's just a spurious
            // gzip header instead of a real one.
            break;
          }
          printf("Error: inflate failed [%s] at file offset [%zu]\n"
                 "imgdiff only supports gzip kernel compression,"
                 " did you try CONFIG_KERNEL_LZO?\n",
                 strm.msg, chunk_offset);
          free(img);
          return NULL;
        }
        curr->len = allocated - strm.avail_out;
        if (strm.avail_out == 0) {
          allocated *= 2;
          curr->data = reinterpret_cast<unsigned char*>(realloc(curr->data, allocated));
        }
        processed_deflate = true;
      } while (ret != Z_STREAM_END);

      curr->deflate_len = sz - strm.avail_in - pos;
      inflateEnd(&strm);
      pos += curr->deflate_len;
      p += curr->deflate_len;
      ++curr;

      // create a normal chunk for the footer

      curr->type = CHUNK_NORMAL;
      curr->start = pos;
      curr->len = GZIP_FOOTER_LEN;
      curr->data = img+pos;
      curr->I = NULL;

      pos += curr->len;
      p += curr->len;
      ++curr;

      // The footer (that we just skipped over) contains the size of
      // the uncompressed data.  Double-check to make sure that it
      // matches the size of the data we got when we actually did
      // the decompression.
      size_t footer_size = Read4(p-4);
      if (footer_size != curr[-2].len) {
        printf("Error: footer size %zu != decompressed size %zu\n",
            footer_size, curr[-2].len);
        free(img);
        return NULL;
      }
    } else {
      // Reallocate the list for every chunk; we expect the number of
      // chunks to be small (5 for typical boot and recovery images).
      ++*num_chunks;
      *chunks = reinterpret_cast<ImageChunk*>(realloc(*chunks, *num_chunks * sizeof(ImageChunk)));
      ImageChunk* curr = *chunks + (*num_chunks-1);
      curr->start = pos;
      curr->I = NULL;

      // 'pos' is not the offset of the start of a gzip chunk, so scan
      // forward until we find a gzip header.
      curr->type = CHUNK_NORMAL;
      curr->data = p;

      for (curr->len = 0; curr->len < (sz - pos); ++curr->len) {
        if (p[curr->len] == 0x1f &&
            p[curr->len+1] == 0x8b &&
            p[curr->len+2] == 0x08 &&
            p[curr->len+3] == 0x00) {
          break;
        }
      }
      pos += curr->len;
    }
  }

  return img;
}

#define BUFFER_SIZE 32768

/*
 * Takes the uncompressed data stored in the chunk, compresses it
 * using the zlib parameters stored in the chunk, and checks that it
 * matches exactly the compressed data we started with (also stored in
 * the chunk).  Return 0 on success.
 */
int TryReconstruction(ImageChunk* chunk, unsigned char* out) {
  size_t p = 0;

#if 0
  printf("trying %d %d %d %d %d\n",
          chunk->level, chunk->method, chunk->windowBits,
          chunk->memLevel, chunk->strategy);
#endif

  z_stream strm;
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = chunk->len;
  strm.next_in = chunk->data;
  int ret;
  ret = deflateInit2(&strm, chunk->level, chunk->method, chunk->windowBits,
                     chunk->memLevel, chunk->strategy);
  do {
    strm.avail_out = BUFFER_SIZE;
    strm.next_out = out;
    ret = deflate(&strm, Z_FINISH);
    size_t have = BUFFER_SIZE - strm.avail_out;

    if (memcmp(out, chunk->deflate_data+p, have) != 0) {
      // mismatch; data isn't the same.
      deflateEnd(&strm);
      return -1;
    }
    p += have;
  } while (ret != Z_STREAM_END);
  deflateEnd(&strm);
  if (p != chunk->deflate_len) {
    // mismatch; ran out of data before we should have.
    return -1;
  }
  return 0;
}

/*
 * Verify that we can reproduce exactly the same compressed data that
 * we started with.  Sets the level, method, windowBits, memLevel, and
 * strategy fields in the chunk to the encoding parameters needed to
 * produce the right output.  Returns 0 on success.
 */
int ReconstructDeflateChunk(ImageChunk* chunk) {
  if (chunk->type != CHUNK_DEFLATE) {
    printf("attempt to reconstruct non-deflate chunk\n");
    return -1;
  }

  size_t p = 0;
  unsigned char* out = reinterpret_cast<unsigned char*>(malloc(BUFFER_SIZE));

  // We only check two combinations of encoder parameters:  level 6
  // (the default) and level 9 (the maximum).
  for (chunk->level = 6; chunk->level <= 9; chunk->level += 3) {
    chunk->windowBits = -15;  // 32kb window; negative to indicate a raw stream.
    chunk->memLevel = 8;      // the default value.
    chunk->method = Z_DEFLATED;
    chunk->strategy = Z_DEFAULT_STRATEGY;

    if (TryReconstruction(chunk, out) == 0) {
      free(out);
      return 0;
    }
  }

  free(out);
  return -1;
}

/*
 * Given source and target chunks, compute a bsdiff patch between them
 * by running bsdiff in a subprocess.  Return the patch data, placing
 * its length in *size.  Return NULL on failure.  We expect the bsdiff
 * program to be in the path.
 */
unsigned char* MakePatch(ImageChunk* src, ImageChunk* tgt, size_t* size) {
  if (tgt->type == CHUNK_NORMAL) {
    if (tgt->len <= 160) {
      tgt->type = CHUNK_RAW;
      *size = tgt->len;
      return tgt->data;
    }
  }

  char ptemp[] = "/tmp/imgdiff-patch-XXXXXX";
  int fd = mkstemp(ptemp);

  if (fd == -1) {
    printf("MakePatch failed to create a temporary file: %s\n",
           strerror(errno));
    return NULL;
  }
  close(fd); // temporary file is created and we don't need its file
             // descriptor

  int r = bsdiff(src->data, src->len, &(src->I), tgt->data, tgt->len, ptemp);
  if (r != 0) {
    printf("bsdiff() failed: %d\n", r);
    return NULL;
  }

  struct stat st;
  if (stat(ptemp, &st) != 0) {
    printf("failed to stat patch file %s: %s\n",
            ptemp, strerror(errno));
    return NULL;
  }

  size_t sz = static_cast<size_t>(st.st_size);
  // TODO: Memory leak on error return.
  unsigned char* data = reinterpret_cast<unsigned char*>(malloc(sz));

  if (tgt->type == CHUNK_NORMAL && tgt->len <= sz) {
    unlink(ptemp);

    tgt->type = CHUNK_RAW;
    *size = tgt->len;
    return tgt->data;
  }

  *size = sz;

  FILE* f = fopen(ptemp, "rb");
  if (f == NULL) {
    printf("failed to open patch %s: %s\n", ptemp, strerror(errno));
    return NULL;
  }
  if (fread(data, 1, sz, f) != sz) {
    printf("failed to read patch %s: %s\n", ptemp, strerror(errno));
    return NULL;
  }
  fclose(f);

  unlink(ptemp);

  tgt->source_start = src->start;
  switch (tgt->type) {
    case CHUNK_NORMAL:
      tgt->source_len = src->len;
      break;
    case CHUNK_DEFLATE:
      tgt->source_len = src->deflate_len;
      tgt->source_uncompressed_len = src->len;
      break;
  }

  return data;
}

/*
 * Cause a gzip chunk to be treated as a normal chunk (ie, as a blob
 * of uninterpreted data).  The resulting patch will likely be about
 * as big as the target file, but it lets us handle the case of images
 * where some gzip chunks are reconstructible but others aren't (by
 * treating the ones that aren't as normal chunks).
 */
void ChangeDeflateChunkToNormal(ImageChunk* ch) {
  if (ch->type != CHUNK_DEFLATE) return;
  ch->type = CHUNK_NORMAL;
  free(ch->data);
  ch->data = ch->deflate_data;
  ch->len = ch->deflate_len;
}

/*
 * Return true if the data in the chunk is identical (including the
 * compressed representation, for gzip chunks).
 */
int AreChunksEqual(ImageChunk* a, ImageChunk* b) {
    if (a->type != b->type) return 0;

    switch (a->type) {
        case CHUNK_NORMAL:
            return a->len == b->len && memcmp(a->data, b->data, a->len) == 0;

        case CHUNK_DEFLATE:
            return a->deflate_len == b->deflate_len &&
                memcmp(a->deflate_data, b->deflate_data, a->deflate_len) == 0;

        default:
            printf("unknown chunk type %d\n", a->type);
            return 0;
    }
}

/*
 * Look for runs of adjacent normal chunks and compress them down into
 * a single chunk.  (Such runs can be produced when deflate chunks are
 * changed to normal chunks.)
 */
void MergeAdjacentNormalChunks(ImageChunk* chunks, int* num_chunks) {
  int out = 0;
  int in_start = 0, in_end;
  while (in_start < *num_chunks) {
    if (chunks[in_start].type != CHUNK_NORMAL) {
      in_end = in_start+1;
    } else {
      // in_start is a normal chunk.  Look for a run of normal chunks
      // that constitute a solid block of data (ie, each chunk begins
      // where the previous one ended).
      for (in_end = in_start+1;
           in_end < *num_chunks && chunks[in_end].type == CHUNK_NORMAL &&
             (chunks[in_end].start ==
              chunks[in_end-1].start + chunks[in_end-1].len &&
              chunks[in_end].data ==
              chunks[in_end-1].data + chunks[in_end-1].len);
           ++in_end);
    }

    if (in_end == in_start+1) {
#if 0
      printf("chunk %d is now %d\n", in_start, out);
#endif
      if (out != in_start) {
        memcpy(chunks+out, chunks+in_start, sizeof(ImageChunk));
      }
    } else {
#if 0
      printf("collapse normal chunks %d-%d into %d\n", in_start, in_end-1, out);
#endif

      // Merge chunks [in_start, in_end-1] into one chunk.  Since the
      // data member of each chunk is just a pointer into an in-memory
      // copy of the file, this can be done without recopying (the
      // output chunk has the first chunk's start location and data
      // pointer, and length equal to the sum of the input chunk
      // lengths).
      chunks[out].type = CHUNK_NORMAL;
      chunks[out].start = chunks[in_start].start;
      chunks[out].data = chunks[in_start].data;
      chunks[out].len = chunks[in_end-1].len +
        (chunks[in_end-1].start - chunks[in_start].start);
    }

    ++out;
    in_start = in_end;
  }
  *num_chunks = out;
}

ImageChunk* FindChunkByName(const char* name,
                            ImageChunk* chunks, int num_chunks) {
  int i;
  for (i = 0; i < num_chunks; ++i) {
    if (chunks[i].type == CHUNK_DEFLATE && chunks[i].filename &&
        strcmp(name, chunks[i].filename) == 0) {
      return chunks+i;
    }
  }
  return NULL;
}

void DumpChunks(ImageChunk* chunks, int num_chunks) {
    for (int i = 0; i < num_chunks; ++i) {
        printf("chunk %d: type %d start %zu len %zu\n",
               i, chunks[i].type, chunks[i].start, chunks[i].len);
    }
}

int main(int argc, char** argv) {
  int zip_mode = 0;

  if (argc >= 2 && strcmp(argv[1], "-z") == 0) {
    zip_mode = 1;
    --argc;
    ++argv;
  }

  size_t bonus_size = 0;
  unsigned char* bonus_data = NULL;
  if (argc >= 3 && strcmp(argv[1], "-b") == 0) {
    struct stat st;
    if (stat(argv[2], &st) != 0) {
      printf("failed to stat bonus file %s: %s\n", argv[2], strerror(errno));
      return 1;
    }
    bonus_size = st.st_size;
    bonus_data = reinterpret_cast<unsigned char*>(malloc(bonus_size));
    FILE* f = fopen(argv[2], "rb");
    if (f == NULL) {
      printf("failed to open bonus file %s: %s\n", argv[2], strerror(errno));
      return 1;
    }
    if (fread(bonus_data, 1, bonus_size, f) != bonus_size) {
      printf("failed to read bonus file %s: %s\n", argv[2], strerror(errno));
      return 1;
    }
    fclose(f);

    argc -= 2;
    argv += 2;
  }

  if (argc != 4) {
    usage:
    printf("usage: %s [-z] [-b <bonus-file>] <src-img> <tgt-img> <patch-file>\n",
            argv[0]);
    return 2;
  }

  int num_src_chunks;
  ImageChunk* src_chunks;
  int num_tgt_chunks;
  ImageChunk* tgt_chunks;
  int i;

  if (zip_mode) {
    if (ReadZip(argv[1], &num_src_chunks, &src_chunks, 1) == NULL) {
      printf("failed to break apart source zip file\n");
      return 1;
    }
    if (ReadZip(argv[2], &num_tgt_chunks, &tgt_chunks, 0) == NULL) {
      printf("failed to break apart target zip file\n");
      return 1;
    }
  } else {
    if (ReadImage(argv[1], &num_src_chunks, &src_chunks) == NULL) {
      printf("failed to break apart source image\n");
      return 1;
    }
    if (ReadImage(argv[2], &num_tgt_chunks, &tgt_chunks) == NULL) {
      printf("failed to break apart target image\n");
      return 1;
    }

    // Verify that the source and target images have the same chunk
    // structure (ie, the same sequence of deflate and normal chunks).

    if (!zip_mode) {
        // Merge the gzip header and footer in with any adjacent
        // normal chunks.
        MergeAdjacentNormalChunks(tgt_chunks, &num_tgt_chunks);
        MergeAdjacentNormalChunks(src_chunks, &num_src_chunks);
    }

    if (num_src_chunks != num_tgt_chunks) {
      printf("source and target don't have same number of chunks!\n");
      printf("source chunks:\n");
      DumpChunks(src_chunks, num_src_chunks);
      printf("target chunks:\n");
      DumpChunks(tgt_chunks, num_tgt_chunks);
      return 1;
    }
    for (i = 0; i < num_src_chunks; ++i) {
      if (src_chunks[i].type != tgt_chunks[i].type) {
        printf("source and target don't have same chunk "
                "structure! (chunk %d)\n", i);
        printf("source chunks:\n");
        DumpChunks(src_chunks, num_src_chunks);
        printf("target chunks:\n");
        DumpChunks(tgt_chunks, num_tgt_chunks);
        return 1;
      }
    }
  }

  for (i = 0; i < num_tgt_chunks; ++i) {
    if (tgt_chunks[i].type == CHUNK_DEFLATE) {
      // Confirm that given the uncompressed chunk data in the target, we
      // can recompress it and get exactly the same bits as are in the
      // input target image.  If this fails, treat the chunk as a normal
      // non-deflated chunk.
      if (ReconstructDeflateChunk(tgt_chunks+i) < 0) {
        printf("failed to reconstruct target deflate chunk %d [%s]; "
               "treating as normal\n", i, tgt_chunks[i].filename);
        ChangeDeflateChunkToNormal(tgt_chunks+i);
        if (zip_mode) {
          ImageChunk* src = FindChunkByName(tgt_chunks[i].filename, src_chunks, num_src_chunks);
          if (src) {
            ChangeDeflateChunkToNormal(src);
          }
        } else {
          ChangeDeflateChunkToNormal(src_chunks+i);
        }
        continue;
      }

      // If two deflate chunks are identical (eg, the kernel has not
      // changed between two builds), treat them as normal chunks.
      // This makes applypatch much faster -- it can apply a trivial
      // patch to the compressed data, rather than uncompressing and
      // recompressing to apply the trivial patch to the uncompressed
      // data.
      ImageChunk* src;
      if (zip_mode) {
        src = FindChunkByName(tgt_chunks[i].filename, src_chunks, num_src_chunks);
      } else {
        src = src_chunks+i;
      }

      if (src == NULL || AreChunksEqual(tgt_chunks+i, src)) {
        ChangeDeflateChunkToNormal(tgt_chunks+i);
        if (src) {
          ChangeDeflateChunkToNormal(src);
        }
      }
    }
  }

  // Merging neighboring normal chunks.
  if (zip_mode) {
    // For zips, we only need to do this to the target:  deflated
    // chunks are matched via filename, and normal chunks are patched
    // using the entire source file as the source.
    MergeAdjacentNormalChunks(tgt_chunks, &num_tgt_chunks);
  } else {
    // For images, we need to maintain the parallel structure of the
    // chunk lists, so do the merging in both the source and target
    // lists.
    MergeAdjacentNormalChunks(tgt_chunks, &num_tgt_chunks);
    MergeAdjacentNormalChunks(src_chunks, &num_src_chunks);
    if (num_src_chunks != num_tgt_chunks) {
      // This shouldn't happen.
      printf("merging normal chunks went awry\n");
      return 1;
    }
  }

  // Compute bsdiff patches for each chunk's data (the uncompressed
  // data, in the case of deflate chunks).

  DumpChunks(src_chunks, num_src_chunks);

  printf("Construct patches for %d chunks...\n", num_tgt_chunks);
  unsigned char** patch_data = reinterpret_cast<unsigned char**>(malloc(
      num_tgt_chunks * sizeof(unsigned char*)));
  size_t* patch_size = reinterpret_cast<size_t*>(malloc(num_tgt_chunks * sizeof(size_t)));
  for (i = 0; i < num_tgt_chunks; ++i) {
    if (zip_mode) {
      ImageChunk* src;
      if (tgt_chunks[i].type == CHUNK_DEFLATE &&
          (src = FindChunkByName(tgt_chunks[i].filename, src_chunks,
                                 num_src_chunks))) {
        patch_data[i] = MakePatch(src, tgt_chunks+i, patch_size+i);
      } else {
        patch_data[i] = MakePatch(src_chunks, tgt_chunks+i, patch_size+i);
      }
    } else {
      if (i == 1 && bonus_data) {
        printf("  using %zu bytes of bonus data for chunk %d\n", bonus_size, i);
        src_chunks[i].data = reinterpret_cast<unsigned char*>(realloc(src_chunks[i].data,
            src_chunks[i].len + bonus_size));
        memcpy(src_chunks[i].data+src_chunks[i].len, bonus_data, bonus_size);
        src_chunks[i].len += bonus_size;
     }

      patch_data[i] = MakePatch(src_chunks+i, tgt_chunks+i, patch_size+i);
    }
    printf("patch %3d is %zu bytes (of %zu)\n",
           i, patch_size[i], tgt_chunks[i].source_len);
  }

  // Figure out how big the imgdiff file header is going to be, so
  // that we can correctly compute the offset of each bsdiff patch
  // within the file.

  size_t total_header_size = 12;
  for (i = 0; i < num_tgt_chunks; ++i) {
    total_header_size += 4;
    switch (tgt_chunks[i].type) {
      case CHUNK_NORMAL:
        total_header_size += 8*3;
        break;
      case CHUNK_DEFLATE:
        total_header_size += 8*5 + 4*5;
        break;
      case CHUNK_RAW:
        total_header_size += 4 + patch_size[i];
        break;
    }
  }

  size_t offset = total_header_size;

  FILE* f = fopen(argv[3], "wb");

  // Write out the headers.

  fwrite("IMGDIFF2", 1, 8, f);
  Write4(num_tgt_chunks, f);
  for (i = 0; i < num_tgt_chunks; ++i) {
    Write4(tgt_chunks[i].type, f);

    switch (tgt_chunks[i].type) {
      case CHUNK_NORMAL:
        printf("chunk %3d: normal   (%10zu, %10zu)  %10zu\n", i,
               tgt_chunks[i].start, tgt_chunks[i].len, patch_size[i]);
        Write8(tgt_chunks[i].source_start, f);
        Write8(tgt_chunks[i].source_len, f);
        Write8(offset, f);
        offset += patch_size[i];
        break;

      case CHUNK_DEFLATE:
        printf("chunk %3d: deflate  (%10zu, %10zu)  %10zu  %s\n", i,
               tgt_chunks[i].start, tgt_chunks[i].deflate_len, patch_size[i],
               tgt_chunks[i].filename);
        Write8(tgt_chunks[i].source_start, f);
        Write8(tgt_chunks[i].source_len, f);
        Write8(offset, f);
        Write8(tgt_chunks[i].source_uncompressed_len, f);
        Write8(tgt_chunks[i].len, f);
        Write4(tgt_chunks[i].level, f);
        Write4(tgt_chunks[i].method, f);
        Write4(tgt_chunks[i].windowBits, f);
        Write4(tgt_chunks[i].memLevel, f);
        Write4(tgt_chunks[i].strategy, f);
        offset += patch_size[i];
        break;

      case CHUNK_RAW:
        printf("chunk %3d: raw      (%10zu, %10zu)\n", i,
               tgt_chunks[i].start, tgt_chunks[i].len);
        Write4(patch_size[i], f);
        fwrite(patch_data[i], 1, patch_size[i], f);
        break;
    }
  }

  // Append each chunk's bsdiff patch, in order.

  for (i = 0; i < num_tgt_chunks; ++i) {
    if (tgt_chunks[i].type != CHUNK_RAW) {
      fwrite(patch_data[i], 1, patch_size[i], f);
    }
  }

  fclose(f);

  return 0;
}
