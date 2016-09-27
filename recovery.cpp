/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/fs.h>
#include <linux/input.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/klog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <vector>

#include <adb.h>
#include <android/log.h> /* Android Log Priority Tags */
#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <bootloader_message/bootloader_message.h>
#include <cutils/android_reboot.h>
#include <cutils/properties.h>
#include <log/logger.h> /* Android Log packet format */
#include <private/android_logger.h> /* private pmsg functions */

#include <healthd/BatteryMonitor.h>

#include "adb_install.h"
#include "common.h"
#include "device.h"
#include "error_code.h"
#include "fuse_sdcard_provider.h"
#include "fuse_sideload.h"
#include "install.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "minzip/Zip.h"
#include "roots.h"
#include "ui.h"
#include "unique_fd.h"
#include "screen_ui.h"

struct selabel_handle *sehandle;

static const struct option OPTIONS[] = {
  { "send_intent", required_argument, NULL, 'i' },
  { "update_package", required_argument, NULL, 'u' },
  { "retry_count", required_argument, NULL, 'n' },
  { "wipe_data", no_argument, NULL, 'w' },
  { "wipe_cache", no_argument, NULL, 'c' },
  { "show_text", no_argument, NULL, 't' },
  { "sideload", no_argument, NULL, 's' },
  { "sideload_auto_reboot", no_argument, NULL, 'a' },
  { "just_exit", no_argument, NULL, 'x' },
  { "locale", required_argument, NULL, 'l' },
  { "stages", required_argument, NULL, 'g' },
  { "shutdown_after", no_argument, NULL, 'p' },
  { "reason", required_argument, NULL, 'r' },
  { "security", no_argument, NULL, 'e'},
  { "wipe_ab", no_argument, NULL, 0 },
  { "wipe_package_size", required_argument, NULL, 0 },
  { NULL, 0, NULL, 0 },
};

// More bootreasons can be found in "system/core/bootstat/bootstat.cpp".
static const std::vector<std::string> bootreason_blacklist {
  "kernel_panic",
  "Panic",
};

static const char *CACHE_LOG_DIR = "/cache/recovery";
static const char *COMMAND_FILE = "/cache/recovery/command";
static const char *INTENT_FILE = "/cache/recovery/intent";
static const char *LOG_FILE = "/cache/recovery/log";
static const char *LAST_INSTALL_FILE = "/cache/recovery/last_install";
static const char *LOCALE_FILE = "/cache/recovery/last_locale";
static const char *CONVERT_FBE_DIR = "/tmp/convert_fbe";
static const char *CONVERT_FBE_FILE = "/tmp/convert_fbe/convert_fbe";
static const char *CACHE_ROOT = "/cache";
static const char *DATA_ROOT = "/data";
static const char *SDCARD_ROOT = "/sdcard";
static const char *TEMPORARY_LOG_FILE = "/tmp/recovery.log";
static const char *TEMPORARY_INSTALL_FILE = "/tmp/last_install";
static const char *LAST_KMSG_FILE = "/cache/recovery/last_kmsg";
static const char *LAST_LOG_FILE = "/cache/recovery/last_log";
static const int KEEP_LOG_COUNT = 10;
// We will try to apply the update package 5 times at most in case of an I/O error.
static const int EIO_RETRY_COUNT = 4;
static const int BATTERY_READ_TIMEOUT_IN_SEC = 10;
// GmsCore enters recovery mode to install package when having enough battery
// percentage. Normally, the threshold is 40% without charger and 20% with charger.
// So we should check battery with a slightly lower limitation.
static const int BATTERY_OK_PERCENTAGE = 20;
static const int BATTERY_WITH_CHARGER_OK_PERCENTAGE = 15;
constexpr const char* RECOVERY_WIPE = "/etc/recovery.wipe";

RecoveryUI* ui = NULL;
static const char* locale = "en_US";
char* stage = NULL;
char* reason = NULL;
bool modified_flash = false;
static bool has_cache = false;

/*
 * The recovery tool communicates with the main system through /cache files.
 *   /cache/recovery/command - INPUT - command line for tool, one arg per line
 *   /cache/recovery/log - OUTPUT - combined log file from recovery run(s)
 *   /cache/recovery/intent - OUTPUT - intent that was passed in
 *
 * The arguments which may be supplied in the recovery.command file:
 *   --send_intent=anystring - write the text out to recovery.intent
 *   --update_package=path - verify install an OTA package file
 *   --wipe_data - erase user data (and cache), then reboot
 *   --wipe_cache - wipe cache (but not user data), then reboot
 *   --set_encrypted_filesystem=on|off - enables / diasables encrypted fs
 *   --just_exit - do nothing; exit and reboot
 *
 * After completing, we remove /cache/recovery/command and reboot.
 * Arguments may also be supplied in the bootloader control block (BCB).
 * These important scenarios must be safely restartable at any point:
 *
 * FACTORY RESET
 * 1. user selects "factory reset"
 * 2. main system writes "--wipe_data" to /cache/recovery/command
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--wipe_data"
 *    -- after this, rebooting will restart the erase --
 * 5. erase_volume() reformats /data
 * 6. erase_volume() reformats /cache
 * 7. finish_recovery() erases BCB
 *    -- after this, rebooting will restart the main system --
 * 8. main() calls reboot() to boot main system
 *
 * OTA INSTALL
 * 1. main system downloads OTA package to /cache/some-filename.zip
 * 2. main system writes "--update_package=/cache/some-filename.zip"
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--update_package=..."
 *    -- after this, rebooting will attempt to reinstall the update --
 * 5. install_package() attempts to install the update
 *    NOTE: the package install must itself be restartable from any point
 * 6. finish_recovery() erases BCB
 *    -- after this, rebooting will (try to) restart the main system --
 * 7. ** if install failed **
 *    7a. prompt_and_wait() shows an error icon and waits for the user
 *    7b; the user reboots (pulling the battery, etc) into the main system
 * 8. main() calls maybe_install_firmware_update()
 *    ** if the update contained radio/hboot firmware **:
 *    8a. m_i_f_u() writes BCB with "boot-recovery" and "--wipe_cache"
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8b. m_i_f_u() writes firmware image into raw cache partition
 *    8c. m_i_f_u() writes BCB with "update-radio/hboot" and "--wipe_cache"
 *        -- after this, rebooting will attempt to reinstall firmware --
 *    8d. bootloader tries to flash firmware
 *    8e. bootloader writes BCB with "boot-recovery" (keeping "--wipe_cache")
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8f. erase_volume() reformats /cache
 *    8g. finish_recovery() erases BCB
 *        -- after this, rebooting will (try to) restart the main system --
 * 9. main() calls reboot() to boot main system
 */

static const int MAX_ARG_LENGTH = 4096;
static const int MAX_ARGS = 100;

// open a given path, mounting partitions as necessary
FILE* fopen_path(const char *path, const char *mode) {
    if (ensure_path_mounted(path) != 0) {
        LOGE("Can't mount %s\n", path);
        return NULL;
    }

    // When writing, try to create the containing directory, if necessary.
    // Use generous permissions, the system (init.rc) will reset them.
    if (strchr("wa", mode[0])) dirCreateHierarchy(path, 0777, NULL, 1, sehandle);

    FILE *fp = fopen(path, mode);
    return fp;
}

// close a file, log an error if the error indicator is set
static void check_and_fclose(FILE *fp, const char *name) {
    fflush(fp);
    if (ferror(fp)) LOGE("Error in %s\n(%s)\n", name, strerror(errno));
    fclose(fp);
}

bool is_ro_debuggable() {
    char value[PROPERTY_VALUE_MAX+1];
    return (property_get("ro.debuggable", value, NULL) == 1 && value[0] == '1');
}

