//#include "safe_iop.h"
//#include "zlib.h"

//#include <errno.h>
//#include <fcntl.h>
//#include <limits.h>
//#include <stdint.h>     // for uintptr_t
#include <stdlib.h>
//#include <sys/stat.h>   // for S_ISLNK()
//#include <unistd.h>

#define LOG_TAG "minzip_mrom"
#include "../minzip/Zip.h"
#include "multirom_Zip.h"
//#include "../minzip/Bits.h"
#include "../minzip/Log.h"
//#include "../minzip/DirUtil.h"

#undef NDEBUG   // do this after including Log.h
//#include <assert.h>

int read_data(ZipArchive *zip, const ZipEntry *entry,char** ppData, int* pLength)
{
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
