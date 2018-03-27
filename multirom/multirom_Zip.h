#ifndef _MULTIROM_MINZIP_ZIP
#define _MULTIROM_MINZIP_ZIP

//#include "inline_magic.h"

//#include <stdlib.h>
//#include <utime.h>

//#include "Hash.h"
//#include "SysUtil.h"
#ifndef USE_MINZIP
#include <ziparchive/zip_archive.h>
#endif

//#include <selinux/selinux.h>
//#include <selinux/label.h>

#ifdef USE_MINZIP
int read_data(ZipArchive *zip, const ZipEntry *entry, char** ppData, size_t* pLength);
#else
int read_data(ZipArchiveHandle zip, ZipEntry entry, char** ppData, size_t* pLength);
#endif

#endif /*_MULTIROM_MINZIP_ZIP*/