static void redirect_stdio(const char* filename) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        LOGE("pipe failed: %s\n", strerror(errno));

        // Fall back to traditional logging mode without timestamps.
        // If these fail, there's not really anywhere to complain...
        freopen(filename, "a", stdout); setbuf(stdout, NULL);
        freopen(filename, "a", stderr); setbuf(stderr, NULL);

        return;
    }

    pid_t pid = fork();
    if (pid == -1) {
        LOGE("fork failed: %s\n", strerror(errno));

        // Fall back to traditional logging mode without timestamps.
        // If these fail, there's not really anywhere to complain...
        freopen(filename, "a", stdout); setbuf(stdout, NULL);
        freopen(filename, "a", stderr); setbuf(stderr, NULL);

        return;
    }

    if (pid == 0) {
        /// Close the unused write end.
        close(pipefd[1]);

        auto start = std::chrono::steady_clock::now();

        // Child logger to actually write to the log file.
        FILE* log_fp = fopen(filename, "a");
        if (log_fp == nullptr) {
            LOGE("fopen \"%s\" failed: %s\n", filename, strerror(errno));
            close(pipefd[0]);
            _exit(1);
        }

        FILE* pipe_fp = fdopen(pipefd[0], "r");
        if (pipe_fp == nullptr) {
            LOGE("fdopen failed: %s\n", strerror(errno));
            check_and_fclose(log_fp, filename);
            close(pipefd[0]);
            _exit(1);
        }

        char* line = nullptr;
        size_t len = 0;
        while (getline(&line, &len, pipe_fp) != -1) {
            auto now = std::chrono::steady_clock::now();
            double duration = std::chrono::duration_cast<std::chrono::duration<double>>(
                    now - start).count();
            if (line[0] == '\n') {
                fprintf(log_fp, "[%12.6lf]\n", duration);
            } else {
                fprintf(log_fp, "[%12.6lf] %s", duration, line);
            }
            fflush(log_fp);
        }

        LOGE("getline failed: %s\n", strerror(errno));

        free(line);
        check_and_fclose(log_fp, filename);
        close(pipefd[0]);
        _exit(1);
    } else {
        // Redirect stdout/stderr to the logger process.
        // Close the unused read end.
        close(pipefd[0]);

        setbuf(stdout, nullptr);
        setbuf(stderr, nullptr);

        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            LOGE("dup2 stdout failed: %s\n", strerror(errno));
        }
        if (dup2(pipefd[1], STDERR_FILENO) == -1) {
            LOGE("dup2 stderr failed: %s\n", strerror(errno));
        }

        close(pipefd[1]);
    }
}

// command line args come from, in decreasing precedence:
//   - the actual command line
//   - the bootloader control block (one per line, after "recovery")
//   - the contents of COMMAND_FILE (one per line)
static void
get_args(int *argc, char ***argv) {
    bootloader_message boot = {};
    std::string err;
    if (!read_bootloader_message(&boot, &err)) {
        LOGE("%s\n", err.c_str());
        // If fails, leave a zeroed bootloader_message.
        memset(&boot, 0, sizeof(boot));
    }
    stage = strndup(boot.stage, sizeof(boot.stage));

    if (boot.command[0] != 0 && boot.command[0] != 255) {
        LOGI("Boot command: %.*s\n", (int)sizeof(boot.command), boot.command);
    }

    if (boot.status[0] != 0 && boot.status[0] != 255) {
        LOGI("Boot status: %.*s\n", (int)sizeof(boot.status), boot.status);
    }

    // --- if arguments weren't supplied, look in the bootloader control block
    if (*argc <= 1) {
        boot.recovery[sizeof(boot.recovery) - 1] = '\0';  // Ensure termination
        const char *arg = strtok(boot.recovery, "\n");
        if (arg != NULL && !strcmp(arg, "recovery")) {
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = strdup(arg);
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if ((arg = strtok(NULL, "\n")) == NULL) break;
                (*argv)[*argc] = strdup(arg);
            }
            LOGI("Got arguments from boot message\n");
        } else if (boot.recovery[0] != 0 && boot.recovery[0] != 255) {
            LOGE("Bad boot message\n\"%.20s\"\n", boot.recovery);
        }
    }

    // --- if that doesn't work, try the command file (if we have /cache).
    if (*argc <= 1 && has_cache) {
        FILE *fp = fopen_path(COMMAND_FILE, "r");
        if (fp != NULL) {
            char *token;
            char *argv0 = (*argv)[0];
            *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
            (*argv)[0] = argv0;  // use the same program name

            char buf[MAX_ARG_LENGTH];
            for (*argc = 1; *argc < MAX_ARGS; ++*argc) {
                if (!fgets(buf, sizeof(buf), fp)) break;
                token = strtok(buf, "\r\n");
                if (token != NULL) {
                    (*argv)[*argc] = strdup(token);  // Strip newline.
                } else {
                    --*argc;
                }
            }

            check_and_fclose(fp, COMMAND_FILE);
            LOGI("Got arguments from %s\n", COMMAND_FILE);
        }
    }

    // --> write the arguments we have back into the bootloader control block
    // always boot into recovery after this (until finish_recovery() is called)
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    int i;
    for (i = 1; i < *argc; ++i) {
        strlcat(boot.recovery, (*argv)[i], sizeof(boot.recovery));
        strlcat(boot.recovery, "\n", sizeof(boot.recovery));
    }
    if (!write_bootloader_message(boot, &err)) {
        LOGE("%s\n", err.c_str());
    }
}

static void
set_sdcard_update_bootloader_message() {
    bootloader_message boot = {};
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
    std::string err;
    if (!write_bootloader_message(boot, &err)) {
        LOGE("%s\n", err.c_str());
    }
}

// Read from kernel log into buffer and write out to file.
static void save_kernel_log(const char* destination) {
    int klog_buf_len = klogctl(KLOG_SIZE_BUFFER, 0, 0);
    if (klog_buf_len <= 0) {
        LOGE("Error getting klog size: %s\n", strerror(errno));
        return;
    }

    std::string buffer(klog_buf_len, 0);
    int n = klogctl(KLOG_READ_ALL, &buffer[0], klog_buf_len);
    if (n == -1) {
        LOGE("Error in reading klog: %s\n", strerror(errno));
        return;
    }
    buffer.resize(n);
    android::base::WriteStringToFile(buffer, destination);
}

// write content to the current pmsg session.
static ssize_t __pmsg_write(const char *filename, const char *buf, size_t len) {
    return __android_log_pmsg_file_write(LOG_ID_SYSTEM, ANDROID_LOG_INFO,
                                         filename, buf, len);
}

static void copy_log_file_to_pmsg(const char* source, const char* destination) {
    std::string content;
    android::base::ReadFileToString(source, &content);
    __pmsg_write(destination, content.c_str(), content.length());
}

// How much of the temp log we have copied to the copy in cache.
static long tmplog_offset = 0;

static void copy_log_file(const char* source, const char* destination, bool append) {
    FILE* dest_fp = fopen_path(destination, append ? "a" : "w");
    if (dest_fp == nullptr) {
        LOGE("Can't open %s\n", destination);
    } else {
        FILE* source_fp = fopen(source, "r");
        if (source_fp != nullptr) {
            if (append) {
                fseek(source_fp, tmplog_offset, SEEK_SET);  // Since last write
            }
            char buf[4096];
            size_t bytes;
            while ((bytes = fread(buf, 1, sizeof(buf), source_fp)) != 0) {
                fwrite(buf, 1, bytes, dest_fp);
            }
            if (append) {
                tmplog_offset = ftell(source_fp);
            }
            check_and_fclose(source_fp, source);
        }
        check_and_fclose(dest_fp, destination);
    }
}

