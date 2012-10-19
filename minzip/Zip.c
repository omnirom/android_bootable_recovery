/*
 * Copyright 2006 The Android Open Source Project
 *
 * Simple Zip file support.
 */
#include "safe_iop.h"
#include "zlib.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>     // for uintptr_t
#include <stdlib.h>
#include <sys/stat.h>   // for S_ISLNK()
#include <unistd.h>

#define LOG_TAG "minzip"
#include "Zip.h"
#include "Bits.h"
#include "Log.h"
#include "DirUtil.h"

#undef NDEBUG   // do this after including Log.h
#include <assert.h>

#define SORT_ENTRIES 1

/*
 * Offset and length constants (java.util.zip naming convention).
 */
enum {
    CENSIG = 0x02014b50,      // PK12
    CENHDR = 46,

    CENVEM =  4,
    CENVER =  6,
    CENFLG =  8,
    CENHOW = 10,
    CENTIM = 12,
    CENCRC = 16,
    CENSIZ = 20,
    CENLEN = 24,
    CENNAM = 28,
    CENEXT = 30,
    CENCOM = 32,
    CENDSK = 34,
    CENATT = 36,
    CENATX = 38,
    CENOFF = 42,

    ENDSIG = 0x06054b50,     // PK56
    ENDHDR = 22,

    ENDSUB =  8,
    ENDTOT = 10,
    ENDSIZ = 12,
    ENDOFF = 16,
    ENDCOM = 20,

    EXTSIG = 0x08074b50,     // PK78
    EXTHDR = 16,

    EXTCRC =  4,
    EXTSIZ =  8,
    EXTLEN = 12,

    LOCSIG = 0x04034b50,      // PK34
    LOCHDR = 30,

    LOCVER =  4,
    LOCFLG =  6,
    LOCHOW =  8,
    LOCTIM = 10,
    LOCCRC = 14,
    LOCSIZ = 18,
    LOCLEN = 22,
    LOCNAM = 26,
    LOCEXT = 28,

    STORED = 0,
    DEFLATED = 8,

    CENVEM_UNIX = 3 << 8,   // the high byte of CENVEM
};


/*
 * For debugging, dump the contents of a ZipEntry.
 */
#if 0
static void dumpEntry(const ZipEntry* pEntry)
{
    LOGI(" %p '%.*s'\n", pEntry->fileName,pEntry->fileNameLen,pEntry->fileName);
    LOGI("   off=%ld comp=%ld uncomp=%ld how=%d\n", pEntry->offset,
        pEntry->compLen, pEntry->uncompLen, pEntry->compression);
}
#endif

/*
 * (This is a mzHashTableLookup callback.)
 *
 * Compare two ZipEntry structs, by name.
 */
static int hashcmpZipEntry(const void* ventry1, const void* ventry2)
{
    const ZipEntry* entry1 = (const ZipEntry*) ventry1;
    const ZipEntry* entry2 = (const ZipEntry*) ventry2;

    if (entry1->fileNameLen != entry2->fileNameLen)
        return entry1->fileNameLen - entry2->fileNameLen;
    return memcmp(entry1->fileName, entry2->fileName, entry1->fileNameLen);
}

/*
 * (This is a mzHashTableLookup callback.)
 *
 * find a ZipEntry struct by name.
 */
static int hashcmpZipName(const void* ventry, const void* vname)
{
    const ZipEntry* entry = (const ZipEntry*) ventry;
    const char* name = (const char*) vname;
    unsigned int nameLen = strlen(name);

    if (entry->fileNameLen != nameLen)
        return entry->fileNameLen - nameLen;
    return memcmp(entry->fileName, name, nameLen);
}

/*
 * Compute the hash code for a ZipEntry filename.
 *
 * Not expected to be compatible with any other hash function, so we init
 * to 2 to ensure it doesn't happen to match.
 */
static unsigned int computeHash(const char* name, int nameLen)
{
    unsigned int hash = 2;

    while (nameLen--)
        hash = hash * 31 + *name++;

    return hash;
}

static void addEntryToHashTable(HashTable* pHash, ZipEntry* pEntry)
{
    unsigned int itemHash = computeHash(pEntry->fileName, pEntry->fileNameLen);
    const ZipEntry* found;

    found = (const ZipEntry*)mzHashTableLookup(pHash,
                itemHash, pEntry, hashcmpZipEntry, true);
    if (found != pEntry) {
        LOGW("WARNING: duplicate entry '%.*s' in Zip\n",
            found->fileNameLen, found->fileName);
        /* keep going */
    }
}

