#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

#include "otautil/roots.h"

#include "bootloader_message/include/bootloader_message/bootloader_message.h"

extern std::string stage;

class args {
    public:
        static std::vector<std::string> get_args(const int *argc, char*** const argv);
};