#ifndef _EXTRAFUNCTIONS_HEADER
#define _EXTRAFUNCTIONS_HEADER

int __system(const char *command);
FILE * __popen(const char *program, const char *type);
int __pclose(FILE *iop);

// Device ID variable / function
extern char device_id[64];
void get_device_id();
static char* copy_sideloaded_package(const char* original_path);
int install_zip_package(const char* zip_path_filename);

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