// Rename last_log -> last_log.1 -> last_log.2 -> ... -> last_log.$max.
// Similarly rename last_kmsg -> last_kmsg.1 -> ... -> last_kmsg.$max.
// Overwrite any existing last_log.$max and last_kmsg.$max.
static void rotate_logs(int max) {
    // Logs should only be rotated once.
    static bool rotated = false;
    if (rotated) {
        return;
    }
    rotated = true;
    ensure_path_mounted(LAST_LOG_FILE);
    ensure_path_mounted(LAST_KMSG_FILE);

    for (int i = max-1; i >= 0; --i) {
        std::string old_log = android::base::StringPrintf("%s", LAST_LOG_FILE);
        if (i > 0) {
          old_log += "." + std::to_string(i);
        }
        std::string new_log = android::base::StringPrintf("%s.%d", LAST_LOG_FILE, i+1);
        // Ignore errors if old_log doesn't exist.
        rename(old_log.c_str(), new_log.c_str());

        std::string old_kmsg = android::base::StringPrintf("%s", LAST_KMSG_FILE);
        if (i > 0) {
          old_kmsg += "." + std::to_string(i);
        }
        std::string new_kmsg = android::base::StringPrintf("%s.%d", LAST_KMSG_FILE, i+1);
        rename(old_kmsg.c_str(), new_kmsg.c_str());
    }
}

static void copy_logs() {
    // We only rotate and record the log of the current session if there are
    // actual attempts to modify the flash, such as wipes, installs from BCB
    // or menu selections. This is to avoid unnecessary rotation (and
    // possible deletion) of log files, if it does not do anything loggable.
    if (!modified_flash) {
        return;
    }

    // Always write to pmsg, this allows the OTA logs to be caught in logcat -L
    copy_log_file_to_pmsg(TEMPORARY_LOG_FILE, LAST_LOG_FILE);
    copy_log_file_to_pmsg(TEMPORARY_INSTALL_FILE, LAST_INSTALL_FILE);

    // We can do nothing for now if there's no /cache partition.
    if (!has_cache) {
        return;
    }

    rotate_logs(KEEP_LOG_COUNT);

    // Copy logs to cache so the system can find out what happened.
    copy_log_file(TEMPORARY_LOG_FILE, LOG_FILE, true);
    copy_log_file(TEMPORARY_LOG_FILE, LAST_LOG_FILE, false);
    copy_log_file(TEMPORARY_INSTALL_FILE, LAST_INSTALL_FILE, false);
    save_kernel_log(LAST_KMSG_FILE);
    chmod(LOG_FILE, 0600);
    chown(LOG_FILE, 1000, 1000);   // system user
    chmod(LAST_KMSG_FILE, 0600);
    chown(LAST_KMSG_FILE, 1000, 1000);   // system user
    chmod(LAST_LOG_FILE, 0640);
    chmod(LAST_INSTALL_FILE, 0644);
    sync();
}

// clear the recovery command and prepare to boot a (hopefully working) system,
// copy our log file to cache as well (for the system to read), and
// record any intent we were asked to communicate back to the system.
// this function is idempotent: call it as many times as you like.
static void
finish_recovery(const char *send_intent) {
    // By this point, we're ready to return to the main system...
    if (send_intent != NULL && has_cache) {
        FILE *fp = fopen_path(INTENT_FILE, "w");
        if (fp == NULL) {
            LOGE("Can't open %s\n", INTENT_FILE);
        } else {
            fputs(send_intent, fp);
            check_and_fclose(fp, INTENT_FILE);
        }
    }

    // Save the locale to cache, so if recovery is next started up
    // without a --locale argument (eg, directly from the bootloader)
    // it will use the last-known locale.
    if (locale != NULL) {
        size_t len = strlen(locale);
        __pmsg_write(LOCALE_FILE, locale, len);
        if (has_cache) {
            LOGI("Saving locale \"%s\"\n", locale);
            FILE* fp = fopen_path(LOCALE_FILE, "w");
            fwrite(locale, 1, len, fp);
            fflush(fp);
            fsync(fileno(fp));
            check_and_fclose(fp, LOCALE_FILE);
        }
    }

    copy_logs();

    // Reset to normal system boot so recovery won't cycle indefinitely.
    bootloader_message boot = {};
    std::string err;
    if (!write_bootloader_message(boot, &err)) {
        LOGE("%s\n", err.c_str());
    }

    // Remove the command file, so recovery won't repeat indefinitely.
    if (has_cache) {
        if (ensure_path_mounted(COMMAND_FILE) != 0 || (unlink(COMMAND_FILE) && errno != ENOENT)) {
            LOGW("Can't unlink %s\n", COMMAND_FILE);
        }
        ensure_path_unmounted(CACHE_ROOT);
    }

    sync();  // For good measure.
}

typedef struct _saved_log_file {
    char* name;
    struct stat st;
    unsigned char* data;
    struct _saved_log_file* next;
} saved_log_file;

static bool erase_volume(const char* volume) {
    bool is_cache = (strcmp(volume, CACHE_ROOT) == 0);
    bool is_data = (strcmp(volume, DATA_ROOT) == 0);

    ui->SetBackground(RecoveryUI::ERASING);
    ui->SetProgressType(RecoveryUI::INDETERMINATE);

    saved_log_file* head = NULL;

    if (is_cache) {
        // If we're reformatting /cache, we load any past logs
        // (i.e. "/cache/recovery/last_*") and the current log
        // ("/cache/recovery/log") into memory, so we can restore them after
        // the reformat.

        ensure_path_mounted(volume);

        DIR* d;
        struct dirent* de;
        d = opendir(CACHE_LOG_DIR);
        if (d) {
            char path[PATH_MAX];
            strcpy(path, CACHE_LOG_DIR);
            strcat(path, "/");
            int path_len = strlen(path);
            while ((de = readdir(d)) != NULL) {
                if (strncmp(de->d_name, "last_", 5) == 0 || strcmp(de->d_name, "log") == 0) {
                    saved_log_file* p = (saved_log_file*) malloc(sizeof(saved_log_file));
                    strcpy(path+path_len, de->d_name);
                    p->name = strdup(path);
                    if (stat(path, &(p->st)) == 0) {
                        // truncate files to 512kb
                        if (p->st.st_size > (1 << 19)) {
                            p->st.st_size = 1 << 19;
                        }
                        p->data = (unsigned char*) malloc(p->st.st_size);
                        FILE* f = fopen(path, "rb");
                        fread(p->data, 1, p->st.st_size, f);
                        fclose(f);
                        p->next = head;
                        head = p;
                    } else {
                        free(p);
                    }
                }
            }
            closedir(d);
        } else {
            if (errno != ENOENT) {
                printf("opendir failed: %s\n", strerror(errno));
            }
        }
    }

    ui->Print("Formatting %s...\n", volume);

    ensure_path_unmounted(volume);

    int result;

    if (is_data && reason && strcmp(reason, "convert_fbe") == 0) {
        // Create convert_fbe breadcrumb file to signal to init
        // to convert to file based encryption, not full disk encryption
        if (mkdir(CONVERT_FBE_DIR, 0700) != 0) {
            ui->Print("Failed to make convert_fbe dir %s\n", strerror(errno));
            return true;
        }
        FILE* f = fopen(CONVERT_FBE_FILE, "wb");
        if (!f) {
            ui->Print("Failed to convert to file encryption %s\n", strerror(errno));
            return true;
        }
        fclose(f);
        result = format_volume(volume, CONVERT_FBE_DIR);
        remove(CONVERT_FBE_FILE);
        rmdir(CONVERT_FBE_DIR);
    } else {
        result = format_volume(volume);
    }

    if (is_cache) {
        while (head) {
            FILE* f = fopen_path(head->name, "wb");
            if (f) {
                fwrite(head->data, 1, head->st.st_size, f);
                fclose(f);
                chmod(head->name, head->st.st_mode);
                chown(head->name, head->st.st_uid, head->st.st_gid);
            }
            free(head->name);
            free(head->data);
            saved_log_file* temp = head->next;
            free(head);
            head = temp;
        }

        // Any part of the log we'd copied to cache is now gone.
        // Reset the pointer so we copy from the beginning of the temp
        // log.
        tmplog_offset = 0;
        copy_logs();
    }

    return (result == 0);
}