static int validFilename(const char *fileName, unsigned int fileNameLen)
{
    // Forbid super long filenames.
    if (fileNameLen >= PATH_MAX) {
        LOGW("Filename too long (%d chatacters)\n", fileNameLen);
        return 0;
    }

    // Require all characters to be printable ASCII (no NUL, no UTF-8, etc).
    unsigned int i;
    for (i = 0; i < fileNameLen; ++i) {
        if (fileName[i] < 32 || fileName[i] >= 127) {
            LOGW("Filename contains invalid character '\%03o'\n", fileName[i]);
            return 0;
        }
    }

    return 1;
}

/*
 * Parse the contents of a Zip archive.  After confirming that the file
 * is in fact a Zip, we scan out the contents of the central directory and
 * store it in a hash table.
 *
 * Returns "true" on success.
 */
static bool parseZipArchive(ZipArchive* pArchive, const MemMapping* pMap)
{
    bool result = false;
    const unsigned char* ptr;
    unsigned int i, numEntries, cdOffset;
    unsigned int val;

    /*
     * The first 4 bytes of the file will either be the local header
     * signature for the first file (LOCSIG) or, if the archive doesn't
     * have any files in it, the end-of-central-directory signature (ENDSIG).
     */
    val = get4LE(pMap->addr);
    if (val == ENDSIG) {
        LOGI("Found Zip archive, but it looks empty\n");
        goto bail;
    } else if (val != LOCSIG) {
        LOGV("Not a Zip archive (found 0x%08x)\n", val);
        goto bail;
    }

    /*
     * Find the EOCD.  We'll find it immediately unless they have a file
     * comment.
     */
    ptr = pMap->addr + pMap->length - ENDHDR;

    while (ptr >= (const unsigned char*) pMap->addr) {
        if (*ptr == (ENDSIG & 0xff) && get4LE(ptr) == ENDSIG)
            break;
        ptr--;
    }
    if (ptr < (const unsigned char*) pMap->addr) {
        LOGI("Could not find end-of-central-directory in Zip\n");
        goto bail;
    }

    /*
     * There are two interesting items in the EOCD block: the number of
     * entries in the file, and the file offset of the start of the
     * central directory.
     */
    numEntries = get2LE(ptr + ENDSUB);
    cdOffset = get4LE(ptr + ENDOFF);

    LOGVV("numEntries=%d cdOffset=%d\n", numEntries, cdOffset);
    if (numEntries == 0 || cdOffset >= pMap->length) {
        LOGW("Invalid entries=%d offset=%d (len=%zd)\n",
            numEntries, cdOffset, pMap->length);
        goto bail;
    }

    /*
     * Create data structures to hold entries.
     */
    pArchive->numEntries = numEntries;
    pArchive->pEntries = (ZipEntry*) calloc(numEntries, sizeof(ZipEntry));
    pArchive->pHash = mzHashTableCreate(mzHashSize(numEntries), NULL);
    if (pArchive->pEntries == NULL || pArchive->pHash == NULL)
        goto bail;

    ptr = pMap->addr + cdOffset;
    for (i = 0; i < numEntries; i++) {
        ZipEntry* pEntry;
        unsigned int fileNameLen, extraLen, commentLen, localHdrOffset;
        const unsigned char* localHdr;
        const char *fileName;

        if (ptr + CENHDR > (const unsigned char*)pMap->addr + pMap->length) {
            LOGW("Ran off the end (at %d)\n", i);
            goto bail;
        }
        if (get4LE(ptr) != CENSIG) {
            LOGW("Missed a central dir sig (at %d)\n", i);
            goto bail;
        }

        localHdrOffset = get4LE(ptr + CENOFF);
        fileNameLen = get2LE(ptr + CENNAM);
        extraLen = get2LE(ptr + CENEXT);
        commentLen = get2LE(ptr + CENCOM);
        fileName = (const char*)ptr + CENHDR;
        if (fileName + fileNameLen > (const char*)pMap->addr + pMap->length) {
            LOGW("Filename ran off the end (at %d)\n", i);
            goto bail;
        }
        if (!validFilename(fileName, fileNameLen)) {
            LOGW("Invalid filename (at %d)\n", i);
            goto bail;
        }

#if SORT_ENTRIES
        /* Figure out where this entry should go (binary search).
         */
        if (i > 0) {
            int low, high;

            low = 0;
            high = i - 1;
            while (low <= high) {
                int mid;
                int diff;
                int diffLen;

                mid = low + ((high - low) / 2); // avoid overflow

                if (pArchive->pEntries[mid].fileNameLen < fileNameLen) {
                    diffLen = pArchive->pEntries[mid].fileNameLen;
                } else {
                    diffLen = fileNameLen;
                }
                diff = strncmp(pArchive->pEntries[mid].fileName, fileName,
                        diffLen);
                if (diff == 0) {
                    diff = pArchive->pEntries[mid].fileNameLen - fileNameLen;
                }
                if (diff < 0) {
                    low = mid + 1;
                } else if (diff > 0) {
                    high = mid - 1;
                } else {
                    high = mid;
                    break;
                }
            }

            unsigned int target = high + 1;
            assert(target <= i);
            if (target != i) {
                /* It belongs somewhere other than at the end of
                 * the list.  Make some room at [target].
                 */
                memmove(pArchive->pEntries + target + 1,
                        pArchive->pEntries + target,
                        (i - target) * sizeof(ZipEntry));
            }
            pEntry = &pArchive->pEntries[target];
        } else {
            pEntry = &pArchive->pEntries[0];
        }
#else
        pEntry = &pArchive->pEntries[i];
#endif

        //LOGI("%d: localHdr=%d fnl=%d el=%d cl=%d\n",
        //    i, localHdrOffset, fileNameLen, extraLen, commentLen);

        pEntry->fileNameLen = fileNameLen;
        pEntry->fileName = fileName;

        pEntry->compLen = get4LE(ptr + CENSIZ);
        pEntry->uncompLen = get4LE(ptr + CENLEN);
        pEntry->compression = get2LE(ptr + CENHOW);
        pEntry->modTime = get4LE(ptr + CENTIM);
        pEntry->crc32 = get4LE(ptr + CENCRC);

        /* These two are necessary for finding the mode of the file.
         */
        pEntry->versionMadeBy = get2LE(ptr + CENVEM);
        if ((pEntry->versionMadeBy & 0xff00) != 0 &&
                (pEntry->versionMadeBy & 0xff00) != CENVEM_UNIX)
        {
            LOGW("Incompatible \"version made by\": 0x%02x (at %d)\n",
                    pEntry->versionMadeBy >> 8, i);
            goto bail;
        }
        pEntry->externalFileAttributes = get4LE(ptr + CENATX);

        // Perform pMap->addr + localHdrOffset, ensuring that it won't
        // overflow. This is needed because localHdrOffset is untrusted.
        if (!safe_add((uintptr_t *)&localHdr, (uintptr_t)pMap->addr,
            (uintptr_t)localHdrOffset)) {
            LOGW("Integer overflow adding in parseZipArchive\n");
            goto bail;
        }
        if ((uintptr_t)localHdr + LOCHDR >
            (uintptr_t)pMap->addr + pMap->length) {
            LOGW("Bad offset to local header: %d (at %d)\n", localHdrOffset, i);
            goto bail;
        }
        if (get4LE(localHdr) != LOCSIG) {
            LOGW("Missed a local header sig (at %d)\n", i);
            goto bail;
        }
        pEntry->offset = localHdrOffset + LOCHDR
            + get2LE(localHdr + LOCNAM) + get2LE(localHdr + LOCEXT);
        if (!safe_add(NULL, pEntry->offset, pEntry->compLen)) {
            LOGW("Integer overflow adding in parseZipArchive\n");
            goto bail;
        }
        if ((size_t)pEntry->offset + pEntry->compLen > pMap->length) {
            LOGW("Data ran off the end (at %d)\n", i);
            goto bail;
        }

#if !SORT_ENTRIES
        /* Add to hash table; no need to lock here.
         * Can't do this now if we're sorting, because entries
         * will move around.
         */
        addEntryToHashTable(pArchive->pHash, pEntry);
#endif

        //dumpEntry(pEntry);
        ptr += CENHDR + fileNameLen + extraLen + commentLen;
    }

#if SORT_ENTRIES
    /* If we're sorting, we have to wait until all entries
     * are in their final places, otherwise the pointers will
     * probably point to the wrong things.
     */
    for (i = 0; i < numEntries; i++) {
        /* Add to hash table; no need to lock here.
         */
        addEntryToHashTable(pArchive->pHash, &pArchive->pEntries[i]);
    }
#endif

    result = true;

bail:
    if (!result) {
        mzHashTableFree(pArchive->pHash);
        pArchive->pHash = NULL;
    }
    return result;
}

