/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <bootloader_message/bootloader_message.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/system_properties.h>
#include <sys/klog.h>

#include <string>
#include <vector>

#include <android/log.h> /* Android Log Priority Tags */
#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <android-base/unique_fd.h>
#include <log/logger.h> /* Android Log packet format */
#include <private/android_logger.h> /* private pmsg functions */
#include <fs_mgr.h>


static struct fstab* read_fstab(std::string* err) {
  // The fstab path is always "/fstab.${ro.hardware}".
  std::string fstab_path = "/fstab.";
  char value[PROP_VALUE_MAX];
  if (__system_property_get("ro.hardware", value) == 0) {
    *err = "failed to get ro.hardware";
    return nullptr;
  }
  fstab_path += value;
  struct fstab* fstab = fs_mgr_read_fstab(fstab_path.c_str());
  if (fstab == nullptr) {
    *err = "failed to read " + fstab_path;
  }
  return fstab;
}

static std::string get_misc_blk_device(std::string* err) {
  struct fstab* fstab = read_fstab(err);
  if (fstab == nullptr) {
    return "";
  }
  fstab_rec* record = fs_mgr_get_entry_for_mount_point(fstab, "/misc");
  if (record == nullptr) {
    *err = "failed to find /misc partition";
    return "";
  }
  return record->blk_device;
}

// In recovery mode, recovery can get started and try to access the misc
// device before the kernel has actually created it.
static bool wait_for_device(const std::string& blk_device, std::string* err) {
  int tries = 0;
  int ret;
  err->clear();
  do {
    ++tries;
    struct stat buf;
    ret = stat(blk_device.c_str(), &buf);
    if (ret == -1) {
      *err += android::base::StringPrintf("failed to stat %s try %d: %s\n",
                                          blk_device.c_str(), tries, strerror(errno));
      sleep(1);
    }
  } while (ret && tries < 10);

  if (ret) {
    *err += android::base::StringPrintf("failed to stat %s\n", blk_device.c_str());
  }
  return ret == 0;
}

static bool read_misc_partition(void* p, size_t size, size_t offset, std::string* err) {
  std::string misc_blk_device = get_misc_blk_device(err);
  if (misc_blk_device.empty()) {
    return false;
  }
  if (!wait_for_device(misc_blk_device, err)) {
    return false;
  }
  android::base::unique_fd fd(open(misc_blk_device.c_str(), O_RDONLY));
  if (fd.get() == -1) {
    *err = android::base::StringPrintf("failed to open %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  if (lseek(fd.get(), static_cast<off_t>(offset), SEEK_SET) != static_cast<off_t>(offset)) {
    *err = android::base::StringPrintf("failed to lseek %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  if (!android::base::ReadFully(fd.get(), p, size)) {
    *err = android::base::StringPrintf("failed to read %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  return true;
}

static bool write_misc_partition(const void* p, size_t size, size_t offset, std::string* err) {
  std::string misc_blk_device = get_misc_blk_device(err);
  if (misc_blk_device.empty()) {
    return false;
  }
  android::base::unique_fd fd(open(misc_blk_device.c_str(), O_WRONLY | O_SYNC));
  if (fd.get() == -1) {
    *err = android::base::StringPrintf("failed to open %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  if (lseek(fd.get(), static_cast<off_t>(offset), SEEK_SET) != static_cast<off_t>(offset)) {
    *err = android::base::StringPrintf("failed to lseek %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  if (!android::base::WriteFully(fd.get(), p, size)) {
    *err = android::base::StringPrintf("failed to write %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }

  // TODO: O_SYNC and fsync duplicates each other?
  if (fsync(fd.get()) == -1) {
    *err = android::base::StringPrintf("failed to fsync %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    return false;
  }
  return true;
}

bool read_bootloader_message(bootloader_message* boot, std::string* err) {
  return read_misc_partition(boot, sizeof(*boot), BOOTLOADER_MESSAGE_OFFSET_IN_MISC, err);
}

bool write_bootloader_message(const bootloader_message& boot, std::string* err) {
  return write_misc_partition(&boot, sizeof(boot), BOOTLOADER_MESSAGE_OFFSET_IN_MISC, err);
}

bool clear_bootloader_message(std::string* err) {
  bootloader_message boot = {};
  return write_bootloader_message(boot, err);
}

bool write_bootloader_message(const std::vector<std::string>& options, std::string* err) {
  bootloader_message boot = {};
  strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
  strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
  for (const auto& s : options) {
    strlcat(boot.recovery, s.c_str(), sizeof(boot.recovery));
    if (s.back() != '\n') {
      strlcat(boot.recovery, "\n", sizeof(boot.recovery));
    }
  }
  return write_bootloader_message(boot, err);
}

bool read_wipe_package(std::string* package_data, size_t size, std::string* err) {
  package_data->resize(size);
  return read_misc_partition(&(*package_data)[0], size, WIPE_PACKAGE_OFFSET_IN_MISC, err);
}

bool write_wipe_package(const std::string& package_data, std::string* err) {
  return write_misc_partition(package_data.data(), package_data.size(),
                              WIPE_PACKAGE_OFFSET_IN_MISC, err);
}

extern "C" bool write_bootloader_message(const char* options) {
  std::string err;
  return write_bootloader_message({options}, &err);
}

// fake Volume struct that allows us to use the AOSP code easily
struct Volume
{
    char fs_type[8];
    char blk_device[256];
};

static Volume misc;

void set_misc_device(const char* type, const char* name) {
    strlcpy(misc.fs_type, type, sizeof(misc.fs_type));
    if (strlen(name) >= sizeof(misc.blk_device)) {
        ALOGE("New device name of '%s' is too large for bootloader.cpp\n", name);
    } else {
        strcpy(misc.blk_device, name);
    }
}

static const char *COMMAND_FILE = "/cache/recovery/command";
static const int MAX_ARG_LENGTH = 4096;
static const int MAX_ARGS = 100;

// command line args come from, in decreasing precedence:
//   - the actual command line
//   - the bootloader control block (one per line, after "recovery")
//   - the contents of COMMAND_FILE (one per line)
void
get_args(int *argc, char ***argv) {
    bootloader_message boot = {};
    std::string err;
    if (!read_bootloader_message(&boot, &err)) {
		ALOGE("%s\n", err.c_str());
		// If fails, leave a zeroed bootloader_message.
		memset(&boot, 0, sizeof(boot));
	}

    if (boot.command[0] != 0 && boot.command[0] != 255) {
        ALOGI("Boot command: %.*s\n", (int)sizeof(boot.command), boot.command);
    }

    if (boot.status[0] != 0 && boot.status[0] != 255) {
        ALOGI("Boot status: %.*s\n", (int)sizeof(boot.status), boot.status);
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
            ALOGI("Got arguments from boot message\n");
        } else if (boot.recovery[0] != 0 && boot.recovery[0] != 255) {
            ALOGE("Bad boot message\n\"%.20s\"\n", boot.recovery);
        }
    }

    // --- if that doesn't work, try the command file
    if (*argc <= 1) {
        FILE *fp = fopen(COMMAND_FILE, "r");
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

            fflush(fp);
            if (ferror(fp)) ALOGE("Error in %s\n(%s)\n", COMMAND_FILE, strerror(errno));
            fclose(fp);
            ALOGI("Got arguments from %s\n", COMMAND_FILE);
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
		ALOGE("%s\n", err.c_str());
	}
}
