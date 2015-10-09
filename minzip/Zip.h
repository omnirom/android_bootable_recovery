/*
 * Copyright 2006 The Android Open Source Project
 *
 * Simple Zip archive support.
 */
#ifndef _MINZIP_ZIP
#define _MINZIP_ZIP

#include "inline_magic.h"

#include <stdlib.h>
#include <utime.h>

#include "Hash.h"
#include "SysUtil.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <selinux/selinux.h>
#include <selinux/label.h>

/*
 * One entry in the Zip archive.  Treat this as opaque -- use accessors below.
 *
 * TODO: we're now keeping the pages mapped so we don't have to copy the
 * filename.  We can change the accessors to retrieve the various pieces
 * directly from the source file instead of copying them out, for a very
 * slight speed hit and a modest reduction in memory usage.
 */
typedef struct ZipEntry {
    unsigned int fileNameLen;
    const char*  fileName;       // not null-terminated
    long         offset;
    long         compLen;
    long         uncompLen;
    int          compression;
    long         modTime;
    long         crc32;
    int          versionMadeBy;
    long         externalFileAttributes;
} ZipEntry;

/*
 * One Zip archive.  Treat as opaque.
 */
typedef struct ZipArchive {
    unsigned int   numEntries;
    ZipEntry*      pEntries;
    HashTable*     pHash;          // maps file name to ZipEntry
    unsigned char* addr;
    size_t         length;
} ZipArchive;

/*
 * Represents a non-NUL-terminated string,
 * which is how entry names are stored.
 */
typedef struct {
    const char *str;
    size_t len;
} UnterminatedString;

/*
 * Open a Zip archive.
 *
 * On success, returns 0 and populates "pArchive".  Returns nonzero errno
 * value on failure.
 */
int mzOpenZipArchive(unsigned char* addr, size_t length, ZipArchive* pArchive);

/*
 * Close archive, releasing resources associated with it.
 *
 * Depending on the implementation this could unmap pages used by classes
 * stored in a Jar.  This should only be done after unloading classes.
 */
void mzCloseZipArchive(ZipArchive* pArchive);


/*
 * Find an entry in the Zip archive, by name.
 */
const ZipEntry* mzFindZipEntry(const ZipArchive* pArchive,
        const char* entryName);

INLINE long mzGetZipEntryOffset(const ZipEntry* pEntry) {
    return pEntry->offset;
}
INLINE long mzGetZipEntryUncompLen(const ZipEntry* pEntry) {
    return pEntry->uncompLen;
}

/*
 * Type definition for the callback function used by
 * mzProcessZipEntryContents().
 */
typedef bool (*ProcessZipEntryContentsFunction)(const unsigned char *data,
    int dataLen, void *cookie);

/*
 * Stream the uncompressed data through the supplied function,
 * passing cookie to it each time it gets called.  processFunction
 * may be called more than once.
 *
 * If processFunction returns false, the operation is abandoned and
 * mzProcessZipEntryContents() immediately returns false.
 *
 * This is useful for calculating the hash of an entry's uncompressed contents.
 */
bool mzProcessZipEntryContents(const ZipArchive *pArchive,
    const ZipEntry *pEntry, ProcessZipEntryContentsFunction processFunction,
    void *cookie);

/*
 * Read an entry into a buffer allocated by the caller.
 */
bool mzReadZipEntry(const ZipArchive* pArchive, const ZipEntry* pEntry,
        char* buf, int bufLen);

/*
 * Inflate and write an entry to a file.
 */
bool mzExtractZipEntryToFile(const ZipArchive *pArchive,
    const ZipEntry *pEntry, int fd);

/*
 * Inflate and write an entry to a memory buffer, which must be long
 * enough to hold mzGetZipEntryUncomplen(pEntry) bytes.
 */
bool mzExtractZipEntryToBuffer(const ZipArchive *pArchive,
    const ZipEntry *pEntry, unsigned char* buffer);

/*
 * Inflate all files under zipDir to the directory specified by
 * targetDir, which must exist and be a writable directory.
 *
 * Directory entries and symlinks are not extracted.
 *
 *
 * The immediate children of zipDir will become the immediate
 * children of targetDir; e.g., if the archive contains the entries
 *
 *     a/b/c/one
 *     a/b/c/two
 *     a/b/c/d/three
 *
 * and mzExtractRecursive(a, "a/b/c", "/tmp", ...) is called, the resulting
 * files will be
 *
 *     /tmp/one
 *     /tmp/two
 *     /tmp/d/three
 *
 * If timestamp is non-NULL, file timestamps will be set accordingly.
 *
 * If callback is non-NULL, it will be invoked with each unpacked file.
 *
 * Returns true on success, false on failure.
 */
bool mzExtractRecursive(const ZipArchive *pArchive,
        const char *zipDir, const char *targetDir,
        const struct utimbuf *timestamp,
        void (*callback)(const char *fn, void*), void *cookie,
        struct selabel_handle *sehnd);

#ifdef __cplusplus
}
#endif

#endif /*_MINZIP_ZIP*/