/*
 * Open a Zip archive and scan out the contents.
 *
 * The easiest way to do this is to mmap() the whole thing and do the
 * traditional backward scan for central directory.  Since the EOCD is
 * a relatively small bit at the end, we should end up only touching a
 * small set of pages.
 *
 * This will be called on non-Zip files, especially during startup, so
 * we don't want to be too noisy about failures.  (Do we want a "quiet"
 * flag?)
 *
 * On success, we fill out the contents of "pArchive".
 */
int mzOpenZipArchive(const char* fileName, ZipArchive* pArchive)
{
    MemMapping map;
    int err;

    LOGV("Opening archive '%s' %p\n", fileName, pArchive);

    map.addr = NULL;
    memset(pArchive, 0, sizeof(*pArchive));

    pArchive->fd = open(fileName, O_RDONLY, 0);
    if (pArchive->fd < 0) {
        err = errno ? errno : -1;
        LOGV("Unable to open '%s': %s\n", fileName, strerror(err));
        goto bail;
    }

    if (sysMapFileInShmem(pArchive->fd, &map) != 0) {
        err = -1;
        LOGW("Map of '%s' failed\n", fileName);
        goto bail;
    }

    if (map.length < ENDHDR) {
        err = -1;
        LOGV("File '%s' too small to be zip (%zd)\n", fileName, map.length);
        goto bail;
    }

    if (!parseZipArchive(pArchive, &map)) {
        err = -1;
        LOGV("Parsing '%s' failed\n", fileName);
        goto bail;
    }

    err = 0;
    sysCopyMap(&pArchive->map, &map);
    map.addr = NULL;

bail:
    if (err != 0)
        mzCloseZipArchive(pArchive);
    if (map.addr != NULL)
        sysReleaseShmem(&map);
    return err;
}