static int
get_menu_selection(const char* const * headers, const char* const * items,
                   int menu_only, int initial_selection, Device* device) {
    // throw away keys pressed previously, so user doesn't
    // accidentally trigger menu items.
    ui->FlushKeys();

    ui->StartMenu(headers, items, initial_selection);
    int selected = initial_selection;
    int chosen_item = -1;

    while (chosen_item < 0) {
        int key = ui->WaitKey();
        int visible = ui->IsTextVisible();

        if (key == -1) {   // ui_wait_key() timed out
            if (ui->WasTextEverVisible()) {
                continue;
            } else {
                LOGI("timed out waiting for key input; rebooting.\n");
                ui->EndMenu();
                return 0; // XXX fixme
            }
        }

        int action = device->HandleMenuKey(key, visible);

        if (action < 0) {
            switch (action) {
                case Device::kHighlightUp:
                    selected = ui->SelectMenu(--selected);
                    break;
                case Device::kHighlightDown:
                    selected = ui->SelectMenu(++selected);
                    break;
                case Device::kInvokeItem:
                    chosen_item = selected;
                    break;
                case Device::kNoAction:
                    break;
            }
        } else if (!menu_only) {
            chosen_item = action;
        }
    }

    ui->EndMenu();
    return chosen_item;
}

static int compare_string(const void* a, const void* b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

// Returns a malloc'd path, or NULL.
static char* browse_directory(const char* path, Device* device) {
    ensure_path_mounted(path);

    DIR* d = opendir(path);
    if (d == NULL) {
        LOGE("error opening %s: %s\n", path, strerror(errno));
        return NULL;
    }

    int d_size = 0;
    int d_alloc = 10;
    char** dirs = (char**)malloc(d_alloc * sizeof(char*));
    int z_size = 1;
    int z_alloc = 10;
    char** zips = (char**)malloc(z_alloc * sizeof(char*));
    zips[0] = strdup("../");

    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        int name_len = strlen(de->d_name);

        if (de->d_type == DT_DIR) {
            // skip "." and ".." entries
            if (name_len == 1 && de->d_name[0] == '.') continue;
            if (name_len == 2 && de->d_name[0] == '.' &&
                de->d_name[1] == '.') continue;

            if (d_size >= d_alloc) {
                d_alloc *= 2;
                dirs = (char**)realloc(dirs, d_alloc * sizeof(char*));
            }
            dirs[d_size] = (char*)malloc(name_len + 2);
            strcpy(dirs[d_size], de->d_name);
            dirs[d_size][name_len] = '/';
            dirs[d_size][name_len+1] = '\0';
            ++d_size;
        } else if (de->d_type == DT_REG &&
                   name_len >= 4 &&
                   strncasecmp(de->d_name + (name_len-4), ".zip", 4) == 0) {
            if (z_size >= z_alloc) {
                z_alloc *= 2;
                zips = (char**)realloc(zips, z_alloc * sizeof(char*));
            }
            zips[z_size++] = strdup(de->d_name);
        }
    }
    closedir(d);

    qsort(dirs, d_size, sizeof(char*), compare_string);
    qsort(zips, z_size, sizeof(char*), compare_string);

    // append dirs to the zips list
    if (d_size + z_size + 1 > z_alloc) {
        z_alloc = d_size + z_size + 1;
        zips = (char**)realloc(zips, z_alloc * sizeof(char*));
    }
    memcpy(zips + z_size, dirs, d_size * sizeof(char*));
    free(dirs);
    z_size += d_size;
    zips[z_size] = NULL;

    const char* headers[] = { "Choose a package to install:", path, NULL };

    char* result;
    int chosen_item = 0;
    while (true) {
        chosen_item = get_menu_selection(headers, zips, 1, chosen_item, device);

        char* item = zips[chosen_item];
        int item_len = strlen(item);
        if (chosen_item == 0) {          // item 0 is always "../"
            // go up but continue browsing (if the caller is update_directory)
            result = NULL;
            break;
        }

        char new_path[PATH_MAX];
        strlcpy(new_path, path, PATH_MAX);
        strlcat(new_path, "/", PATH_MAX);
        strlcat(new_path, item, PATH_MAX);

        if (item[item_len-1] == '/') {
            // recurse down into a subdirectory
            new_path[strlen(new_path)-1] = '\0';  // truncate the trailing '/'
            result = browse_directory(new_path, device);
            if (result) break;
        } else {
            // selected a zip file: return the malloc'd path to the caller.
            result = strdup(new_path);
            break;
        }
    }

    for (int i = 0; i < z_size; ++i) free(zips[i]);
    free(zips);

    return result;
}

static bool yes_no(Device* device, const char* question1, const char* question2) {
    const char* headers[] = { question1, question2, NULL };
    const char* items[] = { " No", " Yes", NULL };

    int chosen_item = get_menu_selection(headers, items, 1, 0, device);
    return (chosen_item == 1);
}

// Return true on success.
static bool wipe_data(int should_confirm, Device* device) {
    if (should_confirm && !yes_no(device, "Wipe all user data?", "  THIS CAN NOT BE UNDONE!")) {
        return false;
    }

    modified_flash = true;

    ui->Print("\n-- Wiping data...\n");
    bool success =
        device->PreWipeData() &&
        erase_volume("/data") &&
        (has_cache ? erase_volume("/cache") : true) &&
        device->PostWipeData();
    ui->Print("Data wipe %s.\n", success ? "complete" : "failed");
    return success;
}

// Return true on success.
static bool wipe_cache(bool should_confirm, Device* device) {
    if (!has_cache) {
        ui->Print("No /cache partition found.\n");
        return false;
    }

    if (should_confirm && !yes_no(device, "Wipe cache?", "  THIS CAN NOT BE UNDONE!")) {
        return false;
    }

    modified_flash = true;

    ui->Print("\n-- Wiping cache...\n");
    bool success = erase_volume("/cache");
    ui->Print("Cache wipe %s.\n", success ? "complete" : "failed");
    return success;
}

// Secure-wipe a given partition. It uses BLKSECDISCARD, if supported.
// Otherwise, it goes with BLKDISCARD (if device supports BLKDISCARDZEROES) or
// BLKZEROOUT.
static bool secure_wipe_partition(const std::string& partition) {
    unique_fd fd(TEMP_FAILURE_RETRY(open(partition.c_str(), O_WRONLY)));
    if (fd.get() == -1) {
        LOGE("failed to open \"%s\": %s\n", partition.c_str(), strerror(errno));
        return false;
    }

    uint64_t range[2] = {0, 0};
    if (ioctl(fd.get(), BLKGETSIZE64, &range[1]) == -1 || range[1] == 0) {
        LOGE("failed to get partition size: %s\n", strerror(errno));
        return false;
    }
    printf("Secure-wiping \"%s\" from %" PRIu64 " to %" PRIu64 ".\n",
           partition.c_str(), range[0], range[1]);

    printf("Trying BLKSECDISCARD...\t");
    if (ioctl(fd.get(), BLKSECDISCARD, &range) == -1) {
        printf("failed: %s\n", strerror(errno));

        // Use BLKDISCARD if it zeroes out blocks, otherwise use BLKZEROOUT.
        unsigned int zeroes;
        if (ioctl(fd.get(), BLKDISCARDZEROES, &zeroes) == 0 && zeroes != 0) {
            printf("Trying BLKDISCARD...\t");
            if (ioctl(fd.get(), BLKDISCARD, &range) == -1) {
                printf("failed: %s\n", strerror(errno));
                return false;
            }
        } else {
            printf("Trying BLKZEROOUT...\t");
            if (ioctl(fd.get(), BLKZEROOUT, &range) == -1) {
                printf("failed: %s\n", strerror(errno));
                return false;
            }
        }
    }

    printf("done\n");
    return true;
}

