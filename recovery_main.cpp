/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/fs.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <bootloader_message/bootloader_message.h>
#include <cutils/sockets.h>
#include <fs_mgr/roots.h>
#include <private/android_logger.h> /* private pmsg functions */
#include <selinux/android.h>
#include <selinux/label.h>
#include <selinux/selinux.h>

#include "fastboot/fastboot.h"
#include "install/wipe_data.h"
#include "otautil/boot_state.h"
#include "otautil/paths.h"
#include "otautil/sysutil.h"
#include "recovery.h"
#include "recovery_ui/device.h"
#include "recovery_ui/stub_ui.h"
#include "recovery_ui/ui.h"
#include "recovery_utils/logging.h"
#include "recovery_utils/roots.h"

static constexpr const char* COMMAND_FILE = "/cache/recovery/command";
static constexpr const char* LOCALE_FILE = "/cache/recovery/last_locale";

static RecoveryUI* ui = nullptr;

static bool IsRoDebuggable() {
  return android::base::GetBoolProperty("ro.debuggable", false);
}

static bool IsDeviceUnlocked() {
  return "orange" == android::base::GetProperty("ro.boot.verifiedbootstate", "");
}

static void UiLogger(android::base::LogId /* id */, android::base::LogSeverity severity,
                     const char* /* tag */, const char* /* file */, unsigned int /* line */,
                     const char* message) {
  static constexpr char log_characters[] = "VDIWEF";
  if (severity >= android::base::ERROR && ui != nullptr) {
    ui->Print("E:%s\n", message);
  } else {
    fprintf(stdout, "%c:%s\n", log_characters[severity], message);
  }
}

// Parses the command line argument from various sources; and reads the stage field from BCB.
// command line args come from, in decreasing precedence:
//   - the actual command line
//   - the bootloader control block (one per line, after "recovery")
//   - the contents of COMMAND_FILE (one per line)
static std::vector<std::string> get_args(const int argc, char** const argv, std::string* stage) {
  CHECK_GT(argc, 0);

  bootloader_message boot = {};
  std::string err;
  if (!read_bootloader_message(&boot, &err)) {
    LOG(ERROR) << err;
    // If fails, leave a zeroed bootloader_message.
    boot = {};
  }
  if (stage) {
    *stage = std::string(boot.stage);
  }

  std::string boot_command;
  if (boot.command[0] != 0) {
    if (memchr(boot.command, '\0', sizeof(boot.command))) {
      boot_command = std::string(boot.command);
    } else {
      boot_command = std::string(boot.command, sizeof(boot.command));
    }
    LOG(INFO) << "Boot command: " << boot_command;
  }

  if (boot.status[0] != 0) {
    std::string boot_status = std::string(boot.status, sizeof(boot.status));
    LOG(INFO) << "Boot status: " << boot_status;
  }

  std::vector<std::string> args(argv, argv + argc);

  // --- if arguments weren't supplied, look in the bootloader control block
  if (args.size() == 1) {
    boot.recovery[sizeof(boot.recovery) - 1] = '\0';  // Ensure termination
    std::string boot_recovery(boot.recovery);
    std::vector<std::string> tokens = android::base::Split(boot_recovery, "\n");
    if (!tokens.empty() && tokens[0] == "recovery") {
      for (auto it = tokens.begin() + 1; it != tokens.end(); it++) {
        // Skip empty and '\0'-filled tokens.
        if (!it->empty() && (*it)[0] != '\0') args.push_back(std::move(*it));
      }
      LOG(INFO) << "Got " << args.size() << " arguments from boot message";
    } else if (boot.recovery[0] != 0) {
      LOG(ERROR) << "Bad boot message: \"" << boot_recovery << "\"";
    }
  }

  // --- if that doesn't work, try the command file (if we have /cache).
  if (args.size() == 1 && HasCache()) {
    std::string content;
    if (ensure_path_mounted(COMMAND_FILE) == 0 &&
        android::base::ReadFileToString(COMMAND_FILE, &content)) {
      std::vector<std::string> tokens = android::base::Split(content, "\n");
      // All the arguments in COMMAND_FILE are needed (unlike the BCB message,
      // COMMAND_FILE doesn't use filename as the first argument).
      for (auto it = tokens.begin(); it != tokens.end(); it++) {
        // Skip empty and '\0'-filled tokens.
        if (!it->empty() && (*it)[0] != '\0') args.push_back(std::move(*it));
      }
      LOG(INFO) << "Got " << args.size() << " arguments from " << COMMAND_FILE;
    }
  }

  // Write the arguments (excluding the filename in args[0]) back into the
  // bootloader control block. So the device will always boot into recovery to
  // finish the pending work, until FinishRecovery() is called.
  std::vector<std::string> options(args.cbegin() + 1, args.cend());
  if (!update_bootloader_message(options, &err)) {
    LOG(ERROR) << "Failed to set BCB message: " << err;
  }

  // Finally, if no arguments were specified, check whether we should boot
  // into fastboot or rescue mode.
  if (args.size() == 1 && boot_command == "boot-fastboot") {
    args.emplace_back("--fastboot");
  } else if (args.size() == 1 && boot_command == "boot-rescue") {
    args.emplace_back("--rescue");
  }

  return args;
}