/*
 * Close a ZipArchive, closing the file and freeing the contents.
 *
 * NOTE: the ZipArchive may not have been fully created.
 */
void mzCloseZipArchive(ZipArchive* pArchive)
{
    LOGV("Closing archive %p\n", pArchive);

    if (pArchive->fd >= 0)
        close(pArchive->fd);
    if (pArchive->map.addr != NULL)
        sysReleaseShmem(&pArchive->map);

    free(pArchive->pEntries);

    mzHashTableFree(pArchive->pHash);

    pArchive->fd = -1;
    pArchive->pHash = NULL;
    pArchive->pEntries = NULL;
}

/*
 * Find a matching entry.
 *
 * Returns NULL if no matching entry found.
 */
const ZipEntry* mzFindZipEntry(const ZipArchive* pArchive,
        const char* entryName)
{
    unsigned int itemHash = computeHash(entryName, strlen(entryName));

    return (const ZipEntry*)mzHashTableLookup(pArchive->pHash,
                itemHash, (char*) entryName, hashcmpZipName, false);
}

/*
 * Return true if the entry is a symbolic link.
 */
bool mzIsZipEntrySymlink(const ZipEntry* pEntry)
{
    if ((pEntry->versionMadeBy & 0xff00) == CENVEM_UNIX) {
        return S_ISLNK(pEntry->externalFileAttributes >> 16);
    }
    return false;
}

/* Call processFunction on the uncompressed data of a STORED entry.
 */
static bool processStoredEntry(const ZipArchive *pArchive,
    const ZipEntry *pEntry, ProcessZipEntryContentsFunction processFunction,
    void *cookie)
{
    size_t bytesLeft = pEntry->compLen;
    while (bytesLeft > 0) {
        unsigned char buf[32 * 1024];
        ssize_t n;
        size_t count;
        bool ret;

        count = bytesLeft;
        if (count > sizeof(buf)) {
            count = sizeof(buf);
        }
        n = read(pArchive->fd, buf, count);
        if (n < 0 || (size_t)n != count) {
            LOGE("Can't read %zu bytes from zip file: %ld\n", count, n);
            return false;
        }
        ret = processFunction(buf, n, cookie);
        if (!ret) {
            return false;
        }
        bytesLeft -= count;
    }
    return true;
}