// Check if the wipe package matches expectation:
// 1. verify the package.
// 2. check metadata (ota-type, pre-device and serial number if having one).
static bool check_wipe_package(size_t wipe_package_size) {
    if (wipe_package_size == 0) {
        LOGE("wipe_package_size is zero.\n");
        return false;
    }
    std::string wipe_package;
    std::string err_str;
    if (!read_wipe_package(&wipe_package, wipe_package_size, &err_str)) {
        LOGE("Failed to read wipe package: %s\n", err_str.c_str());
        return false;
    }
    if (!verify_package(reinterpret_cast<const unsigned char*>(wipe_package.data()),
                        wipe_package.size())) {
        LOGE("Failed to verify package.\n");
        return false;
    }

    // Extract metadata
    ZipArchive zip;
    int err = mzOpenZipArchive(reinterpret_cast<unsigned char*>(&wipe_package[0]),
                               wipe_package.size(), &zip);
    if (err != 0) {
        LOGE("Can't open wipe package: %s\n", err != -1 ? strerror(err) : "bad");
        return false;
    }
    std::string metadata;
    if (!read_metadata_from_package(&zip, &metadata)) {
        mzCloseZipArchive(&zip);
        return false;
    }
    mzCloseZipArchive(&zip);

    // Check metadata
    std::vector<std::string> lines = android::base::Split(metadata, "\n");
    bool ota_type_matched = false;
    bool device_type_matched = false;
    bool has_serial_number = false;
    bool serial_number_matched = false;
    for (const auto& line : lines) {
        if (line == "ota-type=BRICK") {
            ota_type_matched = true;
        } else if (android::base::StartsWith(line, "pre-device=")) {
            std::string device_type = line.substr(strlen("pre-device="));
            char real_device_type[PROPERTY_VALUE_MAX];
            property_get("ro.build.product", real_device_type, "");
            device_type_matched = (device_type == real_device_type);
        } else if (android::base::StartsWith(line, "serialno=")) {
            std::string serial_no = line.substr(strlen("serialno="));
            char real_serial_no[PROPERTY_VALUE_MAX];
            property_get("ro.serialno", real_serial_no, "");
            has_serial_number = true;
            serial_number_matched = (serial_no == real_serial_no);
        }
    }
    return ota_type_matched && device_type_matched && (!has_serial_number || serial_number_matched);
}

// Wipe the current A/B device, with a secure wipe of all the partitions in
// RECOVERY_WIPE.
static bool wipe_ab_device(size_t wipe_package_size) {
    ui->SetBackground(RecoveryUI::ERASING);
    ui->SetProgressType(RecoveryUI::INDETERMINATE);

    if (!check_wipe_package(wipe_package_size)) {
        LOGE("Failed to verify wipe package\n");
        return false;
    }
    std::string partition_list;
    if (!android::base::ReadFileToString(RECOVERY_WIPE, &partition_list)) {
        LOGE("failed to read \"%s\".\n", RECOVERY_WIPE);
        return false;
    }

    std::vector<std::string> lines = android::base::Split(partition_list, "\n");
    for (const std::string& line : lines) {
        std::string partition = android::base::Trim(line);
        // Ignore '#' comment or empty lines.
        if (android::base::StartsWith(partition, "#") || partition.empty()) {
            continue;
        }

        // Proceed anyway even if it fails to wipe some partition.
        secure_wipe_partition(partition);
    }
    return true;
}

static void choose_recovery_file(Device* device) {
    // "Back" + KEEP_LOG_COUNT * 2 + terminating nullptr entry
    char* entries[1 + KEEP_LOG_COUNT * 2 + 1];
    memset(entries, 0, sizeof(entries));

    unsigned int n = 0;

    if (has_cache) {
        // Add LAST_LOG_FILE + LAST_LOG_FILE.x
        // Add LAST_KMSG_FILE + LAST_KMSG_FILE.x
        for (int i = 0; i < KEEP_LOG_COUNT; i++) {
            char* log_file;
            int ret;
            ret = (i == 0) ? asprintf(&log_file, "%s", LAST_LOG_FILE) :
                    asprintf(&log_file, "%s.%d", LAST_LOG_FILE, i);
            if (ret == -1) {
                // memory allocation failure - return early. Should never happen.
                return;
            }
            if ((ensure_path_mounted(log_file) != 0) || (access(log_file, R_OK) == -1)) {
                free(log_file);
            } else {
                entries[n++] = log_file;
            }

            char* kmsg_file;
            ret = (i == 0) ? asprintf(&kmsg_file, "%s", LAST_KMSG_FILE) :
                    asprintf(&kmsg_file, "%s.%d", LAST_KMSG_FILE, i);
            if (ret == -1) {
                // memory allocation failure - return early. Should never happen.
                return;
            }
            if ((ensure_path_mounted(kmsg_file) != 0) || (access(kmsg_file, R_OK) == -1)) {
                free(kmsg_file);
            } else {
                entries[n++] = kmsg_file;
            }
        }
    } else {
        // If cache partition is not found, view /tmp/recovery.log instead.
        ui->Print("No /cache partition found.\n");
        if (access(TEMPORARY_LOG_FILE, R_OK) == -1) {
            return;
        } else{
            entries[n++] = strdup(TEMPORARY_LOG_FILE);
        }
    }

    entries[n++] = strdup("Back");

    const char* headers[] = { "Select file to view", nullptr };

    while (true) {
        int chosen_item = get_menu_selection(headers, entries, 1, 0, device);
        if (strcmp(entries[chosen_item], "Back") == 0) break;

        ui->ShowFile(entries[chosen_item]);
    }

    for (size_t i = 0; i < (sizeof(entries) / sizeof(*entries)); i++) {
        free(entries[i]);
    }
}

static void run_graphics_test(Device* device) {
    // Switch to graphics screen.
    ui->ShowText(false);

    ui->SetProgressType(RecoveryUI::INDETERMINATE);
    ui->SetBackground(RecoveryUI::INSTALLING_UPDATE);
    sleep(1);

    ui->SetBackground(RecoveryUI::ERROR);
    sleep(1);

    ui->SetBackground(RecoveryUI::NO_COMMAND);
    sleep(1);

    ui->SetBackground(RecoveryUI::ERASING);
    sleep(1);

    ui->SetBackground(RecoveryUI::INSTALLING_UPDATE);

    ui->SetProgressType(RecoveryUI::DETERMINATE);
    ui->ShowProgress(1.0, 10.0);
    float fraction = 0.0;
    for (size_t i = 0; i < 100; ++i) {
      fraction += .01;
      ui->SetProgress(fraction);
      usleep(100000);
    }

    ui->ShowText(true);
}

// How long (in seconds) we wait for the fuse-provided package file to
// appear, before timing out.
#define SDCARD_INSTALL_TIMEOUT 10