static std::string load_locale_from_cache() {
  if (ensure_path_mounted(LOCALE_FILE) != 0) {
    LOG(ERROR) << "Can't mount " << LOCALE_FILE;
    return "";
  }

  std::string content;
  if (!android::base::ReadFileToString(LOCALE_FILE, &content)) {
    PLOG(ERROR) << "Can't read " << LOCALE_FILE;
    return "";
  }

  return android::base::Trim(content);
}

// Sets the usb config to 'state'.
static bool SetUsbConfig(const std::string& state) {
  android::base::SetProperty("sys.usb.config", state);
  return android::base::WaitForProperty("sys.usb.state", state);
}

static void ListenRecoverySocket(RecoveryUI* ui, std::atomic<Device::BuiltinAction>& action) {
  android::base::unique_fd sock_fd(android_get_control_socket("recovery"));
  if (sock_fd < 0) {
    PLOG(ERROR) << "Failed to open recovery socket";
    return;
  }
  listen(sock_fd, 4);

  while (true) {
    android::base::unique_fd connection_fd;
    connection_fd.reset(accept(sock_fd, nullptr, nullptr));
    if (connection_fd < 0) {
      PLOG(ERROR) << "Failed to accept socket connection";
      continue;
    }
    char msg;
    constexpr char kSwitchToFastboot = 'f';
    constexpr char kSwitchToRecovery = 'r';
    ssize_t ret = TEMP_FAILURE_RETRY(read(connection_fd, &msg, sizeof(msg)));
    if (ret != sizeof(msg)) {
      PLOG(ERROR) << "Couldn't read from socket";
      continue;
    }
    switch (msg) {
      case kSwitchToRecovery:
        action = Device::BuiltinAction::ENTER_RECOVERY;
        break;
      case kSwitchToFastboot:
        action = Device::BuiltinAction::ENTER_FASTBOOT;
        break;
      default:
        LOG(ERROR) << "Unrecognized char from socket " << msg;
        continue;
    }
    ui->InterruptKey();
  }
}

static void redirect_stdio(const char* filename) {
  android::base::unique_fd pipe_read, pipe_write;
  // Create a pipe that allows parent process sending logs over.
  if (!android::base::Pipe(&pipe_read, &pipe_write)) {
    PLOG(ERROR) << "Failed to create pipe for redirecting stdio";

    // Fall back to traditional logging mode without timestamps. If these fail, there's not really
    // anywhere to complain...
    freopen(filename, "a", stdout);
    setbuf(stdout, nullptr);
    freopen(filename, "a", stderr);
    setbuf(stderr, nullptr);

    return;
  }

  pid_t pid = fork();
  if (pid == -1) {
    PLOG(ERROR) << "Failed to fork for redirecting stdio";

    // Fall back to traditional logging mode without timestamps. If these fail, there's not really
    // anywhere to complain...
    freopen(filename, "a", stdout);
    setbuf(stdout, nullptr);
    freopen(filename, "a", stderr);
    setbuf(stderr, nullptr);

    return;
  }

  if (pid == 0) {
    // Child process reads the incoming logs and doesn't write to the pipe.
    pipe_write.reset();

    auto start = std::chrono::steady_clock::now();

    // Child logger to actually write to the log file.
    FILE* log_fp = fopen(filename, "ae");
    if (log_fp == nullptr) {
      PLOG(ERROR) << "fopen \"" << filename << "\" failed";
      _exit(EXIT_FAILURE);
    }

    FILE* pipe_fp = android::base::Fdopen(std::move(pipe_read), "r");
    if (pipe_fp == nullptr) {
      PLOG(ERROR) << "fdopen failed";
      check_and_fclose(log_fp, filename);
      _exit(EXIT_FAILURE);
    }

    char* line = nullptr;
    size_t len = 0;
    while (getline(&line, &len, pipe_fp) != -1) {
      auto now = std::chrono::steady_clock::now();
      double duration =
          std::chrono::duration_cast<std::chrono::duration<double>>(now - start).count();
      if (line[0] == '\n') {
        fprintf(log_fp, "[%12.6lf]\n", duration);
      } else {
        fprintf(log_fp, "[%12.6lf] %s", duration, line);
      }
      fflush(log_fp);
    }

    PLOG(ERROR) << "getline failed";

    fclose(pipe_fp);
    free(line);
    check_and_fclose(log_fp, filename);
    _exit(EXIT_FAILURE);
  } else {
    // Redirect stdout/stderr to the logger process. Close the unused read end.
    pipe_read.reset();

    setbuf(stdout, nullptr);
    setbuf(stderr, nullptr);

    if (dup2(pipe_write.get(), STDOUT_FILENO) == -1) {
      PLOG(ERROR) << "dup2 stdout failed";
    }
    if (dup2(pipe_write.get(), STDERR_FILENO) == -1) {
      PLOG(ERROR) << "dup2 stderr failed";
    }
  }
}