static bool processDeflatedEntry(const ZipArchive *pArchive,
    const ZipEntry *pEntry, ProcessZipEntryContentsFunction processFunction,
    void *cookie)
{
    long result = -1;
    unsigned char readBuf[32 * 1024];
    unsigned char procBuf[32 * 1024];
    z_stream zstream;
    int zerr;
    long compRemaining;

    compRemaining = pEntry->compLen;

    /*
     * Initialize the zlib stream.
     */
    memset(&zstream, 0, sizeof(zstream));
    zstream.zalloc = Z_NULL;
    zstream.zfree = Z_NULL;
    zstream.opaque = Z_NULL;
    zstream.next_in = NULL;
    zstream.avail_in = 0;
    zstream.next_out = (Bytef*) procBuf;
    zstream.avail_out = sizeof(procBuf);
    zstream.data_type = Z_UNKNOWN;

    /*
     * Use the undocumented "negative window bits" feature to tell zlib
     * that there's no zlib header waiting for it.
     */
    zerr = inflateInit2(&zstream, -MAX_WBITS);
    if (zerr != Z_OK) {
        if (zerr == Z_VERSION_ERROR) {
            LOGE("Installed zlib is not compatible with linked version (%s)\n",
                ZLIB_VERSION);
        } else {
            LOGE("Call to inflateInit2 failed (zerr=%d)\n", zerr);
        }
        goto bail;
    }

    /*
     * Loop while we have data.
     */
    do {
        /* read as much as we can */
        if (zstream.avail_in == 0) {
            long getSize = (compRemaining > (long)sizeof(readBuf)) ?
                        (long)sizeof(readBuf) : compRemaining;
            LOGVV("+++ reading %ld bytes (%ld left)\n",
                getSize, compRemaining);

            int cc = read(pArchive->fd, readBuf, getSize);
            if (cc != (int) getSize) {
                LOGW("inflate read failed (%d vs %ld)\n", cc, getSize);
                goto z_bail;
            }

            compRemaining -= getSize;

            zstream.next_in = readBuf;
            zstream.avail_in = getSize;
        }

        /* uncompress the data */
        zerr = inflate(&zstream, Z_NO_FLUSH);
        if (zerr != Z_OK && zerr != Z_STREAM_END) {
            LOGD("zlib inflate call failed (zerr=%d)\n", zerr);
            goto z_bail;
        }

        /* write when we're full or when we're done */
        if (zstream.avail_out == 0 ||
            (zerr == Z_STREAM_END && zstream.avail_out != sizeof(procBuf)))
        {
            long procSize = zstream.next_out - procBuf;
            LOGVV("+++ processing %d bytes\n", (int) procSize);
            bool ret = processFunction(procBuf, procSize, cookie);
            if (!ret) {
                LOGW("Process function elected to fail (in inflate)\n");
                goto z_bail;
            }

            zstream.next_out = procBuf;
            zstream.avail_out = sizeof(procBuf);
        }
    } while (zerr == Z_OK);

    assert(zerr == Z_STREAM_END);       /* other errors should've been caught */

    // success!
    result = zstream.total_out;

z_bail:
    inflateEnd(&zstream);        /* free up any allocated structures */

bail:
    if (result != pEntry->uncompLen) {
        if (result != -1)        // error already shown?
            LOGW("Size mismatch on inflated file (%ld vs %ld)\n",
                result, pEntry->uncompLen);
        return false;
    }
    return true;
}

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
    void *cookie)
{
    bool ret = false;
    off_t oldOff;

    /* save current offset */
    oldOff = lseek(pArchive->fd, 0, SEEK_CUR);

    /* Seek to the beginning of the entry's compressed data. */
    lseek(pArchive->fd, pEntry->offset, SEEK_SET);

    switch (pEntry->compression) {
    case STORED:
        ret = processStoredEntry(pArchive, pEntry, processFunction, cookie);
        break;
    case DEFLATED:
        ret = processDeflatedEntry(pArchive, pEntry, processFunction, cookie);
        break;
    default:
        LOGE("Unsupported compression type %d for entry '%s'\n",
                pEntry->compression, pEntry->fileName);
        break;
    }

    /* restore file offset */
    lseek(pArchive->fd, oldOff, SEEK_SET);
    return ret;
}

static bool crcProcessFunction(const unsigned char *data, int dataLen,
        void *crc)
{
    *(unsigned long *)crc = crc32(*(unsigned long *)crc, data, dataLen);
    return true;
}

/*
 * Check the CRC on this entry; return true if it is correct.
 * May do other internal checks as well.
 */
bool mzIsZipEntryIntact(const ZipArchive *pArchive, const ZipEntry *pEntry)
{
    unsigned long crc;
    bool ret;

    crc = crc32(0L, Z_NULL, 0);
    ret = mzProcessZipEntryContents(pArchive, pEntry, crcProcessFunction,
            (void *)&crc);
    if (!ret) {
        LOGE("Can't calculate CRC for entry\n");
        return false;
    }
    if (crc != (unsigned long)pEntry->crc32) {
        LOGW("CRC for entry %.*s (0x%08lx) != expected (0x%08lx)\n",
                pEntry->fileNameLen, pEntry->fileName, crc, pEntry->crc32);
        return false;
    }
    return true;
}

