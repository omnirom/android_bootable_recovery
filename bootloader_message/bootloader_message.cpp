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
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/system_properties.h>

#include <string>
#include <vector>

/*
#include <android-base/file.h>
#include <android-base/stringprintf.h>
#include <android-base/unique_fd.h>
*/
#ifndef EXCLUDE_FS_MGR
#include <fs_mgr.h>
#endif

static std::string misc_blkdev;

void set_misc_device(std::string name) {
    misc_blkdev = name;
}

#ifndef EXCLUDE_FS_MGR
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
#endif

static std::string get_misc_blk_device(std::string* err) {
#ifdef EXCLUDE_FS_MGR
  return misc_blkdev;
#else
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
#endif
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
      char buffer[2048];
      sprintf(buffer, "failed to stat %s try %d: %s\n",
                                          blk_device.c_str(), tries, strerror(errno));
      *err += buffer;
      /*
      *err += android::base::StringPrintf("failed to stat %s try %d: %s\n",
                                          blk_device.c_str(), tries, strerror(errno));
      */
      sleep(1);
    }
  } while (ret && tries < 10);

  if (ret) {
    *err += "failed to stat " + blk_device + "\n";
    /*
    *err += android::base::StringPrintf("failed to stat %s\n", blk_device.c_str());
    */
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
  int fd(open(misc_blk_device.c_str(), O_RDONLY));
  if (fd < 0) {
    *err = "failed to open " + misc_blk_device + ": ";
    *err += strerror(errno);
    /*
    *err = android::base::StringPrintf("failed to open %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    */
    return false;
  }
  if (lseek(fd, static_cast<off_t>(offset), SEEK_SET) != static_cast<off_t>(offset)) {
    *err = "failed to lseek " + misc_blk_device + ": ";
    *err += strerror(errno);
    close(fd);
    /*
    *err = android::base::StringPrintf("failed to lseek %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    */
    return false;
  }
  if (read(fd, p, size) != size) {
    *err = "failed to read " + misc_blk_device + ": ";
    *err += strerror(errno);
    close(fd);
    /*
    *err = android::base::StringPrintf("failed to read %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    */
    return false;
  }
  close(fd);
  return true;
}

static bool write_misc_partition(const void* p, size_t size, size_t offset, std::string* err) {
  std::string misc_blk_device = get_misc_blk_device(err);
  if (misc_blk_device.empty()) {
    *err = "no misc device set";
    return false;
  }
  int fd = (open(misc_blk_device.c_str(), O_WRONLY | O_SYNC));
  if (fd == -1) {
    *err = "failed to open " + misc_blk_device + ": ";
    *err += strerror(errno);
    /*
    *err = android::base::StringPrintf("failed to open %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    */
    return false;
  }
  if (lseek(fd, static_cast<off_t>(offset), SEEK_SET) != static_cast<off_t>(offset)) {
    *err = "failed to lseek " + misc_blk_device + ": ";
    *err += strerror(errno);
    close(fd);
    /*
    *err = android::base::StringPrintf("failed to lseek %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    */
    return false;
  }
  if (write(fd, p, size) != size) {
    *err = "failed to write " + misc_blk_device + ": ";
    *err += strerror(errno);
    close(fd);
    /*
    *err = android::base::StringPrintf("failed to write %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    */
    return false;
  }

  // TODO: O_SYNC and fsync duplicates each other?
  if (fsync(fd) == -1) {
    *err = "failed to fsync " + misc_blk_device + ": ";
    *err += strerror(errno);
    close(fd);
    /*
    *err = android::base::StringPrintf("failed to fsync %s: %s", misc_blk_device.c_str(),
                                       strerror(errno));
    */
    return false;
  }
  close(fd);
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
    if (s.substr(s.size() - 1) != "\n") {
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
  bootloader_message boot = {};
  memcpy(&boot, options, sizeof(boot));
  return write_bootloader_message(boot, &err);
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
        printf("%s\n", err.c_str());
        // If fails, leave a zeroed bootloader_message.
        memset(&boot, 0, sizeof(boot));
    }
    //stage = strndup(boot.stage, sizeof(boot.stage));

    if (boot.command[0] != 0 && boot.command[0] != 255) {
        printf("Boot command: %.*s\n", (int)sizeof(boot.command), boot.command);
    }

    if (boot.status[0] != 0 && boot.status[0] != 255) {
        printf("Boot status: %.*s\n", (int)sizeof(boot.status), boot.status);
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

// if the device does not have an own recovery key combo we just want to open TWRP after
// walking through the factory reset screen - without actually doing a factory reset
#ifdef IGNORE_MISC_WIPE_DATA
                if (!strcmp(arg, "--wipe_data")) {
                    (*argv)[*argc] = "";
                    *argc = *argc -1;
                    printf("Bootloader arg \"%s\" ignored because TWRP was compiled with TW_IGNORE_MISC_WIPE_DATA\n", arg);
                    continue;
                }
#endif
                (*argv)[*argc] = strdup(arg);
            }
            printf("Got arguments from boot message\n");
        } else if (boot.recovery[0] != 0 && boot.recovery[0] != 255) {
            printf("Bad boot message\n\"%.20s\"\n", boot.recovery);
        }
    }

    // --- if that doesn't work, try the command file (if we have /cache).
    if (*argc <= 1/* && has_cache*/) {
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

            fclose(fp);
            printf("Got arguments from %s\n", COMMAND_FILE);
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
        printf("%s\n", err.c_str());
    }
}
