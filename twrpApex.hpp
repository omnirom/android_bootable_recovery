
#ifndef TWRPAPEX_HPP
#define TWRPAPEX_HPP

#include <string>
#include <vector>
#include <filesystem>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/loop.h>
#include <sys/mount.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <unistd.h>

#include <ziparchive/zip_archive.h>
#include "twcommon.h"


#define APEX_DIR "/system_root/system/apex"
#define APEX_PAYLOAD "apex_payload.img"
#define LOOP_BLOCK_DEVICE_DIR "/dev/block/"
#define APEX_BASE "/apex/"

class twrpApex {
public:
	bool loadApexImages();

private:
	std::string unzipImage(std::string file);
	bool createLoopBackDevices(size_t count);
	bool loadApexImage(std::string fileToMount, size_t loop_device_number);
};
#endif