typedef struct {
    char *buf;
    int bufLen;
} CopyProcessArgs;

static bool copyProcessFunction(const unsigned char *data, int dataLen,
        void *cookie)
{
    CopyProcessArgs *args = (CopyProcessArgs *)cookie;
    if (dataLen <= args->bufLen) {
        memcpy(args->buf, data, dataLen);
        args->buf += dataLen;
        args->bufLen -= dataLen;
        return true;
    }
    return false;
}

/*
 * Read an entry into a buffer allocated by the caller.
 */
bool mzReadZipEntry(const ZipArchive* pArchive, const ZipEntry* pEntry,
        char *buf, int bufLen)
{
    CopyProcessArgs args;
    bool ret;

    args.buf = buf;
    args.bufLen = bufLen;
    ret = mzProcessZipEntryContents(pArchive, pEntry, copyProcessFunction,
            (void *)&args);
    if (!ret) {
        LOGE("Can't extract entry to buffer.\n");
        return false;
    }
    return true;
}

static bool writeProcessFunction(const unsigned char *data, int dataLen,
                                 void *cookie)
{
    int fd = (int)cookie;

    ssize_t soFar = 0;
    while (true) {
        ssize_t n = write(fd, data+soFar, dataLen-soFar);
        if (n <= 0) {
            LOGE("Error writing %ld bytes from zip file from %p: %s\n",
                 dataLen-soFar, data+soFar, strerror(errno));
            if (errno != EINTR) {
              return false;
            }
        } else if (n > 0) {
            soFar += n;
            if (soFar == dataLen) return true;
            if (soFar > dataLen) {
                LOGE("write overrun?  (%ld bytes instead of %d)\n",
                     soFar, dataLen);
                return false;
            }
        }
    }
}

/*
 * Uncompress "pEntry" in "pArchive" to "fd" at the current offset.
 */
bool mzExtractZipEntryToFile(const ZipArchive *pArchive,
    const ZipEntry *pEntry, int fd)
{
    bool ret = mzProcessZipEntryContents(pArchive, pEntry, writeProcessFunction,
                                         (void*)fd);
    if (!ret) {
        LOGE("Can't extract entry to file.\n");
        return false;
    }
    return true;
}

typedef struct {
    unsigned char* buffer;
    long len;
} BufferExtractCookie;

static bool bufferProcessFunction(const unsigned char *data, int dataLen,
    void *cookie) {
    BufferExtractCookie *bec = (BufferExtractCookie*)cookie;

    memmove(bec->buffer, data, dataLen);
    bec->buffer += dataLen;
    bec->len -= dataLen;

    return true;
}

/*
 * Uncompress "pEntry" in "pArchive" to buffer, which must be large
 * enough to hold mzGetZipEntryUncomplen(pEntry) bytes.
 */
bool mzExtractZipEntryToBuffer(const ZipArchive *pArchive,
    const ZipEntry *pEntry, unsigned char *buffer)
{
    BufferExtractCookie bec;
    bec.buffer = buffer;
    bec.len = mzGetZipEntryUncompLen(pEntry);

    bool ret = mzProcessZipEntryContents(pArchive, pEntry,
        bufferProcessFunction, (void*)&bec);
    if (!ret || bec.len != 0) {
        LOGE("Can't extract entry to memory buffer.\n");
        return false;
    }
    return true;
}


/* Helper state to make path translation easier and less malloc-happy.
 */
typedef struct {
    const char *targetDir;
    const char *zipDir;
    char *buf;
    int targetDirLen;
    int zipDirLen;
    int bufLen;
} MzPathHelper;

/* Given the values of targetDir and zipDir in the helper,
 * return the target filename of the provided entry.
 * The helper must be initialized first.
 */