int main(int argc, char** argv) {
  // We don't have logcat yet under recovery; so we'll print error on screen and log to stdout
  // (which is redirected to recovery.log) as we used to do.
  android::base::InitLogging(argv, &UiLogger);

  // Take last pmsg contents and rewrite it to the current pmsg session.
  static constexpr const char filter[] = "recovery/";
  // Do we need to rotate?
  bool do_rotate = false;

  __android_log_pmsg_file_read(LOG_ID_SYSTEM, ANDROID_LOG_INFO, filter, logbasename, &do_rotate);
  // Take action to refresh pmsg contents
  __android_log_pmsg_file_read(LOG_ID_SYSTEM, ANDROID_LOG_INFO, filter, logrotate, &do_rotate);

  time_t start = time(nullptr);

  // redirect_stdio should be called only in non-sideload mode. Otherwise we may have two logger
  // instances with different timestamps.
  redirect_stdio(Paths::Get().temporary_log_file().c_str());

  load_volume_table();

  std::string stage;
  std::vector<std::string> args = get_args(argc, argv, &stage);
  auto args_to_parse = StringVectorToNullTerminatedArray(args);

  static constexpr struct option OPTIONS[] = {
    { "fastboot", no_argument, nullptr, 0 },
    { "locale", required_argument, nullptr, 0 },
    { "reason", required_argument, nullptr, 0 },
    { "show_text", no_argument, nullptr, 't' },
    { nullptr, 0, nullptr, 0 },
  };

  bool show_text = false;
  bool fastboot = false;
  std::string locale;
  std::string reason;

  // The code here is only interested in the options that signal the intent to start fastbootd or
  // recovery. Unrecognized options are likely meant for recovery, which will be processed later in
  // start_recovery(). Suppress the warnings for such -- even if some flags were indeed invalid, the
  // code in start_recovery() will capture and report them.
  opterr = 0;

  int arg;
  int option_index;
  while ((arg = getopt_long(args_to_parse.size() - 1, args_to_parse.data(), "", OPTIONS,
                            &option_index)) != -1) {
    switch (arg) {
      case 't':
        show_text = true;
        break;
      case 0: {
        std::string option = OPTIONS[option_index].name;
        if (option == "locale") {
          locale = optarg;
        } else if (option == "reason") {
          reason = optarg;
        } else if (option == "fastboot" &&
                   android::base::GetBoolProperty("ro.boot.dynamic_partitions", false)) {
          fastboot = true;
        }
        break;
      }
    }
  }
  optind = 1;
  opterr = 1;

  if (locale.empty()) {
    if (HasCache()) {
      locale = load_locale_from_cache();
    }

    if (locale.empty()) {
      locale = DEFAULT_LOCALE;
    }
  }

  static constexpr const char* kDefaultLibRecoveryUIExt = "librecovery_ui_ext.so";
  // Intentionally not calling dlclose(3) to avoid potential gotchas (e.g. `make_device` may have
  // handed out pointers to code or static [or thread-local] data and doesn't collect them all back
  // in on dlclose).
  void* librecovery_ui_ext = dlopen(kDefaultLibRecoveryUIExt, RTLD_NOW);

  using MakeDeviceType = decltype(&make_device);
  MakeDeviceType make_device_func = nullptr;
  if (librecovery_ui_ext == nullptr) {
    printf("Failed to dlopen %s: %s\n", kDefaultLibRecoveryUIExt, dlerror());
  } else {
    reinterpret_cast<void*&>(make_device_func) = dlsym(librecovery_ui_ext, "make_device");
    if (make_device_func == nullptr) {
      printf("Failed to dlsym make_device: %s\n", dlerror());
    }
  }

  Device* device;
  if (make_device_func == nullptr) {
    printf("Falling back to the default make_device() instead\n");
    device = make_device();
  } else {
    printf("Loading make_device from %s\n", kDefaultLibRecoveryUIExt);
    device = (*make_device_func)();
  }

  if (android::base::GetBoolProperty("ro.boot.quiescent", false)) {
    printf("Quiescent recovery mode.\n");
    device->ResetUI(new StubRecoveryUI());
  } else {
    if (!device->GetUI()->Init(locale)) {
      printf("Failed to initialize UI; using stub UI instead.\n");
      device->ResetUI(new StubRecoveryUI());
    }
  }

  BootState boot_state(reason, stage);  // recovery_main owns the state of boot.
  device->SetBootState(&boot_state);
  ui = device->GetUI();

  if (!HasCache()) {
    device->RemoveMenuItemForAction(Device::WIPE_CACHE);
  }

  if (!android::base::GetBoolProperty("ro.boot.dynamic_partitions", false)) {
    device->RemoveMenuItemForAction(Device::ENTER_FASTBOOT);
  }

  if (!IsRoDebuggable()) {
    device->RemoveMenuItemForAction(Device::ENTER_RESCUE);
  }

  ui->SetBackground(RecoveryUI::NONE);
  if (show_text) ui->ShowText(true);

  LOG(INFO) << "Starting recovery (pid " << getpid() << ") on " << ctime(&start);
  LOG(INFO) << "locale is [" << locale << "]";

  auto sehandle = selinux_android_file_context_handle();
  selinux_android_set_sehandle(sehandle);
  if (!sehandle) {
    ui->Print("Warning: No file_contexts\n");
  }

  SetLoggingSehandle(sehandle);

  std::atomic<Device::BuiltinAction> action;
  std::thread listener_thread(ListenRecoverySocket, ui, std::ref(action));
  listener_thread.detach();

  while (true) {
    // We start adbd in recovery for the device with userdebug build or a unlocked bootloader.
    std::string usb_config =
        fastboot ? "fastboot" : IsRoDebuggable() || IsDeviceUnlocked() ? "adb" : "none";
    std::string usb_state = android::base::GetProperty("sys.usb.state", "none");
    if (fastboot) {
      device->PreFastboot();
    } else {
      device->PreRecovery();
    }
    if (usb_config != usb_state) {
      if (!SetUsbConfig("none")) {
        LOG(ERROR) << "Failed to clear USB config";
      }
      if (!SetUsbConfig(usb_config)) {
        LOG(ERROR) << "Failed to set USB config to " << usb_config;
      }
    }

    ui->SetEnableFastbootdLogo(fastboot);

    auto ret = fastboot ? StartFastboot(device, args) : start_recovery(device, args);

    if (ret == Device::KEY_INTERRUPTED) {
      ret = action.exchange(ret);
      if (ret == Device::NO_ACTION) {
        continue;
      }
    }
    switch (ret) {
      case Device::SHUTDOWN:
        ui->Print("Shutting down...\n");
        Shutdown("userrequested,recovery");
        break;

      case Device::SHUTDOWN_FROM_FASTBOOT:
        ui->Print("Shutting down...\n");
        Shutdown("userrequested,fastboot");
        break;

      case Device::REBOOT_BOOTLOADER:
        ui->Print("Rebooting to bootloader...\n");
        Reboot("bootloader");
        break;

      case Device::REBOOT_FASTBOOT:
        ui->Print("Rebooting to recovery/fastboot...\n");
        Reboot("fastboot");
        break;

      case Device::REBOOT_RECOVERY:
        ui->Print("Rebooting to recovery...\n");
        Reboot("recovery");
        break;

      case Device::REBOOT_RESCUE: {
        // Not using `Reboot("rescue")`, as it requires matching support in kernel and/or
        // bootloader.
        bootloader_message boot = {};
        strlcpy(boot.command, "boot-rescue", sizeof(boot.command));
        std::string err;
        if (!write_bootloader_message(boot, &err)) {
          LOG(ERROR) << "Failed to write bootloader message: " << err;
          // Stay under recovery on failure.
          continue;
        }
        ui->Print("Rebooting to recovery/rescue...\n");
        Reboot("recovery");
        break;
      }

      case Device::ENTER_FASTBOOT:
        if (android::fs_mgr::LogicalPartitionsMapped()) {
          ui->Print("Partitions may be mounted - rebooting to enter fastboot.");
          Reboot("fastboot");
        } else {
          LOG(INFO) << "Entering fastboot";
          fastboot = true;
        }
        break;

      case Device::ENTER_RECOVERY:
        LOG(INFO) << "Entering recovery";
        fastboot = false;
        break;

      case Device::REBOOT:
        ui->Print("Rebooting...\n");
        Reboot("userrequested,recovery");
        break;

      case Device::REBOOT_FROM_FASTBOOT:
        ui->Print("Rebooting...\n");
        Reboot("userrequested,fastboot");
        break;

      default:
        ui->Print("Rebooting...\n");
        Reboot("unknown" + std::to_string(ret));
        break;
    }
  }

  // Should be unreachable.
  return EXIT_SUCCESS;
}
