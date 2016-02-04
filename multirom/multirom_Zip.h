#ifndef _MULTIROM_MINZIP_ZIP
#define _MULTIROM_MINZIP_ZIP

//#include "inline_magic.h"

//#include <stdlib.h>
//#include <utime.h>

//#include "Hash.h"
//#include "SysUtil.h"

#ifdef __cplusplus
extern "C" {
#endif

//#include <selinux/selinux.h>
//#include <selinux/label.h>

int read_data(ZipArchive *zip, const ZipEntry *entry, char** ppData, int* pLength);

#ifdef __cplusplus
}
#endif

#endif /*_MULTIROM_MINZIP_ZIP*/
