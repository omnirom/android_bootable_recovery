#include "twrpApex.hpp"

namespace fs = std::filesystem;

bool twrpApex::loadApexImages() {
	std::vector<std::string> apexFiles;
	if (access(APEX_DIR, F_OK) != 0) {
		LOGERR("Unable to open %s\n", APEX_DIR);
		return false;
	}
	for (const auto& entry : fs::directory_iterator(APEX_DIR)) {
	   if (entry.is_regular_file()) {
		   apexFiles.push_back(entry.path().string());
	   }
	}

	if (!createLoopBackDevices(apexFiles.size())) {
		LOGERR("unable to create loop devices to mount apex files\n");
		return false;
	}

	size_t apexFileCount = 0;
	for (auto&& apexFile : apexFiles) {
		std::string fileToMount = unzipImage(apexFile);
		loadApexImage(fileToMount, apexFileCount++);
	}
	return true;
}

std::string twrpApex::unzipImage(std::string file) {
	ZipArchiveHandle handle;
	int32_t ret = OpenArchive(file.c_str(), &handle);
	if (ret != 0) {
		LOGERR("unable to open zip archive %s\n", file.c_str());
		CloseArchive(handle);
		return nullptr;
	}

	ZipEntry entry;
	ZipString zip_string(APEX_PAYLOAD);
	ret = FindEntry(handle, zip_string, &entry);
	if (ret != 0) {
		LOGERR("unable to find %s in zip\n", APEX_PAYLOAD);
		CloseArchive(handle);
		return nullptr;
	}

	std::string baseFile = basename(file.c_str());
	std::string path(APEX_BASE);
	path = path + baseFile;
	int fd = open(path.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0666);
	ret = ExtractEntryToFile(handle, &entry, fd);
	if (ret != 0) {
		LOGERR("unable to extract %s\n", path.c_str());
		close(fd);
		CloseArchive(handle);
		return nullptr;
	}

	close(fd);
	CloseArchive(handle);
	return path;
}

bool twrpApex::createLoopBackDevices(size_t count) {
	size_t existing_loop_device_count = 0;

	for (const auto& entry : fs::directory_iterator(LOOP_BLOCK_DEVICE_DIR)) {
	   if (entry.is_block_file() && entry.path().string().find("loop") != std::string::npos) {
		   existing_loop_device_count++;
	   }
	}

	if (existing_loop_device_count < count) {
		size_t devices_to_create = count - existing_loop_device_count;
		for (size_t i = existing_loop_device_count; i < (devices_to_create + existing_loop_device_count); ++i) {
			std::string loop_device = LOOP_BLOCK_DEVICE_DIR;
			loop_device = loop_device + "loop" + std::to_string(i);
			int ret = mknod(loop_device.c_str(), S_IFBLK | S_IRUSR | S_IWUSR , makedev(7, i));
			if (ret != 0) {
				LOGERR("unable to create loop device: %s\n", loop_device.c_str());
				return false;
			}
		}
	}
	return true;
}

bool twrpApex::loadApexImage(std::string fileToMount, size_t loop_device_number) {
	struct loop_info64 info;

	int fd = open(fileToMount.c_str(), O_RDONLY);
	if (fd < 0) {
		LOGERR("unable to open apex file: %s\n", fileToMount.c_str());
		return false;
	}

	std::string loop_device = "/dev/block/loop" + std::to_string(loop_device_number);
	int loop_fd = open(loop_device.c_str(), O_RDONLY);
	if (loop_fd < 0) {
		LOGERR("unable to open loop device: %s\n", loop_device.c_str());
		close(fd);
		return false;
	}

	if (ioctl(loop_fd, LOOP_SET_FD, fd) < 0) {
		LOGERR("failed to mount %s to loop device %s\n", fileToMount.c_str(), loop_device.c_str());
		close(fd);
		close(loop_fd);
		return false;
	}

	close(fd);

	memset(&info, 0, sizeof(struct loop_info64));
	if (ioctl(loop_fd, LOOP_SET_STATUS64, &info)) {
		LOGERR("failed to mount loop: %s: %s\n", fileToMount.c_str(), strerror(errno));
		close(loop_fd);
		return false;
	}
	close(loop_fd);
	std::string bind_mount = fileToMount + "-mount";
	int ret = mkdir(bind_mount.c_str(), 0666);
	if (ret != 0) {
		LOGERR("unable to create mount directory: %s\n", bind_mount.c_str());
		return false;
	}

	ret = mount(loop_device.c_str(), bind_mount.c_str(), "ext4", MS_RDONLY, nullptr);
	if (ret != 0) {
		LOGERR("unable to mount loop device %s to %s\n", loop_device.c_str(), bind_mount.c_str());
		return false;
	}

	return true;
}