static const char *targetEntryPath(MzPathHelper *helper, ZipEntry *pEntry)
{
    int needLen;
    bool firstTime = (helper->buf == NULL);

    /* target file <-- targetDir + / + entry[zipDirLen:]
     */
    needLen = helper->targetDirLen + 1 +
            pEntry->fileNameLen - helper->zipDirLen + 1;
    if (needLen > helper->bufLen) {
        char *newBuf;

        needLen *= 2;
        newBuf = (char *)realloc(helper->buf, needLen);
        if (newBuf == NULL) {
            return NULL;
        }
        helper->buf = newBuf;
        helper->bufLen = needLen;
    }

    /* Every path will start with the target path and a slash.
     */
    if (firstTime) {
        char *p = helper->buf;
        memcpy(p, helper->targetDir, helper->targetDirLen);
        p += helper->targetDirLen;
        if (p == helper->buf || p[-1] != '/') {
            helper->targetDirLen += 1;
            *p++ = '/';
        }
    }

    /* Replace the custom part of the path with the appropriate
     * part of the entry's path.
     */
    char *epath = helper->buf + helper->targetDirLen;
    memcpy(epath, pEntry->fileName + helper->zipDirLen,
            pEntry->fileNameLen - helper->zipDirLen);
    epath += pEntry->fileNameLen - helper->zipDirLen;
    *epath = '\0';

    return helper->buf;
}

/*
 * Inflate all entries under zipDir to the directory specified by
 * targetDir, which must exist and be a writable directory.
 *
 * The immediate children of zipDir will become the immediate
 * children of targetDir; e.g., if the archive contains the entries
 *
 *     a/b/c/one
 *     a/b/c/two
 *     a/b/c/d/three
 *
 * and mzExtractRecursive(a, "a/b/c", "/tmp") is called, the resulting
 * files will be
 *
 *     /tmp/one
 *     /tmp/two
 *     /tmp/d/three
 *
 * Returns true on success, false on failure.
 */
bool mzExtractRecursive(const ZipArchive *pArchive,
                        const char *zipDir, const char *targetDir,
                        int flags, const struct utimbuf *timestamp,
                        void (*callback)(const char *fn, void *), void *cookie,
                        struct selabel_handle *sehnd)
{
    if (zipDir[0] == '/') {
        LOGE("mzExtractRecursive(): zipDir must be a relative path.\n");
        return false;
    }
    if (targetDir[0] != '/') {
        LOGE("mzExtractRecursive(): targetDir must be an absolute path.\n");
        return false;
    }

    unsigned int zipDirLen;
    char *zpath;

    zipDirLen = strlen(zipDir);
    zpath = (char *)malloc(zipDirLen + 2);
    if (zpath == NULL) {
        LOGE("Can't allocate %d bytes for zip path\n", zipDirLen + 2);
        return false;
    }
    /* If zipDir is empty, we'll extract the entire zip file.
     * Otherwise, canonicalize the path.
     */
    if (zipDirLen > 0) {
        /* Make sure there's (hopefully, exactly one) slash at the
         * end of the path.  This way we don't need to worry about
         * accidentally extracting "one/twothree" when a path like
         * "one/two" is specified.
         */
        memcpy(zpath, zipDir, zipDirLen);
        if (zpath[zipDirLen-1] != '/') {
            zpath[zipDirLen++] = '/';
        }
    }
    zpath[zipDirLen] = '\0';

    /* Set up the helper structure that we'll use to assemble paths.
     */
    MzPathHelper helper;
    helper.targetDir = targetDir;
    helper.targetDirLen = strlen(helper.targetDir);
    helper.zipDir = zpath;
    helper.zipDirLen = strlen(helper.zipDir);
    helper.buf = NULL;
    helper.bufLen = 0;

