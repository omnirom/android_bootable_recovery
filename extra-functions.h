#ifndef _EXTRAFUNCTIONS_HEADER
#define _EXTRAFUNCTIONS_HEADER

#include "mincrypt/rsa.h"
#include "minzip/Zip.h"

int __system(const char *command);
FILE * __popen(const char *program, const char *type);
int __pclose(FILE *iop);

// Install Zip functions
int TWtry_update_binary(const char *path, ZipArchive *zip, int* wipe_cache);
static RSAPublicKey* TWload_keys(const char* filename, int* numKeys);
int TWverify_file(const char* path, const RSAPublicKey *pKeys, unsigned int numKeys);
int TWinstall_zip(const char* path, int* wipe_cache);

void wipe_dalvik_cache();
void wipe_battery_stats();
void wipe_rotate_data();

static long tmplog_offset = 0;

// Battery level
char* print_batt_cap();

void update_tz_environment_variables();

void fix_perms();

void run_script(const char *str1, const char *str2, const char *str3, const char *str4, const char *str5, const char *str6, const char *str7, int request_confirm);

void install_htc_dumlock(void);
void htc_dumlock_restore_original_boot(void);
void htc_dumlock_reflash_recovery_to_boot(void);

void check_and_run_script(const char* script_file, const char* display_name);
int check_backup_name(int show_error);
void twfinish_recovery(const char *send_intent);

#endif // _EXTRAFUNCTIONS_HEADER