static int apply_from_sdcard(Device* device, bool* wipe_cache) {
    modified_flash = true;

    if (ensure_path_mounted(SDCARD_ROOT) != 0) {
        ui->Print("\n-- Couldn't mount %s.\n", SDCARD_ROOT);
        return INSTALL_ERROR;
    }

    char* path = browse_directory(SDCARD_ROOT, device);
    if (path == NULL) {
        ui->Print("\n-- No package file selected.\n");
        ensure_path_unmounted(SDCARD_ROOT);
        return INSTALL_ERROR;
    }

    ui->Print("\n-- Install %s ...\n", path);
    set_sdcard_update_bootloader_message();

    // We used to use fuse in a thread as opposed to a process. Since accessing
    // through fuse involves going from kernel to userspace to kernel, it leads
    // to deadlock when a page fault occurs. (Bug: 26313124)
    pid_t child;
    if ((child = fork()) == 0) {
        bool status = start_sdcard_fuse(path);

        _exit(status ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    // FUSE_SIDELOAD_HOST_PATHNAME will start to exist once the fuse in child
    // process is ready.
    int result = INSTALL_ERROR;
    int status;
    bool waited = false;
    for (int i = 0; i < SDCARD_INSTALL_TIMEOUT; ++i) {
        if (waitpid(child, &status, WNOHANG) == -1) {
            result = INSTALL_ERROR;
            waited = true;
            break;
        }

        struct stat sb;
        if (stat(FUSE_SIDELOAD_HOST_PATHNAME, &sb) == -1) {
            if (errno == ENOENT && i < SDCARD_INSTALL_TIMEOUT-1) {
                sleep(1);
                continue;
            } else {
                LOGE("Timed out waiting for the fuse-provided package.\n");
                result = INSTALL_ERROR;
                kill(child, SIGKILL);
                break;
            }
        }

        result = install_package(FUSE_SIDELOAD_HOST_PATHNAME, wipe_cache,
                                 TEMPORARY_INSTALL_FILE, false, 0/*retry_count*/);
        break;
    }

    if (!waited) {
        // Calling stat() on this magic filename signals the fuse
        // filesystem to shut down.
        struct stat sb;
        stat(FUSE_SIDELOAD_HOST_EXIT_PATHNAME, &sb);

        waitpid(child, &status, 0);
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        LOGE("Error exit from the fuse process: %d\n", WEXITSTATUS(status));
    }

    ensure_path_unmounted(SDCARD_ROOT);
    return result;
}

// Return REBOOT, SHUTDOWN, or REBOOT_BOOTLOADER.  Returning NO_ACTION
// means to take the default, which is to reboot or shutdown depending
// on if the --shutdown_after flag was passed to recovery.
static Device::BuiltinAction
prompt_and_wait(Device* device, int status) {
    for (;;) {
        finish_recovery(NULL);
        switch (status) {
            case INSTALL_SUCCESS:
            case INSTALL_NONE:
                ui->SetBackground(RecoveryUI::NO_COMMAND);
                break;

            case INSTALL_ERROR:
            case INSTALL_CORRUPT:
                ui->SetBackground(RecoveryUI::ERROR);
                break;
        }
        ui->SetProgressType(RecoveryUI::EMPTY);

        int chosen_item = get_menu_selection(nullptr, device->GetMenuItems(), 0, 0, device);

        // device-specific code may take some action here.  It may
        // return one of the core actions handled in the switch
        // statement below.
        Device::BuiltinAction chosen_action = device->InvokeMenuItem(chosen_item);

        bool should_wipe_cache = false;
        switch (chosen_action) {
            case Device::NO_ACTION:
                break;

            case Device::REBOOT:
            case Device::SHUTDOWN:
            case Device::REBOOT_BOOTLOADER:
                return chosen_action;

            case Device::WIPE_DATA:
                wipe_data(ui->IsTextVisible(), device);
                if (!ui->IsTextVisible()) return Device::NO_ACTION;
                break;

            case Device::WIPE_CACHE:
                wipe_cache(ui->IsTextVisible(), device);
                if (!ui->IsTextVisible()) return Device::NO_ACTION;
                break;

            case Device::APPLY_ADB_SIDELOAD:
            case Device::APPLY_SDCARD:
                {
                    bool adb = (chosen_action == Device::APPLY_ADB_SIDELOAD);
                    if (adb) {
                        status = apply_from_adb(ui, &should_wipe_cache, TEMPORARY_INSTALL_FILE);
                    } else {
                        status = apply_from_sdcard(device, &should_wipe_cache);
                    }

                    if (status == INSTALL_SUCCESS && should_wipe_cache) {
                        if (!wipe_cache(false, device)) {
                            status = INSTALL_ERROR;
                        }
                    }

                    if (status != INSTALL_SUCCESS) {
                        ui->SetBackground(RecoveryUI::ERROR);
                        ui->Print("Installation aborted.\n");
                        copy_logs();
                    } else if (!ui->IsTextVisible()) {
                        return Device::NO_ACTION;  // reboot if logs aren't visible
                    } else {
                        ui->Print("\nInstall from %s complete.\n", adb ? "ADB" : "SD card");
                    }
                }
                break;

            case Device::VIEW_RECOVERY_LOGS:
                choose_recovery_file(device);
                break;

            case Device::RUN_GRAPHICS_TEST:
                run_graphics_test(device);
                break;

            case Device::MOUNT_SYSTEM:
                char system_root_image[PROPERTY_VALUE_MAX];
                property_get("ro.build.system_root_image", system_root_image, "");

                // For a system image built with the root directory (i.e.
                // system_root_image == "true"), we mount it to /system_root, and symlink /system
                // to /system_root/system to make adb shell work (the symlink is created through
                // the build system).
                // Bug: 22855115
                if (strcmp(system_root_image, "true") == 0) {
                    if (ensure_path_mounted_at("/", "/system_root") != -1) {
                        ui->Print("Mounted /system.\n");
                    }
                } else {
                    if (ensure_path_mounted("/system") != -1) {
                        ui->Print("Mounted /system.\n");
                    }
                }

                break;
        }
    }
}

static void
print_property(const char *key, const char *name, void *cookie) {
    printf("%s=%s\n", key, name);
}

static void
load_locale_from_cache() {
    FILE* fp = fopen_path(LOCALE_FILE, "r");
    char buffer[80];
    if (fp != NULL) {
        fgets(buffer, sizeof(buffer), fp);
        int j = 0;
        unsigned int i;
        for (i = 0; i < sizeof(buffer) && buffer[i]; ++i) {
            if (!isspace(buffer[i])) {
                buffer[j++] = buffer[i];
            }
        }
        buffer[j] = 0;
        locale = strdup(buffer);
        check_and_fclose(fp, LOCALE_FILE);
    }
}

static RecoveryUI* gCurrentUI = NULL;

void
ui_print(const char* format, ...) {
    char buffer[256];

    va_list ap;
    va_start(ap, format);
    vsnprintf(buffer, sizeof(buffer), format, ap);
    va_end(ap);

    if (gCurrentUI != NULL) {
        gCurrentUI->Print("%s", buffer);
    } else {
        fputs(buffer, stdout);
    }
}

static bool is_battery_ok() {
    struct healthd_config healthd_config = {
            .batteryStatusPath = android::String8(android::String8::kEmptyString),
            .batteryHealthPath = android::String8(android::String8::kEmptyString),
            .batteryPresentPath = android::String8(android::String8::kEmptyString),
            .batteryCapacityPath = android::String8(android::String8::kEmptyString),
            .batteryVoltagePath = android::String8(android::String8::kEmptyString),
            .batteryTemperaturePath = android::String8(android::String8::kEmptyString),
            .batteryTechnologyPath = android::String8(android::String8::kEmptyString),
            .batteryCurrentNowPath = android::String8(android::String8::kEmptyString),
            .batteryCurrentAvgPath = android::String8(android::String8::kEmptyString),
            .batteryChargeCounterPath = android::String8(android::String8::kEmptyString),
            .batteryFullChargePath = android::String8(android::String8::kEmptyString),
            .batteryCycleCountPath = android::String8(android::String8::kEmptyString),
            .energyCounter = NULL,
            .boot_min_cap = 0,
            .screen_on = NULL
    };
    healthd_board_init(&healthd_config);

    android::BatteryMonitor monitor;
    monitor.init(&healthd_config);

    int wait_second = 0;
    while (true) {
        int charge_status = monitor.getChargeStatus();
        // Treat unknown status as charged.
        bool charged = (charge_status != android::BATTERY_STATUS_DISCHARGING &&
                        charge_status != android::BATTERY_STATUS_NOT_CHARGING);
        android::BatteryProperty capacity;
        android::status_t status = monitor.getProperty(android::BATTERY_PROP_CAPACITY, &capacity);
        ui_print("charge_status %d, charged %d, status %d, capacity %lld\n", charge_status,
                 charged, status, capacity.valueInt64);
        // At startup, the battery drivers in devices like N5X/N6P take some time to load
        // the battery profile. Before the load finishes, it reports value 50 as a fake
        // capacity. BATTERY_READ_TIMEOUT_IN_SEC is set that the battery drivers are expected
        // to finish loading the battery profile earlier than 10 seconds after kernel startup.
        if (status == 0 && capacity.valueInt64 == 50) {
            if (wait_second < BATTERY_READ_TIMEOUT_IN_SEC) {
                sleep(1);
                wait_second++;
                continue;
            }
        }
        // If we can't read battery percentage, it may be a device without battery. In this
        // situation, use 100 as a fake battery percentage.
        if (status != 0) {
            capacity.valueInt64 = 100;
        }
        return (charged && capacity.valueInt64 >= BATTERY_WITH_CHARGER_OK_PERCENTAGE) ||
                (!charged && capacity.valueInt64 >= BATTERY_OK_PERCENTAGE);
    }
}

static void set_retry_bootloader_message(int retry_count, int argc, char** argv) {
    bootloader_message boot = {};
    strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
    strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));

    for (int i = 1; i < argc; ++i) {
        if (strstr(argv[i], "retry_count") == nullptr) {
            strlcat(boot.recovery, argv[i], sizeof(boot.recovery));
            strlcat(boot.recovery, "\n", sizeof(boot.recovery));
        }
    }

    // Initialize counter to 1 if it's not in BCB, otherwise increment it by 1.
    if (retry_count == 0) {
        strlcat(boot.recovery, "--retry_count=1\n", sizeof(boot.recovery));
    } else {
        char buffer[20];
        snprintf(buffer, sizeof(buffer), "--retry_count=%d\n", retry_count+1);
        strlcat(boot.recovery, buffer, sizeof(boot.recovery));
    }
    std::string err;
    if (!write_bootloader_message(boot, &err)) {
        LOGE("%s\n", err.c_str());
    }
}

