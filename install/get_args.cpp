#include "twinstall/get_args.h"

std::string stage;
bool has_cache = false;
static constexpr const char* COMMAND_FILE = "/cache/recovery/command";

// command line args come from, in decreasing precedence:
//   - the actual command line
//   - the bootloader control block (one per line, after "recovery")
//   - the contents of COMMAND_FILE (one per line)
std::vector<std::string> args::get_args(const int *argc, char*** const argv) {
  CHECK_GT(*argc, 0);

  bootloader_message boot = {};
  std::string err;
  if (!read_bootloader_message(&boot, &err)) {
    LOG(ERROR) << err;
    // If fails, leave a zeroed bootloader_message.
    boot = {};
  }
  stage = std::string(boot.stage);

  std::string boot_command;
  if (boot.command[0] != 0) {
    if (memchr(boot.command, '\0', sizeof(boot.command))) {
      boot_command = std::string(boot.command);
    } else {
      boot_command = std::string(boot.command, sizeof(boot.command));
    }
    LOG(INFO) << "Boot command: " << boot_command;
    printf("boot command: %s\n", boot_command.c_str());
  }

  if (boot.status[0] != 0) {
    std::string boot_status = std::string(boot.status, sizeof(boot.status));
    LOG(INFO) << "Boot status: " << boot_status;
  }

  std::vector<std::string> args(*argv, *argv + *argc);

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
  if (args.size() == 1 && has_cache) {
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
  // finish the pending work, until finish_recovery() is called.
  std::vector<std::string> options(args.cbegin() + 1, args.cend());
  if (!update_bootloader_message(options, &err)) {
    LOG(ERROR) << "Failed to set BCB message: " << err;
  }

  // Finally, if no arguments were specified, check whether we should boot
  // into fastboot or rescue mode.
  if (args.size() == 1 && boot_command == "boot-fastboot") {
    printf("fastbootd needed\n");
    args.emplace_back("--fastboot");
  } else if (args.size() == 1 && boot_command == "boot-rescue") {
    args.emplace_back("--rescue");
  }

  return args;
}
