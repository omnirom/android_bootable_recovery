#ifndef _EXTRAFUNCTIONS_HEADER
#define _EXTRAFUNCTIONS_HEADER

#include "mincrypt/rsa.h"
#include "minzip/Zip.h"

int __system(const char *command);
FILE * __popen(const char *program, const char *type);
int __pclose(FILE *iop);

static long tmplog_offset = 0;

void update_tz_environment_variables();

void run_script(const char *str1, const char *str2, const char *str3, const char *str4, const char *str5, const char *str6, const char *str7, int request_confirm);

void check_and_run_script(const char* script_file, const char* display_name);
int check_backup_name(int show_error);
void twfinish_recovery(const char *send_intent);

#endif // _EXTRAFUNCTIONS_HEADER