static bool bootreason_in_blacklist() {
    char bootreason[PROPERTY_VALUE_MAX];
    if (property_get("ro.boot.bootreason", bootreason, nullptr) > 0) {
        for (const auto& str : bootreason_blacklist) {
            if (strcasecmp(str.c_str(), bootreason) == 0) {
                return true;
            }
        }
    }
    return false;
}

static void log_failure_code(ErrorCode code, const char *update_package) {
    std::vector<std::string> log_buffer = {
        update_package,
        "0",  // install result
        "error: " + std::to_string(code),
    };
    std::string log_content = android::base::Join(log_buffer, "\n");
    if (!android::base::WriteStringToFile(log_content, TEMPORARY_INSTALL_FILE)) {
        LOGE("failed to write %s: %s\n", TEMPORARY_INSTALL_FILE, strerror(errno));
    }

    // Also write the info into last_log.
    LOGI("%s\n", log_content.c_str());
}

static ssize_t logbasename(
        log_id_t /* logId */,
        char /* prio */,
        const char *filename,
        const char * /* buf */, size_t len,
        void *arg) {
    if (strstr(LAST_KMSG_FILE, filename) ||
            strstr(LAST_LOG_FILE, filename)) {
        bool *doRotate = reinterpret_cast<bool *>(arg);
        *doRotate = true;
    }
    return len;
}

static ssize_t logrotate(
        log_id_t logId,
        char prio,
        const char *filename,
        const char *buf, size_t len,
        void *arg) {
    bool *doRotate = reinterpret_cast<bool *>(arg);
    if (!*doRotate) {
        return __android_log_pmsg_file_write(logId, prio, filename, buf, len);
    }

    std::string name(filename);
    size_t dot = name.find_last_of(".");
    std::string sub = name.substr(0, dot);

    if (!strstr(LAST_KMSG_FILE, sub.c_str()) &&
                !strstr(LAST_LOG_FILE, sub.c_str())) {
        return __android_log_pmsg_file_write(logId, prio, filename, buf, len);
    }

    // filename rotation
    if (dot == std::string::npos) {
        name += ".1";
    } else {
        std::string number = name.substr(dot + 1);
        if (!isdigit(number.data()[0])) {
            name += ".1";
        } else {
            unsigned long long i = std::stoull(number);
            name = sub + "." + std::to_string(i + 1);
        }
    }

    return __android_log_pmsg_file_write(logId, prio, name.c_str(), buf, len);
}

