#ifndef _EXTRAFUNCTIONS_HEADER
#define _EXTRAFUNCTIONS_HEADER

#include "mincrypt/rsa.h"
#include "minzip/Zip.h"

void run_script(const char *str1, const char *str2, const char *str3, const char *str4, const char *str5, const char *str6, const char *str7, int request_confirm);

int check_backup_name(int show_error);

#endif // _EXTRAFUNCTIONS_HEADER