    /* Walk through the entries and extract anything whose path begins
     * with zpath.
//TODO: since the entries are sorted, binary search for the first match
//      and stop after the first non-match.
     */
    unsigned int i;
    bool seenMatch = false;
    int ok = true;
    int extractCount = 0;
    for (i = 0; i < pArchive->numEntries; i++) {
        ZipEntry *pEntry = pArchive->pEntries + i;
        if (pEntry->fileNameLen < zipDirLen) {
//TODO: look out for a single empty directory entry that matches zpath, but
//      missing the trailing slash.  Most zip files seem to include
//      the trailing slash, but I think it's legal to leave it off.
//      e.g., zpath "a/b/", entry "a/b", with no children of the entry.
            /* No chance of matching.
             */
#if SORT_ENTRIES
            if (seenMatch) {
                /* Since the entries are sorted, we can give up
                 * on the first mismatch after the first match.
                 */
                break;
            }
#endif
            continue;
        }
        /* If zpath is empty, this strncmp() will match everything,
         * which is what we want.
         */
        if (strncmp(pEntry->fileName, zpath, zipDirLen) != 0) {
#if SORT_ENTRIES
            if (seenMatch) {
                /* Since the entries are sorted, we can give up
                 * on the first mismatch after the first match.
                 */
                break;
            }
#endif
            continue;
        }
        /* This entry begins with zipDir, so we'll extract it.
         */
        seenMatch = true;

        /* Find the target location of the entry.
         */
        const char *targetFile = targetEntryPath(&helper, pEntry);
        if (targetFile == NULL) {
            LOGE("Can't assemble target path for \"%.*s\"\n",
                    pEntry->fileNameLen, pEntry->fileName);
            ok = false;
            break;
        }

        /* With DRY_RUN set, invoke the callback but don't do anything else.
         */
        if (flags & MZ_EXTRACT_DRY_RUN) {
            if (callback != NULL) callback(targetFile, cookie);
            continue;
        }

        /* Create the file or directory.
         */
#define UNZIP_DIRMODE 0755
#define UNZIP_FILEMODE 0644
        if (pEntry->fileName[pEntry->fileNameLen-1] == '/') {
            if (!(flags & MZ_EXTRACT_FILES_ONLY)) {
                int ret = dirCreateHierarchy(
                        targetFile, UNZIP_DIRMODE, timestamp, false, sehnd);
                if (ret != 0) {
                    LOGE("Can't create containing directory for \"%s\": %s\n",
                            targetFile, strerror(errno));
                    ok = false;
                    break;
                }
                LOGD("Extracted dir \"%s\"\n", targetFile);
            }
        } else {
            /* This is not a directory.  First, make sure that
             * the containing directory exists.
             */
            int ret = dirCreateHierarchy(
                    targetFile, UNZIP_DIRMODE, timestamp, true, sehnd);
            if (ret != 0) {
                LOGE("Can't create containing directory for \"%s\": %s\n",
                        targetFile, strerror(errno));
                ok = false;
                break;
            }

            /* With FILES_ONLY set, we need to ignore metadata entirely,
             * so treat symlinks as regular files.
             */
            if (!(flags & MZ_EXTRACT_FILES_ONLY) && mzIsZipEntrySymlink(pEntry)) {
                /* The entry is a symbolic link.
                 * The relative target of the symlink is in the
                 * data section of this entry.
                 */
                if (pEntry->uncompLen == 0) {
                    LOGE("Symlink entry \"%s\" has no target\n",
                            targetFile);
                    ok = false;
                    break;
                }
                char *linkTarget = malloc(pEntry->uncompLen + 1);
                if (linkTarget == NULL) {
                    ok = false;
                    break;
                }
                ok = mzReadZipEntry(pArchive, pEntry, linkTarget,
                        pEntry->uncompLen);
                if (!ok) {
                    LOGE("Can't read symlink target for \"%s\"\n",
                            targetFile);
                    free(linkTarget);
                    break;
                }
                linkTarget[pEntry->uncompLen] = '\0';

                /* Make the link.
                 */
                ret = symlink(linkTarget, targetFile);
                if (ret != 0) {
                    LOGE("Can't symlink \"%s\" to \"%s\": %s\n",
                            targetFile, linkTarget, strerror(errno));
                    free(linkTarget);
                    ok = false;
                    break;
                }
                LOGD("Extracted symlink \"%s\" -> \"%s\"\n",
                        targetFile, linkTarget);
                free(linkTarget);
            } else {
                /* The entry is a regular file.
                 * Open the target for writing.
                 */

                char *secontext = NULL;

                if (sehnd) {
                    selabel_lookup(sehnd, &secontext, targetFile, UNZIP_FILEMODE);
                    setfscreatecon(secontext);
                }

                int fd = creat(targetFile, UNZIP_FILEMODE);

                if (secontext) {
                    freecon(secontext);
                    setfscreatecon(NULL);
                }

                if (fd < 0) {
                    LOGE("Can't create target file \"%s\": %s\n",
                            targetFile, strerror(errno));
                    ok = false;
                    break;
                }

                bool ok = mzExtractZipEntryToFile(pArchive, pEntry, fd);
                close(fd);
                if (!ok) {
                    LOGE("Error extracting \"%s\"\n", targetFile);
                    ok = false;
                    break;
                }

                if (timestamp != NULL && utime(targetFile, timestamp)) {
                    LOGE("Error touching \"%s\"\n", targetFile);
                    ok = false;
                    break;
                }

                LOGV("Extracted file \"%s\"\n", targetFile);
                ++extractCount;
            }
        }

        if (callback != NULL) callback(targetFile, cookie);
    }

    LOGD("Extracted %d file(s)\n", extractCount);

    free(helper.buf);
    free(zpath);

    return ok;
}