int main(int argc, char **argv) {
    // Take last pmsg contents and rewrite it to the current pmsg session.
    static const char filter[] = "recovery/";
    // Do we need to rotate?
    bool doRotate = false;
    __android_log_pmsg_file_read(
        LOG_ID_SYSTEM, ANDROID_LOG_INFO, filter,
        logbasename, &doRotate);
    // Take action to refresh pmsg contents
    __android_log_pmsg_file_read(
        LOG_ID_SYSTEM, ANDROID_LOG_INFO, filter,
        logrotate, &doRotate);

    // If this binary is started with the single argument "--adbd",
    // instead of being the normal recovery binary, it turns into kind
    // of a stripped-down version of adbd that only supports the
    // 'sideload' command.  Note this must be a real argument, not
    // anything in the command file or bootloader control block; the
    // only way recovery should be run with this argument is when it
    // starts a copy of itself from the apply_from_adb() function.
    if (argc == 2 && strcmp(argv[1], "--adbd") == 0) {
        adb_server_main(0, DEFAULT_ADB_PORT, -1);
        return 0;
    }

    time_t start = time(NULL);

    // redirect_stdio should be called only in non-sideload mode. Otherwise
    // we may have two logger instances with different timestamps.
    redirect_stdio(TEMPORARY_LOG_FILE);

    printf("Starting recovery (pid %d) on %s", getpid(), ctime(&start));

    load_volume_table();
    has_cache = volume_for_path(CACHE_ROOT) != nullptr;

    get_args(&argc, &argv);

    const char *send_intent = NULL;
    const char *update_package = NULL;
    bool should_wipe_data = false;
    bool should_wipe_cache = false;
    bool should_wipe_ab = false;
    size_t wipe_package_size = 0;
    bool show_text = false;
    bool sideload = false;
    bool sideload_auto_reboot = false;
    bool just_exit = false;
    bool shutdown_after = false;
    int retry_count = 0;
    bool security_update = false;

    int arg;
    int option_index;
    while ((arg = getopt_long(argc, argv, "", OPTIONS, &option_index)) != -1) {
        switch (arg) {
        case 'i': send_intent = optarg; break;
        case 'n': android::base::ParseInt(optarg, &retry_count, 0); break;
        case 'u': update_package = optarg; break;
        case 'w': should_wipe_data = true; break;
        case 'c': should_wipe_cache = true; break;
        case 't': show_text = true; break;
        case 's': sideload = true; break;
        case 'a': sideload = true; sideload_auto_reboot = true; break;
        case 'x': just_exit = true; break;
        case 'l': locale = optarg; break;
        case 'g': {
            if (stage == NULL || *stage == '\0') {
                char buffer[20] = "1/";
                strncat(buffer, optarg, sizeof(buffer)-3);
                stage = strdup(buffer);
            }
            break;
        }
        case 'p': shutdown_after = true; break;
        case 'r': reason = optarg; break;
        case 'e': security_update = true; break;
        case 0: {
            if (strcmp(OPTIONS[option_index].name, "wipe_ab") == 0) {
                should_wipe_ab = true;
                break;
            } else if (strcmp(OPTIONS[option_index].name, "wipe_package_size") == 0) {
                android::base::ParseUint(optarg, &wipe_package_size);
                break;
            }
            break;
        }
        case '?':
            LOGE("Invalid command argument\n");
            continue;
        }
    }

    if (locale == nullptr && has_cache) {
        load_locale_from_cache();
    }
    printf("locale is [%s]\n", locale);
    printf("stage is [%s]\n", stage);
    printf("reason is [%s]\n", reason);

    Device* device = make_device();
    ui = device->GetUI();
    gCurrentUI = ui;

    ui->SetLocale(locale);
    ui->Init();
    // Set background string to "installing security update" for security update,
    // otherwise set it to "installing system update".
    ui->SetSystemUpdateText(security_update);

    int st_cur, st_max;
    if (stage != NULL && sscanf(stage, "%d/%d", &st_cur, &st_max) == 2) {
        ui->SetStage(st_cur, st_max);
    }

    ui->SetBackground(RecoveryUI::NONE);
    if (show_text) ui->ShowText(true);

    struct selinux_opt seopts[] = {
      { SELABEL_OPT_PATH, "/file_contexts" }
    };

    sehandle = selabel_open(SELABEL_CTX_FILE, seopts, 1);

    if (!sehandle) {
        ui->Print("Warning: No file_contexts\n");
    }

    device->StartRecovery();

    printf("Command:");
    for (arg = 0; arg < argc; arg++) {
        printf(" \"%s\"", argv[arg]);
    }
    printf("\n");

    if (update_package) {
        // For backwards compatibility on the cache partition only, if
        // we're given an old 'root' path "CACHE:foo", change it to
        // "/cache/foo".
        if (strncmp(update_package, "CACHE:", 6) == 0) {
            int len = strlen(update_package) + 10;
            char* modified_path = (char*)malloc(len);
            if (modified_path) {
                strlcpy(modified_path, "/cache/", len);
                strlcat(modified_path, update_package+6, len);
                printf("(replacing path \"%s\" with \"%s\")\n",
                       update_package, modified_path);
                update_package = modified_path;
            }
            else
                printf("modified_path allocation failed\n");
        }
    }
    printf("\n");

    property_list(print_property, NULL);
    printf("\n");

    ui->Print("Supported API: %d\n", RECOVERY_API_VERSION);

    int status = INSTALL_SUCCESS;

    if (update_package != NULL) {
        // It's not entirely true that we will modify the flash. But we want
        // to log the update attempt since update_package is non-NULL.
        modified_flash = true;

        if (!is_battery_ok()) {
            ui->Print("battery capacity is not enough for installing package, needed is %d%%\n",
                      BATTERY_OK_PERCENTAGE);
            // Log the error code to last_install when installation skips due to
            // low battery.
            log_failure_code(kLowBattery, update_package);
            status = INSTALL_SKIPPED;
        } else if (bootreason_in_blacklist()) {
            // Skip update-on-reboot when bootreason is kernel_panic or similar
            ui->Print("bootreason is in the blacklist; skip OTA installation\n");
            log_failure_code(kBootreasonInBlacklist, update_package);
            status = INSTALL_SKIPPED;
        } else {
            status = install_package(update_package, &should_wipe_cache,
                                     TEMPORARY_INSTALL_FILE, true, retry_count);
            if (status == INSTALL_SUCCESS && should_wipe_cache) {
                wipe_cache(false, device);
            }
            if (status != INSTALL_SUCCESS) {
                ui->Print("Installation aborted.\n");
                // When I/O error happens, reboot and retry installation EIO_RETRY_COUNT
                // times before we abandon this OTA update.
                if (status == INSTALL_RETRY && retry_count < EIO_RETRY_COUNT) {
                    copy_logs();
                    set_retry_bootloader_message(retry_count, argc, argv);
                    // Print retry count on screen.
                    ui->Print("Retry attempt %d\n", retry_count);

                    // Reboot and retry the update
                    int ret = property_set(ANDROID_RB_PROPERTY, "reboot,recovery");
                    if (ret < 0) {
                        ui->Print("Reboot failed\n");
                    } else {
                        while (true) {
                            pause();
                        }
                    }
                }
                // If this is an eng or userdebug build, then automatically
                // turn the text display on if the script fails so the error
                // message is visible.
                if (is_ro_debuggable()) {
                    ui->ShowText(true);
                }
            }
        }
    } else if (should_wipe_data) {
        if (!wipe_data(false, device)) {
            status = INSTALL_ERROR;
        }
    } else if (should_wipe_cache) {
        if (!wipe_cache(false, device)) {
            status = INSTALL_ERROR;
        }
    } else if (should_wipe_ab) {
        if (!wipe_ab_device(wipe_package_size)) {
            status = INSTALL_ERROR;
        }
    } else if (sideload) {
        // 'adb reboot sideload' acts the same as user presses key combinations
        // to enter the sideload mode. When 'sideload-auto-reboot' is used, text
        // display will NOT be turned on by default. And it will reboot after
        // sideload finishes even if there are errors. Unless one turns on the
        // text display during the installation. This is to enable automated
        // testing.
        if (!sideload_auto_reboot) {
            ui->ShowText(true);
        }
        status = apply_from_adb(ui, &should_wipe_cache, TEMPORARY_INSTALL_FILE);
        if (status == INSTALL_SUCCESS && should_wipe_cache) {
            if (!wipe_cache(false, device)) {
                status = INSTALL_ERROR;
            }
        }
        ui->Print("\nInstall from ADB complete (status: %d).\n", status);
        if (sideload_auto_reboot) {
            ui->Print("Rebooting automatically.\n");
        }
    } else if (!just_exit) {
        status = INSTALL_NONE;  // No command specified
        ui->SetBackground(RecoveryUI::NO_COMMAND);

        // http://b/17489952
        // If this is an eng or userdebug build, automatically turn on the
        // text display if no command is specified.
        if (is_ro_debuggable()) {
            ui->ShowText(true);
        }
    }

    if (!sideload_auto_reboot && (status == INSTALL_ERROR || status == INSTALL_CORRUPT)) {
        copy_logs();
        ui->SetBackground(RecoveryUI::ERROR);
    }

    Device::BuiltinAction after = shutdown_after ? Device::SHUTDOWN : Device::REBOOT;
    if ((status != INSTALL_SUCCESS && status != INSTALL_SKIPPED && !sideload_auto_reboot) ||
            ui->IsTextVisible()) {
        Device::BuiltinAction temp = prompt_and_wait(device, status);
        if (temp != Device::NO_ACTION) {
            after = temp;
        }
    }

    // Save logs and clean up before rebooting or shutting down.
    finish_recovery(send_intent);

    switch (after) {
        case Device::SHUTDOWN:
            ui->Print("Shutting down...\n");
            property_set(ANDROID_RB_PROPERTY, "shutdown,");
            break;

        case Device::REBOOT_BOOTLOADER:
            ui->Print("Rebooting to bootloader...\n");
            property_set(ANDROID_RB_PROPERTY, "reboot,bootloader");
            break;

        default:
            ui->Print("Rebooting...\n");
            property_set(ANDROID_RB_PROPERTY, "reboot,");
            break;
    }
    while (true) {
      pause();
    }
    // Should be unreachable.
    return EXIT_SUCCESS;
}
