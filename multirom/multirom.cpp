#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <linux/capability.h>
#include <linux/xattr.h>
#include <sys/xattr.h>
#include <sys/vfs.h>

// clone libbootimg to /system/extras/ from
// https://github.com/Tasssadar/libbootimg.git
#include <libbootimg.h>

#if LIBBOOTIMG_VERSION  < 0x000200
#error "libbootimg version 0.2.0 or higher is required. Please update libbootimg."
#endif

#include "multirom.h"
#include "../partitions.hpp"
#include "../twrp-functions.hpp"
#include "../twinstall.h"
#include "../minzip/SysUtil.h"
#include "../minzip/Zip.h"
#include "multirom_Zip.h"
#include "../verifier.h"
#include "../variables.h"
#include "../openrecoveryscript.hpp"
#include "../fuse_sideload.h"
#include "multiromedify.h"

extern "C" {
#include "../twcommon.h"
#include "../digest/md5.h"
#include "multirom_hooks.h"
}

#include "../libblkid/include/blkid.h"
#include "cp_xattrs/libcp_xattrs.h"

std::string MultiROM::m_path = "";
std::string MultiROM::m_boot_dev = "";
std::string MultiROM::m_mount_rom_paths[2] = { "", "" };
std::string MultiROM::m_curr_roms_path = "";
MROMInstaller *MultiROM::m_installer = NULL;
MultiROM::baseFolders MultiROM::m_base_folders;
int MultiROM::m_base_folder_cnt = 0;
bool MultiROM::m_has_firmware = false;

base_folder::base_folder(const std::string& name, int min_size, int size)
{
	this->name = name;
	this->min_size = min_size;
	this->size = size;
}

base_folder::base_folder(const base_folder& other)
{
	name = other.name;
	min_size = other.min_size;
	size = other.size;
}

base_folder::base_folder()
{
	min_size = 1;
	size = 1;
}

MultiROM::config::config()
{
	current_rom = INTERNAL_NAME;
	auto_boot_seconds = 5;
	auto_boot_rom = INTERNAL_NAME;
	auto_boot_type = 0;
	colors = 0;
	brightness = 40;
	hide_internal = 0;
	int_display_name = INTERNAL_NAME;
	rotation = TW_DEFAULT_ROTATION;
	enable_adb = 0;
	force_generic_fb = 0;
	anim_duration_coef_pct = 100;
}

bool MultiROM::folderExists()
{
	findPath();
	return !m_path.empty();
}

std::string MultiROM::getRomsPath()
{
	return m_curr_roms_path;
}

std::string MultiROM::getPath()
{
	return m_path;
}

void MultiROM::findPath()
{
	TWPartition *boot = PartitionManager.Find_Partition_By_Path("/boot");
	TWPartition *data = PartitionManager.Find_Partition_By_Path("/data");
	if(!boot || !data)
	{
		gui_print("Failed to find boot or data device!\n");
		m_path.clear();
		return;
	}

	if(!data->Mount(true))
	{
		gui_print("Failed to mount /data partition!\n");
		m_path.clear();
		return;
	}

	m_boot_dev = boot->Actual_Block_Device;

	TWPartition *fw = PartitionManager.Find_Partition_By_Path("/firmware");
	m_has_firmware = (fw && (fw->Current_File_System == "vfat" || fw->Current_File_System == "ext4"));

	static const char *paths[] = {
		"/data/media/multirom",
		"/data/media/0/multirom",
		NULL
	};

	struct stat info;
	for(int i = 0; paths[i]; ++i)
	{
		if(stat(paths[i], &info) >= 0)
		{
			m_path = paths[i];
			m_curr_roms_path = m_path + "/roms/";
			return;
		}
	}
	m_path.clear();
}

bool MultiROM::setRomsPath(std::string loc)
{
	umount("/mnt"); // umount last thing mounted there

	if(loc.compare(INTERNAL_MEM_LOC_TXT) == 0)
	{
		m_curr_roms_path = m_path + "/roms/";
		return true;
	}

	size_t idx = loc.find(' ');
	if(idx == std::string::npos)
	{
		m_curr_roms_path.clear();
		return false;
	}

	std::string dev = loc.substr(0, idx);
	mkdir("/mnt", 0777); // in case it does not exist

	char cmd[256];
	if(loc.find("(ntfs") != std::string::npos)
		sprintf(cmd, "ntfs-3g %s /mnt", dev.c_str());
#ifndef TW_NO_EXFAT_FUSE
	else if(loc.find("(exfat)") != std::string::npos)
		sprintf(cmd, "exfat-fuse -o big_writes,max_read=131072,max_write=131072,nonempty %s /mnt", dev.c_str());
#endif
	else
		sprintf(cmd, "mount %s /mnt", dev.c_str());

	if(system(cmd) != 0)
	{
		LOGERR("Failed to mount location \"%s\"!\n", loc.c_str());
		return false;
	}

	m_curr_roms_path = "/mnt/multirom-"TARGET_DEVICE"/";
	mkdir("/mnt/multirom-"TARGET_DEVICE"/", 0777);
	return true;
}

std::string MultiROM::listInstallLocations()
{
	std::string res = INTERNAL_MEM_LOC_TXT"\n";
	blkid_probe pr;
	const char *type;
	struct dirent *dt;
	char path[64];
	DIR *d = opendir("/dev/block/");
	if(!d)
		return res;

	while((dt = readdir(d)))
	{
		if(strncmp(dt->d_name, "mmcblk0", 7) == 0 || strncmp(dt->d_name, "dm-", 3) == 0)
			continue;
		snprintf(path, sizeof(path), "/dev/block/%s", dt->d_name);

		pr = blkid_new_probe_from_filename(path);
		if(!pr)
			continue;

		blkid_do_safeprobe(pr);
		if (blkid_probe_lookup_value(pr, "TYPE", &type, NULL) >= 0)
		{
			res += "/dev/block/";
			res += dt->d_name;
			res += " (";
			res += type;
			res += ")\n";
		}
		blkid_free_probe(pr);
	}
	closedir(d);
	return res;
}

void MultiROM::updateSupportedSystems()
{
	char p[64];

	snprintf(p, sizeof(p), "%s/infos/ubuntu.txt", m_path.c_str());
	DataManager::SetValue("tw_multirom_ubuntu_supported", (access(p, F_OK) >= 0) ? 1 : 0);

	snprintf(p, sizeof(p), "%s/infos/sailfishos.txt", m_path.c_str());
	DataManager::SetValue("tw_multirom_sailfish_supported", (access(p, F_OK) >= 0) ? 1 : 0);
}

bool MultiROM::installLocNeedsImages(const std::string& loc)
{
	return loc.compare(INTERNAL_MEM_LOC_TXT) != 0 &&
		loc.find("(ext") == std::string::npos &&
		loc.find("(f2fs)") == std::string::npos;
}

bool MultiROM::move(std::string from, std::string to)
{
	std::string roms = getRomsPath();
	std::string cmd = "mv \"" + roms + "/" + from + "\" ";
	cmd += "\"" + roms + "/" + to + "\"";

	gui_print("Moving ROM \"%s\" to \"%s\"...\n", from.c_str(), to.c_str());

	return system(cmd.c_str()) == 0;
}

bool MultiROM::erase(std::string name)
{
	std::string path = getRomsPath() + "/" + name;

	gui_print("Erasing ROM \"%s\"...\n", name.c_str());

	int res = system_args("chattr -R -i \"%s\"", path.c_str());
	if(res != 0)
	{
		gui_print("Failed to remove immutable attribute from that folder!\n");
		return false;
	}
	res = system_args("rm -rf \"%s\"", path.c_str());
	sync();
	return res == 0;
}

bool MultiROM::wipe(std::string name, std::string what)
{
	gui_print("Changing mountpoints...\n");
	if(!changeMounts(name))
	{
		gui_print("Failed to change mountpoints!\n");
		return false;
	}

	char cmd[256];
	bool res = true;
	if(what == "dalvik")
	{
		static const char *dirs[] = {
			"data/dalvik-cache",
			"cache/dalvik-cache",
			"cache/dc",
		};

		for(uint8_t i = 0; res && i < sizeof(dirs)/sizeof(dirs[0]); ++i)
		{
			sprintf(cmd, "rm -rf \"/%s\"", dirs[i]);
			gui_print("Wiping dalvik: %s...\n", dirs[i]);
			res = (system(cmd) == 0);
		}
	}
	else
	{
		sprintf(cmd, "rm -rf \"/%s/\"*", what.c_str());
		gui_print("Wiping ROM's /%s...\n", what.c_str());
		res = (system(cmd) == 0);
	}

	sync();

	if(!res)
		gui_print("ERROR: Failed to erase %s!\n", what.c_str());

	gui_print("Restoring mountpoints...\n");
	restoreMounts();
	return res;
}

bool MultiROM::restorecon(std::string name)
{
	bool res = false;
	bool replaced_contexts = false;

	std::string file_contexts = getRomsPath() + name;
	std::string seapp_contexts = file_contexts + "/boot/seapp_contexts";
	file_contexts += "/boot/file_contexts";

	if(access(file_contexts.c_str(), R_OK) >= 0)
	{
		gui_print("Using ROM's file_contexts\n");
		rename("/file_contexts", "/file_contexts.orig");
		rename("/seapp_contexts", "/seapp_contexts.orig");
		system_args("cp -a \"%s\" /file_contexts", file_contexts.c_str());
		system_args("cp -a \"%s\" /seapp_contexts", seapp_contexts.c_str());
		replaced_contexts = true;
	}

	if(!changeMounts(name))
		goto exit;

	static const char * const parts[] = { "/system", "/data", "/cache", NULL };
	for(int i = 0; parts[i]; ++i)
	{
		gui_print("Running restorecon on ROM's %s\n", parts[i]);
		system_args("restorecon -RFDv %s", parts[i]);
	}

	// SuperSU moves the real app_process into _original
	gui_print("Settting context for app_processXX_original\n");
	system("chcon u:object_r:zygote_exec:s0 /system/bin/app_process32_original");
	system("chcon u:object_r:zygote_exec:s0 /system/bin/app_process64_original");

	restoreMounts();
	res = true;
exit:
	if(replaced_contexts) {
		rename("/file_contexts.orig", "/file_contexts");
		rename("/seapp_contexts.orig", "/seapp_contexts");
	}
	return res;
}

bool MultiROM::initBackup(const std::string& name)
{
	bool hadInternalStorage = (DataManager::GetStrValue("tw_storage_path").find("/data") == 0);

	if(!changeMounts(name))
		return false;

	std::string boot = getRomsPath() + name;
	normalizeROMPath(boot);
	boot += "/boot.img";

	translateToRealdata(boot);

	if(!fakeBootPartition(boot.c_str()))
	{
		restoreMounts();
		return false;
	}

	PartitionManager.Update_System_Details();

	if(hadInternalStorage)
	{
		TWPartition *realdata = PartitionManager.Find_Partition_By_Path("/realdata");
		if(!realdata)
		{
			LOGERR("Couldn't find /realdata!\n");
			restoreBootPartition();
			restoreMounts();
			return false;
		}

		DataManager::SetValue("tw_settings_path", realdata->Storage_Path);
		DataManager::SetValue("tw_storage_path", realdata->Storage_Path);

		DataManager::SetBackupFolder();
	}

	DataManager::SetValue("multirom_do_backup", 1);
	DataManager::SetValue("multirom_rom_name_title", 1);
	return true;
}

void MultiROM::deinitBackup()
{
	bool hadInternalStorage = (DataManager::GetStrValue("tw_storage_path").find(REALDATA) == 0);

	restoreBootPartition();
	restoreMounts();

	DataManager::SetValue("multirom_do_backup", 0);
	DataManager::SetValue("multirom_rom_name_title", 0);

	if(hadInternalStorage)
	{
		TWPartition *data = PartitionManager.Find_Partition_By_Path("/data");
		if(!data)
		{
			LOGERR("Couldn't find /realdata!\n");
			return;
		}

		DataManager::SetValue("tw_settings_path", data->Storage_Path);
		DataManager::SetValue("tw_storage_path", data->Storage_Path);

		DataManager::SetBackupFolder();
	}
}

int MultiROM::getType(std::string name)
{
	std::string path = getRomsPath() + "/" + name + "/";
	if(getRomsPath().find("/mnt") != 0) // Internal memory
	{
		if(name == INTERNAL_NAME)
			return ROM_INTERNAL_PRIMARY;

		if (access((path + "system").c_str(), F_OK) >= 0 &&
			access((path + "data").c_str(), F_OK) >= 0 &&
			access((path + "cache").c_str(), F_OK) >= 0)
		{
			if(access((path + "boot").c_str(), F_OK) >= 0)
				return ROM_ANDROID_INTERNAL;
			else
				return ROM_UTOUCH_INTERNAL;
		}

		if (access((path + "data").c_str(), F_OK) >= 0 &&
			access((path + "rom_info.txt").c_str(), F_OK) >= 0)
		{
			return ROM_SAILFISH_INTERNAL;
		}

		if(access((path + "root").c_str(), F_OK) >= 0)
			return ROM_UBUNTU_INTERNAL;
	}
	else // USB roms
	{
		if (access((path + "system").c_str(), F_OK) >= 0 &&
			access((path + "data").c_str(), F_OK) >= 0 &&
			access((path + "cache").c_str(), F_OK) >= 0)
		{
			if(access((path + "boot").c_str(), F_OK) >= 0)
				return ROM_ANDROID_USB_DIR;
			else
				return ROM_UTOUCH_USB_DIR;
		}

		if (access((path + "system.img").c_str(), F_OK) >= 0 &&
			access((path + "data.img").c_str(), F_OK) >= 0 &&
			access((path + "cache.img").c_str(), F_OK) >= 0)
		{
			if(access((path + "boot").c_str(), F_OK) >= 0)
				return ROM_ANDROID_USB_IMG;
			else
				return ROM_UTOUCH_USB_IMG;
		}

		if(access((path + "root").c_str(), F_OK) >= 0)
			return ROM_UBUNTU_USB_DIR;

		if(access((path + "root.img").c_str(), F_OK) >= 0)
			return ROM_UBUNTU_USB_IMG;
	}
	return ROM_UNKNOWN;
}

static bool rom_sort(std::string a, std::string b)
{
	if(a == INTERNAL_NAME)
		return true;
	if(b == INTERNAL_NAME)
		return false;
	return a.compare(b) < 0;
}

std::string MultiROM::listRoms(uint32_t mask, bool with_bootimg_only)
{
	DIR *d = opendir(getRomsPath().c_str());
	if(!d)
		return "";

	int type;
	std::vector<std::string> vec;
	struct dirent *dr;
	while((dr = readdir(d)) != NULL)
	{
		if(dr->d_type != DT_DIR)
			continue;

		if(dr->d_name[0] == '.')
			continue;

		if(mask == MASK_ALL)
			vec.push_back(dr->d_name);
		else
		{
			type = M(getType(dr->d_name));
			if(!(type & mask))
				continue;

			if((type & MASK_ANDROID) && with_bootimg_only)
			{
				std::string path = getRomsPath() + dr->d_name + "/boot.img";
				if(access(path.c_str(), F_OK) < 0)
					continue;
			}

			vec.push_back(dr->d_name);
		}
	}
	closedir(d);

	std::sort(vec.begin(), vec.end(), rom_sort);

	std::string res = "";
	for(size_t i = 0; i < vec.size(); ++i)
		res += vec[i] + "\n";
	return res;
}

MultiROM::config MultiROM::loadConfig()
{
	config cfg;

	FILE *f = fopen((m_path + "/multirom.ini").c_str(), "r");
	if(f)
	{
		char line[512];
		char *p;
		std::string name, val;
		while(fgets(line, sizeof(line), f))
		{
			p = strtok(line, "=\n");
			if(!p)
				continue;
			name = p;

			p = strtok(NULL, "=\n");
			if(!p)
				continue;
			val = p;

			if(name == "current_rom")
				cfg.current_rom = val;
			else if(name == "auto_boot_seconds")
				cfg.auto_boot_seconds = atoi(val.c_str());
			else if(name == "auto_boot_rom")
				cfg.auto_boot_rom = val;
			else if(name == "auto_boot_type")
				cfg.auto_boot_type = atoi(val.c_str());
			else if(name == "colors_v2")
				cfg.colors = atoi(val.c_str());
			else if(name == "brightness")
				cfg.brightness = atoi(val.c_str());
			else if(name == "enable_adb")
				cfg.enable_adb = atoi(val.c_str());
			else if(name == "hide_internal")
				cfg.hide_internal = atoi(val.c_str());
			else if(name == "int_display_name")
				cfg.int_display_name = val;
			else if(name == "rotation")
				cfg.rotation = atoi(val.c_str());
			else if(name == "force_generic_fb")
				cfg.force_generic_fb = atoi(val.c_str());
			else if(name == "anim_duration_coef_pct")
				cfg.anim_duration_coef_pct = atoi(val.c_str());
			else
				cfg.unrecognized_opts += name + "=" + val + "\n";
		}
		fclose(f);
	}
	return cfg;
}

void MultiROM::saveConfig(const MultiROM::config& cfg)
{
	FILE *f = fopen((m_path + "/multirom.ini").c_str(), "w");
	if(!f)
		return;

	fprintf(f, "current_rom=%s\n", cfg.current_rom.c_str());
	fprintf(f, "auto_boot_seconds=%d\n", cfg.auto_boot_seconds);
	fprintf(f, "auto_boot_rom=%s\n", cfg.auto_boot_rom.c_str());
	fprintf(f, "auto_boot_type=%d\n", cfg.auto_boot_type);
	fprintf(f, "colors_v2=%d\n", cfg.colors);
	fprintf(f, "brightness=%d\n", cfg.brightness);
	fprintf(f, "enable_adb=%d\n", cfg.enable_adb);
	fprintf(f, "hide_internal=%d\n", cfg.hide_internal);
	fprintf(f, "int_display_name=%s\n", cfg.int_display_name.c_str());
	fprintf(f, "rotation=%d\n", cfg.rotation);
	fprintf(f, "force_generic_fb=%d\n", cfg.force_generic_fb);
	fprintf(f, "anim_duration_coef_pct=%d\n", cfg.anim_duration_coef_pct);

	fputs(cfg.unrecognized_opts.c_str(), f);
	fclose(f);
}

bool MultiROM::changeMounts(std::string name)
{
	gui_print("Changing mounts to ROM %s...\n", name.c_str());

	int type = getType(name);
	std::string base = getRomsPath() + name;
	normalizeROMPath(base);

	if(M(type) & MASK_INTERNAL)
		base.replace(0, 5, REALDATA);

	sync();
	mkdir(REALDATA, 0777);

	PartitionManager.Copy_And_Push_Context();

	TWPartition *realdata, *data, *sys, *cache;
	std::vector<TWPartition*>& parts = PartitionManager.getPartitions();
	for(std::vector<TWPartition*>::iterator itr = parts.begin(); itr != parts.end();)
	{
		if((*itr)->Mount_Point == "/system" || (*itr)->Mount_Point == "/cache")
		{
			(*itr)->UnMount(true);
			delete *itr;
			itr = parts.erase(itr);
		}
		else
		{
			if((*itr)->Mount_Point == "/data")
				data = *itr;
			++itr;
		}
	}

	if(!data)
	{
		gui_print("Failed to find data or boot device!\n");
		m_path.clear();
		PartitionManager.Pop_Context();
		PartitionManager.Update_System_Details();
		return false;
	}

	data->UnMount(true);

	realdata = data;
	realdata->Display_Name = "Realdata";
	realdata->Mount_Point = REALDATA;
	realdata->Symlink_Path.replace(0, sizeof("/data")-1, REALDATA);
	realdata->Storage_Path.replace(0, sizeof("/data")-1, REALDATA);
	realdata->Can_Be_Backed_Up = false;

	if(DataManager::GetStrValue(TW_INTERNAL_PATH).find("/data/media") == 0)
	{
		std::string path = DataManager::GetStrValue(TW_INTERNAL_PATH);
		path.replace(0, sizeof("/data")-1, REALDATA);
		DataManager::SetValue(TW_INTERNAL_PATH, path);
	}

	if(!data->Mount(true))
	{
		gui_print("Failed to mount realdata, canceling!\n");
		PartitionManager.Pop_Context();
		PartitionManager.Update_System_Details();
		return false;
	}

	const char *fs = realdata->Fstab_File_System.c_str();
	if(!(M(type) & MASK_IMAGES))
	{
		data = TWPartition::makePartFromFstab("/data_t %s %s/data flags=bindof=/realdata\n", fs, base.c_str());
		sys = TWPartition::makePartFromFstab("/system %s %s/system flags=bindof=/realdata\n", fs, base.c_str());
		cache = TWPartition::makePartFromFstab("/cache %s %s/cache flags=bindof=/realdata\n", fs, base.c_str());
	}
	else
	{
		data = TWPartition::makePartFromFstab("/data_t %s %s/data.img flags=imagemount\n", fs, base.c_str());
		sys = TWPartition::makePartFromFstab("/system %s %s/system.img flags=imagemount\n", fs, base.c_str());
		cache = TWPartition::makePartFromFstab("/cache %s %s/cache.img flags=imagemount\n", fs, base.c_str());
	}

	// Workaround TWRP̈́'s datamedia code
	data->Backup_Display_Name = data->Display_Name = "Data";
	data->Backup_Name = "data";
	data->Backup_Path = data->Mount_Point = "/data";
	data->Can_Be_Backed_Up = true;

	parts.push_back(data);
	parts.push_back(sys);
	parts.push_back(cache);

	if(DataManager::GetStrValue("tw_storage_path").find("/data/media") == 0)
	{
		std::string path = DataManager::GetStrValue("tw_storage_path");
		path.replace(0, sizeof("/data")-1, REALDATA);
		DataManager::SetValue("tw_storage_path", path);
	}

	PartitionManager.Output_Partition_Logging();
	PartitionManager.Update_Storage_Sizes();
	PartitionManager.Write_Fstab();

	if(!data->Mount(true) || !sys->Mount(true) || !cache->Mount(true))
	{
		gui_print("Failed to mount fake partitions, canceling!\n");
		data->UnMount(false);
		sys->UnMount(false);
		cache->UnMount(false);
		realdata->UnMount(false);
		PartitionManager.Pop_Context();
		PartitionManager.Update_System_Details();
		return false;
	}

	// We really don't want scripts to be able to write to real partitions
	//system("mv /sbin/umount /sbin/umount.bak"); // this should be handled by multiromedify.cpp

	// SuperSU tries *very* hard to mount /data and /system, even looks through
	// recovery.fstab and manages to mount the real /system
	system("mv /etc/recovery.fstab /etc/recovery.fstab.bak");

	// This shim prevents everything from mounting anything as read-only
	system("mv /sbin/mount /sbin/mount_real");
	system("cp -a /sbin/mount_shim.sh /sbin/mount");

	return true;
}

void MultiROM::restoreMounts()
{
	if(!PartitionManager.Has_Extra_Contexts())
		return;

	gui_print("Restoring mounts...\n");

	if(DataManager::GetStrValue("tw_storage_path").find("/realdata/media") == 0)
	{
		std::string path = DataManager::GetStrValue("tw_storage_path");
		path.replace(0, sizeof("/realdata")-1, "/data");
		DataManager::SetValue("tw_storage_path", path);
	}

	//system("mv /sbin/umount.bak /sbin/umount");
	system("mv /etc/recovery.fstab.bak /etc/recovery.fstab");
	system("if [ -e /sbin/mount_real ]; then mv /sbin/mount_real /sbin/mount; fi;");

	// script might have mounted it several times over, we _have_ to umount it all
	system("sync;"
		"i=0;"
		"while"
		"  [ -n \"$(grep -e /data -e /system -e /realdata -e /cache -e /sdcard /etc/mtab)\" ] &&"
		"  [ $i -le 10 ];"
		"do"
		"    i=$(( $i + 1 ));"
		"    umount -d /system /data /cache /sdcard /realdata;"
		"done;"
		"i=0;"
		"while [ $i -le 10 ]; do"
		"    if [ -e \"/dev/block/loop$i\" ]; then"
		"        losetup -d  \"/dev/block/loop$i\";"
		"    fi;"
		"    i=$(( $i + 1 ));"
		"done;");

	PartitionManager.Pop_Context();
	PartitionManager.Update_System_Details();

	PartitionManager.Mount_By_Path("/data", true);
	PartitionManager.Mount_By_Path("/cache", true);

	if(DataManager::GetStrValue(TW_INTERNAL_PATH).find("/realdata/media") == 0)
	{
		std::string path = DataManager::GetStrValue(TW_INTERNAL_PATH);
		path.replace(0, sizeof("/realdata")-1, "/data");
		DataManager::SetValue(TW_INTERNAL_PATH, path);
	}

	restoreROMPath();
}

void MultiROM::translateToRealdata(std::string& path)
{
	if(path.find("/sdcard/") != std::string::npos)
	{
		struct stat info;
		if(stat(REALDATA"/media/0", &info) >= 0)
			path.replace(0, strlen("/sdcard/"), REALDATA"/media/0/");
		else
			path.replace(0, strlen("/sdcard/"), REALDATA"/media/");
	}
	else if(path.find("/data/media/") != std::string::npos)
		path.replace(0, strlen("/data/"), REALDATA"/");
}

void MultiROM::normalizeROMPath(std::string& path)
{
	if(!m_mount_rom_paths[0].empty())
	{
		path = m_mount_rom_paths[1];
		return;
	}

	// remove spaces from path
	size_t idx = path.find(' ');
	if(idx == std::string::npos)
	{
		m_mount_rom_paths[0].clear();
		return;
	}

	m_mount_rom_paths[0] = path;
	while(idx != std::string::npos)
	{
		path.replace(idx, 1, "-");
		idx = path.find(' ', idx);
	}

	struct stat info;
	while(stat(path.c_str(), &info) >= 0)
		path += "a";

	m_mount_rom_paths[1] = path;
	system_args("mv \"%s\" \"%s\"", m_mount_rom_paths[0].c_str(), path.c_str());
}

void MultiROM::restoreROMPath()
{
	if(m_mount_rom_paths[0].empty())
		return;

	system_args("mv \"%s\" \"%s\"", m_mount_rom_paths[1].c_str(), m_mount_rom_paths[0].c_str());
	m_mount_rom_paths[0].clear();
}

bool MultiROM::createFakeSystemImg()
{
	std::string sysimg = m_path;
	translateToRealdata(sysimg);

	TWPartition *data = PartitionManager.Find_Partition_By_Path("/realdata");
	TWPartition *sys = PartitionManager.Find_Original_Partition_By_Path("/system");
	if(!data || !sys)
	{
		LOGERR("Failed to find /data or /system partition!\n");
		return false;
	}

	uint64_t size = sys->GetSizeRaw();
	if(size == 0)
		size = sys->GetSizeTotal();
	size = size/1024/1024 + 32;

	if(!createImage(sysimg, "system", size))
	{
		LOGERR("Failed to create system.img!");
		return false;
	}

	std::string loop_device;
	if(TWFunc::Exec_Cmd("losetup -f", loop_device) < 0)
	{
		system_args("rm \"%s/system.img\"", sysimg.c_str());
		LOGERR("Failed to find free loop device\n");
		return false;
	}

	TWFunc::trim(loop_device);

	if(system_args("losetup \"%s\" \"%s/system.img\"", loop_device.c_str(), sysimg.c_str()) != 0)
	{
		system_args("rm \"%s/system.img\"", sysimg.c_str());
		LOGERR("Failed to setup loop device!\n");
		return false;
	}

	if(system_args("mv \"%s\" \"%s-orig\" && ln -s \"%s\" \"%s\"",
		sys->Actual_Block_Device.c_str(), sys->Actual_Block_Device.c_str(), loop_device.c_str(), sys->Actual_Block_Device.c_str()) != 0)
	{
		system_args("losetup -d \"%s\"; rm -f \"%s/system.img\"", loop_device.c_str(), sysimg.c_str());
		LOGERR("Failed to fake system device!\n");
		return false;
	}

	system_args("rm \"%s/system.img\"", sysimg.c_str());
	system_args("echo \"%s\" > /tmp/mrom_fakesyspart", sys->Actual_Block_Device.c_str());
	return true;
}

#define MR_UPDATE_SCRIPT_PATH  "META-INF/com/google/android/"
#define MR_UPDATE_SCRIPT_NAME  "META-INF/com/google/android/updater-script"

bool MultiROM::flashZip(std::string rom, std::string file)
{
	int status = INSTALL_ERROR;
	int verify_status = 0;
	int wipe_cache = 0;
	int sideloaded = 0;
	bool restore_script = false;
	EdifyHacker hacker;
	std::string boot, sysimg, loop_device;
	TWPartition *data, *sys;

	gui_print("Flashing ZIP file %s\n", file.c_str());
	gui_print("ROM: %s\n", rom.c_str());

	if(!verifyZIP(file, verify_status))
		return false;

	gui_print("Preparing ZIP file...\n");
	if(!prepareZIP(file, &hacker, restore_script))  // may change file var
		return false;

	if(!changeMounts(rom))
	{
		gui_print("Failed to change mountpoints!\n");
		goto exit;
	}

	boot = getRomsPath() + rom;
	normalizeROMPath(boot);
	boot += "/boot.img";

	translateToRealdata(file);
	translateToRealdata(boot);

	if(!fakeBootPartition(boot.c_str()))
		goto exit;

	if(hacker.getProcessFlags() & EDIFY_BLOCK_UPDATES)
	{
		gui_print("ZIP uses block updates\n");
		if(!createFakeSystemImg())
			goto exit;
	}

	DataManager::SetValue(TW_SIGNED_ZIP_VERIFY_VAR, 0);
	status = TWinstall_zip(file.c_str(), &wipe_cache);
	DataManager::SetValue(TW_SIGNED_ZIP_VERIFY_VAR, verify_status);

	if(restore_script && hacker.restoreState() && hacker.writeToFile("/tmp/"MR_UPDATE_SCRIPT_NAME))
	{
		gui_print("Restoring original updater-script\n");
		if(system_args("cd /tmp && zip \"%s\" %s", file.c_str(), MR_UPDATE_SCRIPT_NAME) != 0)
			LOGERR("Failed to restore original updater-script, THIS ZIP IS NOW UNUSEABLE FOR NON-MULTIROM FLASHING\n");
	}

	system("rm -r "MR_UPDATE_SCRIPT_PATH);
	if(file == "/tmp/mr_update.zip")
		system("rm /tmp/mr_update.zip");

	if(status != INSTALL_SUCCESS)
		gui_print("Failed to install ZIP!\n");
	else
		gui_print("ZIP successfully installed\n");

	if((hacker.getProcessFlags() & EDIFY_BLOCK_UPDATES) && system_args("busybox umount -d /tmpsystem") != 0)
		system_args("dev=\"$(losetup | grep 'system\\.img' | grep -o '/.*:')\"; losetup -d \"${dev%%:}\"");

exit:
	if(hacker.getProcessFlags() & EDIFY_BLOCK_UPDATES)
		failsafeCheckPartition("/tmp/mrom_fakesyspart");

	restoreBootPartition();
	restoreMounts();

	sideloaded = DataManager::GetIntValue("tw_mrom_sideloaded");
	DataManager::SetValue("tw_mrom_sideloaded", 0);
	if(sideloaded && file.compare(FUSE_SIDELOAD_HOST_PATHNAME) != 0)
		remove(file.c_str());
	return (status == INSTALL_SUCCESS);
}

bool MultiROM::flashORSZip(std::string file, int *wipe_cache)
{
	int status, verify_status = 0;
	EdifyHacker hacker;
	bool restore_script = false;

	gui_print("Flashing ZIP file %s\n", file.c_str());

	if(!verifyZIP(file, verify_status))
		return false;

	gui_print("Preparing ZIP file...\n");
	if(!prepareZIP(file, &hacker, restore_script)) // may change file var
		return false;

	if(hacker.getProcessFlags() & EDIFY_BLOCK_UPDATES)
	{
		gui_print("ZIP uses block updates\n");
		if(!createFakeSystemImg())
			return false;
	}

	DataManager::SetValue(TW_SIGNED_ZIP_VERIFY_VAR, 0);
	status = TWinstall_zip(file.c_str(), wipe_cache);
	DataManager::SetValue(TW_SIGNED_ZIP_VERIFY_VAR, verify_status);

	if(restore_script && hacker.restoreState() && hacker.writeToFile("/tmp/"MR_UPDATE_SCRIPT_NAME))
	{
		gui_print("Restoring original updater-script\n");
		if(system_args("cd /tmp && zip \"%s\" %s", file.c_str(), MR_UPDATE_SCRIPT_NAME) != 0)
			LOGERR("Failed to restore original updater-script, THIS ZIP IS NOW UNUSEABLE FOR NON-MULTIROM FLASHING\n");
	}

	system("rm -r "MR_UPDATE_SCRIPT_PATH);
	if(file == "/tmp/mr_update.zip")
		system("rm /tmp/mr_update.zip");

	if(status != INSTALL_SUCCESS)
		gui_print("Failed to install ZIP!\n");
	else
		gui_print("ZIP successfully installed\n");

	if(hacker.getProcessFlags() & EDIFY_BLOCK_UPDATES)
	{
		if(system_args("busybox umount -d /tmpsystem") != 0)
			system_args("dev=\"$(losetup | grep 'system\\.img' | grep -o '/.*:')\"; losetup -d \"${dev%%:}\"");
		failsafeCheckPartition("/tmp/mrom_fakesyspart");
	}

	return (status == INSTALL_SUCCESS);
}

bool MultiROM::verifyZIP(const std::string& file, int &verify_status)
{
	DataManager::GetValue(TW_SIGNED_ZIP_VERIFY_VAR, verify_status);
	if(!verify_status)
		return true;

	gui_print("Verifying zip signature...\n");
	MemMapping map;
	if (sysMapFile(file.c_str(), &map) != 0) {
		LOGERR("Failed to sysMapFile '%s'\n", file.c_str());
		return false;
	}
	int ret_val = verify_file(map.addr, map.length);
	sysReleaseMap(&map);
	if (ret_val != VERIFY_SUCCESS) {
		LOGERR("Zip signature verification failed: %i\n", ret_val);
		return false;
	}
	return true;
}

bool MultiROM::prepareZIP(std::string& file, EdifyHacker *hacker, bool& restore_script)
{
	bool res = false;

	const ZipEntry *script_entry;
	int script_len;
	char* script_data = NULL;
	int itr = 0;
	bool changed = false;

	system("rm /tmp/mr_update.zip");

	struct stat info;
	if(stat(file.c_str(), &info) < 0)
	{
		gui_print("Failed to open file %s!\n!", file.c_str());
		return false;
	}

	system_args("mkdir -p /tmp/%s", MR_UPDATE_SCRIPT_PATH);

	MemMapping map;
	if (sysMapFile(file.c_str(), &map) != 0) {
		LOGERR("Failed to sysMapFile '%s'\n", file.c_str());
		return false;
	}

	ZipArchive zip;
	if (mzOpenZipArchive(map.addr, map.length, &zip) != 0)
	{
		gui_print("Failed to open ZIP archive %s!\n", file.c_str());
		goto exit;
	}

	script_entry = mzFindZipEntry(&zip, MR_UPDATE_SCRIPT_NAME);
	if(!script_entry)
	{
		gui_print("Failed to find entry "MR_UPDATE_SCRIPT_NAME" in ZIP file %s!\n", file.c_str());
		goto exit;
	}

	if (read_data(&zip, script_entry, &script_data, &script_len) < 0)
	{
		gui_print("Failed to read updater-script entry from %s!\n", file.c_str());
		goto exit;
	}

	mzCloseZipArchive(&zip);
	sysReleaseMap(&map);

	if(!hacker->loadBuffer(script_data, script_len))
	{
		gui_print("Failed to process updater-script!\n");
		goto exit;
	}

	free(script_data);
	script_data = NULL;

	hacker->saveState();
	hacker->replaceOffendings();

	if(!hacker->writeToFile("/tmp/"MR_UPDATE_SCRIPT_NAME))
		goto exit;

	hacker->writeToFile("/tmp/mrom_last_updater_script");

	if(hacker->getProcessFlags() & EDIFY_BLOCK_UPDATES)
	{
		TWPartition *data = PartitionManager.Find_Original_Partition_By_Path("/data");
		if(data && info.st_size*2.25 > data->GetSizeFree())
		{
			LOGERR("Failed, you need at least %llu MB of free space on /data!", uint64_t(info.st_size*2.25)/1024/1024);
			goto exit;
		}
	}

	if(hacker->getProcessFlags() & EDIFY_CHANGED)
	{
		int64_t max_tmp_size = TWFunc::getFreeSpace("/tmp");
		if(max_tmp_size < 0)
			max_tmp_size = 450*1024*1024;
		else
			max_tmp_size *= 0.45;

		LOGINFO("ZIP size limit for /tmp: %.2f MB\n", double(max_tmp_size)/1024/1024);

		if(info.st_size < max_tmp_size)
		{
			gui_print("Copying ZIP to /tmp...\n");
			system_args("cp \"%s\" /tmp/mr_update.zip", file.c_str());
			file = "/tmp/mr_update.zip";
		}
		else if(file.compare(FUSE_SIDELOAD_HOST_PATHNAME) == 0)
		{
			std::string new_file = DataManager::GetStrValue("tw_storage_path") + "/sideload.zip";
			gui_print("Copying ZIP to %s\n", new_file.c_str());
			system_args("cp \"%s\" \"%s\"", file.c_str(), new_file.c_str());
			file = new_file;
		}
		else
		{
			LOGINFO("Modifying ZIP %s in place, gonna restore it later\n", file.c_str());
			restore_script = true;
		}

		if(system_args("cd /tmp && zip \"%s\" %s", file.c_str(), MR_UPDATE_SCRIPT_NAME) != 0)
		{
			system("rm /tmp/mr_update.zip");
			return false;
		}
	}
	else
		gui_print("No need to change ZIP.\n");

	return true;

exit:
	free(script_data);
	mzCloseZipArchive(&zip);
	sysReleaseMap(&map);
	return false;
}

bool MultiROM::injectBoot(std::string img_path, bool only_if_older)
{
	int tr_my_ver = getTrampolineVersion();
	if(tr_my_ver <= 0)
	{
		gui_print("Failed to get trampoline version: %d\n", tr_my_ver);
		return false;
	}

	if(tr_my_ver < 17)
		return injectBootDeprecated(img_path, only_if_older);

	return system_args("\"%s/trampoline\" --inject=\"%s\" --mrom_dir=\"%s\" %s",
		m_path.c_str(), img_path.c_str(), m_path.c_str(), only_if_older ? "" : "-f") == 0;
}

bool MultiROM::injectBootDeprecated(std::string img_path, bool only_if_older)
{
	int rd_cmpr;
	struct bootimg img;
	std::string path_trampoline = m_path + "/trampoline";

	if (access(path_trampoline.c_str(), F_OK) < 0)
	{
		gui_print("%s not found!\n", path_trampoline.c_str());
		return false;
	}

	// EXTRACT BOOTIMG
	gui_print("Extracting boot image...\n");
	system("rm -r /tmp/boot; mkdir /tmp/boot");

	if (libbootimg_init_load(&img, img_path.c_str(), LIBBOOTIMG_LOAD_ALL) < 0 ||
		libbootimg_dump_ramdisk(&img, "/tmp/boot/initrd.img") < 0)
	{
		gui_print("Failed to unpack boot img!\n");
		goto fail;
	}

	// DECOMPRESS RAMDISK
	gui_print("Decompressing ramdisk...\n");
	system("mkdir /tmp/boot/rd");

	rd_cmpr = decompressRamdisk("/tmp/boot/initrd.img", "/tmp/boot/rd/");
	if(rd_cmpr == -1 || access("/tmp/boot/rd/init", F_OK) < 0)
	{
		gui_print("Failed to decompress ramdisk!\n");
		goto fail;
	}

	if(only_if_older)
	{
		int tr_rd_ver = getTrampolineVersion("/tmp/boot/rd/init", true);
		int tr_my_ver = getTrampolineVersion();
		if(tr_rd_ver >= tr_my_ver && tr_my_ver > 0)
		{
			gui_print("No need to inject bootimg, it has the newest trampoline (v%d)\n", tr_rd_ver);
			libbootimg_destroy(&img);
			system("rm -r /tmp/boot");
			return true;
		}
	}

	// COPY TRAMPOLINE
	gui_print("Copying trampoline...\n");
	if(access("/tmp/boot/rd/main_init", F_OK) < 0)
		system("mv /tmp/boot/rd/init /tmp/boot/rd/main_init");

	system_args("cp \"%s\" /tmp/boot/rd/init", path_trampoline.c_str());
	system("chmod 750 /tmp/boot/rd/init");
	system("ln -sf ../main_init /tmp/boot/rd/sbin/ueventd");
	system("ln -sf ../main_init /tmp/boot/rd/sbin/watchdogd");

#ifdef MR_USE_MROM_FSTAB
	system_args("cp \"%s/mrom.fstab\" /tmp/boot/rd/mrom.fstab", m_path.c_str());
#endif

	// COMPRESS RAMDISK
	gui_print("Compressing ramdisk...\n");
	if(!compressRamdisk("/tmp/boot/rd", "/tmp/boot/initrd.img", rd_cmpr))
		goto fail;

	// PACK BOOT IMG
	gui_print("Packing boot image\n");
	if(libbootimg_load_ramdisk(&img, "/tmp/boot/initrd.img") < 0)
	{
		gui_print("Failed to load modified ramdisk!\n");
		goto fail;
	}

#ifdef MR_RD_ADDR
	img.hdr.ramdisk_addr = MR_RD_ADDR;
#endif

	if(img_path != m_boot_dev)
		snprintf((char*)img.hdr.name, BOOT_NAME_SIZE, "tr_ver%d", getTrampolineVersion());

	if(libbootimg_write_img_and_destroy(&img, "/tmp/newboot.img") < 0)
	{
		gui_print("Failed to pack boot image!\n");
		return false;
	}
	system("rm -r /tmp/boot");

	if(img_path == m_boot_dev)
		system_args("dd bs=4096 if=/tmp/newboot.img of=\"%s\"", m_boot_dev.c_str());
	else
		system_args("cp /tmp/newboot.img \"%s\"", img_path.c_str());
	return true;

fail:
	libbootimg_destroy(&img);
	return false;
}

int MultiROM::decompressRamdisk(const char *src, const char* dest)
{
	FILE *f = fopen(src, "r");
	if(!f)
	{
		gui_print("Failed to open initrd\n");
		return -1;
	}

	char m[4];
	if(fread(m, 1, sizeof(m), f) != sizeof(m))
	{
		gui_print("Failed to read initrd magic\n");
		fclose(f);
		return -1;
	}
	fclose(f);

	char cmd[256];
	// gzip
	if(*((uint16_t*)m) == 0x8B1F)
	{
		gui_print("Ramdisk uses GZIP compression\n");
		sprintf(cmd, "cd \"%s\" && gzip -d -c \"%s\" | cpio -i", dest, src);
		system(cmd);
		return CMPR_GZIP;
	}
	// lz4
	else if(*((uint32_t*)m) == 0x184C2102)
	{
		gui_print("Ramdisk uses LZ4 compression\n");
		sprintf(cmd, "cd \"%s\" && lz4 -d \"%s\" stdout | cpio -i", dest, src);
		system(cmd);
		return CMPR_LZ4;
	}
	// lzma
	else if(*((uint32_t*)m) == 0x0000005D || *((uint32_t*)m) == 0x8000005D)
	{
		gui_print("Ramdisk uses LZMA compression\n");
		sprintf(cmd, "cd \"%s\" && lzma -d -c \"%s\" | cpio -i", dest, src);
		system(cmd);
		return CMPR_LZMA;
	}
	else
		gui_print("Unknown ramdisk compression (%X %X %X %X)\n", m[0], m[1], m[2], m[3]);

	return -1;
}

bool MultiROM::compressRamdisk(const char* src, const char* dst, int cmpr)
{
	char cmd[256];
	switch(cmpr)
	{
		case CMPR_GZIP:
			sprintf(cmd, "cd \"%s\" && find . | cpio -o -H newc | gzip > \"%s\"", src, dst);
			system(cmd);
			return true;
		case CMPR_LZ4:
			sprintf(cmd, "cd \"%s\" && find . | cpio -o -H newc | lz4 stdin \"%s\"", src, dst);
			system(cmd);
			return true;
		// FIXME: busybox can't compress with lzma
		case CMPR_LZMA:
			gui_print("Recovery can't compress ramdisk using LZMA!\n");
			return false;
//			sprintf(cmd, "cd \"%s\" && find . | cpio -o -H newc | lzma > \"%s\"", src, dst);
//			system(cmd);
//			return true;
		default:
			gui_print("Invalid compression type: %d", cmpr);
			return false;
	}
}

int MultiROM::copyBoot(std::string& orig, std::string rom)
{
	std::string img_path = getRomsPath() + "/" + rom + "/boot.img";
	char cmd[256];
	sprintf(cmd, "cp \"%s\" \"%s\"", orig.c_str(), img_path.c_str());
	if(system(cmd) != 0)
		return 1;

	orig.swap(img_path);
	return 0;
}

std::string MultiROM::getNewRomName(std::string zip, std::string def)
{
	std::string name = "ROM";
	if(def.empty())
	{
		size_t idx = zip.find_last_of("/");
		size_t idx_dot = zip.find_last_of(".");

		if(zip.substr(idx) == "/rootfs.img")
			name = "Ubuntu";
		else if(idx != std::string::npos)
		{
			// android backups
			if(DataManager::GetStrValue("tw_multirom_add_source") == "backup")
				name = "bckp_" + zip.substr(idx+1);
			// ZIP files
			else if(idx_dot != std::string::npos && idx_dot > idx)
				name = zip.substr(idx+1, idx_dot-idx-1);
		}
	}
	else
		name = def;

	if(name.size() > MAX_ROM_NAME)
		name.resize(MAX_ROM_NAME);

	TWFunc::trim(name);

	DIR *d = opendir(getRomsPath().c_str());
	if(!d)
		return "";

	std::vector<std::string> roms;
	struct dirent *dr;
	while((dr = readdir(d)))
	{
		if(dr->d_name[0] == '.')
			continue;

		if(dr->d_type != DT_DIR && dr->d_type != DT_LNK)
			continue;

		roms.push_back(dr->d_name);
	}

	closedir(d);

	std::string res = name;
	char num[8] = { 0 };
	int c = 1;
	for(size_t i = 0; i < roms.size();)
	{
		if(roms[i] == res)
		{
			res = name;
			sprintf(num, "%d", c++);
			if(res.size() + strlen(num) > MAX_ROM_NAME)
				res.replace(res.size()-strlen(num), strlen(num), num);
			else
				res += num;
			i = 0;
		}
		else
			++i;
	}

	return res;
}

bool MultiROM::createImage(const std::string& base, const char *img, int size)
{
	gui_print("Creating %s.img...\n", img);

	if(size <= 0)
	{
		gui_print("Failed to create %s image: invalid size (%d)\n", img, size);
		return false;
	}

	char cmd[256];

	// make_ext4fs errors out if it has unknown path
	if(TWFunc::Path_Exists("/file_contexts") &&
		(!strcmp(img, "data") ||
		 !strcmp(img, "system") ||
		 !strcmp(img, "cache"))) {
		snprintf(cmd, sizeof(cmd), "make_ext4fs -l %dM -a \"/%s\" -S /file_contexts \"%s/%s.img\"", size, img, base.c_str(), img);
	} else {
		snprintf(cmd, sizeof(cmd), "make_ext4fs -l %dM \"%s/%s.img\"", size, base.c_str(), img);
	}

	LOGINFO("Creating image with cmd: %s\n", cmd);
	return system(cmd) == 0;
}

bool MultiROM::createImagesFromBase(const std::string& base)
{
	int spaceneeded = 0;
	for(baseFolders::const_iterator itr = m_base_folders.begin(); itr != m_base_folders.end(); ++itr)
		spaceneeded +=itr->second.size;
	spaceneeded *=1024; /*Covert to kb*/

	struct statfs buf; /* allocate a buffer */
	int rc;
	long disksize, freesize;  /* computed in kb */
	rc = statfs(base.c_str(), &buf);

	if (rc == 0) {
		/* NOTE: bfree does not include reserved space */
		disksize = (buf.f_bsize/1024L) * buf.f_blocks; /* in kb */
		freesize = (buf.f_bsize/1024L) * buf.f_bavail;
		gui_print("Disk size: %li\nFreesize: %li\nSpace Required: %li\n", disksize, freesize, spaceneeded);
		if (spaceneeded < freesize) {
			for(baseFolders::const_iterator itr = m_base_folders.begin(); itr != m_base_folders.end(); ++itr)
				if(!createImage(base, itr->first.c_str(), itr->second.size))
					return false;
		} else {
			gui_print("Failed to create image, not enough space for images!\n");
			return false;
		}
	} else {
		gui_print("Failed to get disk size!\n");
		return false;
	}


	return true;
}

bool MultiROM::createDirsFromBase(const string& base)
{
	for(baseFolders::const_iterator itr = m_base_folders.begin(); itr != m_base_folders.end(); ++itr)
	{
		if (mkdir((base + "/" + itr->first).c_str(), 0777) < 0)
		{
			gui_print("Failed to create folder %s/%s!\n", base.c_str(), itr->first.c_str());
			return false;
		}
	}
	return true;
}

bool MultiROM::createDirs(std::string name, int type)
{
	std::string base = getRomsPath() + "/" + name;
	if(mkdir(base.c_str(), 0777) < 0)
	{
		gui_print("Failed to create ROM folder \"%s\" (%s)!\n", base.c_str(), strerror(errno));
		return false;
	}

	gui_print("Creating folders and images for type %d\n", type);

	switch(type)
	{
		case ROM_ANDROID_INTERNAL:
		case ROM_ANDROID_USB_DIR:
			if (mkdir((base + "/boot").c_str(), 0777) < 0 ||
				mkdir((base + "/system").c_str(), 0755) < 0 ||
				mkdir((base + "/data").c_str(), 0771) < 0 ||
				mkdir((base + "/cache").c_str(), 0770) < 0)
			{
				gui_print("Failed to create android folders!\n");
				return false;
			}
			system_args(
				"chcon u:object_r:system_file:s0 \"%s/system\";"
				"chcon u:object_r:system_data_file:s0 \"%s/data\";"
				"chcon u:object_r:cache_file:s0 \"%s/cache\";",
				base.c_str(), base.c_str(), base.c_str());
			break;
		case ROM_UTOUCH_INTERNAL:
		case ROM_UTOUCH_USB_DIR:
			if (mkdir((base + "/system").c_str(), 0755) < 0 ||
				mkdir((base + "/data").c_str(), 0771) < 0 ||
				mkdir((base + "/cache").c_str(), 0770) < 0)
			{
				gui_print("Failed to create ubuntu touch folders!\n");
				return false;
			}
			break;
		case ROM_ANDROID_USB_IMG:
			if (mkdir((base + "/boot").c_str(), 0777) < 0)
			{
				gui_print("Failed to create android folders!\n");
				return false;
			}

			if(!createImagesFromBase(base))
				return false;
			break;
		case ROM_UBUNTU_INTERNAL:
		case ROM_UBUNTU_USB_DIR:
		case ROM_INSTALLER_INTERNAL:
		case ROM_INSTALLER_USB_DIR:
		case ROM_SAILFISH_INTERNAL:
			if(!createDirsFromBase(base))
				return false;
			break;
		case ROM_UBUNTU_USB_IMG:
		case ROM_INSTALLER_USB_IMG:
		case ROM_UTOUCH_USB_IMG:
			if(!createImagesFromBase(base))
				return false;
			break;
		default:
			gui_print("Unknown ROM type %d!\n", type);
			return false;

	}
	return true;
}

bool MultiROM::extractBootForROM(std::string base)
{
	char path[256];
	struct bootimg img;

	gui_print("Extracting contents of boot.img...\n");
	if(libbootimg_init_load(&img, (base + "/boot.img").c_str(), LIBBOOTIMG_LOAD_RAMDISK) < 0)
	{
		gui_print("Failed to load bootimg!\n");
		return false;
	}

	system_args("rm -r \"%s/boot/\"*", base.c_str());
	if(libbootimg_dump_ramdisk(&img, (base + "/boot/initrd.img").c_str()) < 0)
	{
		gui_print("Failed to dump ramdisk\n");
		libbootimg_destroy(&img);
		return false;
	}

	libbootimg_destroy(&img);

	system("rm -r /tmp/boot");
	system("mkdir /tmp/boot");

	int rd_cmpr = decompressRamdisk((base + "/boot/initrd.img").c_str(), "/tmp/boot");
	if(rd_cmpr == -1 || access("/tmp/boot/init", F_OK) < 0)
	{
		gui_print("Failed to extract ramdisk!\n");
		return false;
	}

	// copy needed files
	static const char *cp_f[] = {
		"*.rc", "default.prop", "init", "main_init", "fstab.*",
		// Since Android 4.3 - for SELinux
		"file_contexts", "property_contexts", "seapp_contexts", "sepolicy",
		NULL
	};

	for(int i = 0; cp_f[i]; ++i)
		system_args("cp -a /tmp/boot/%s \"%s/boot/\"", cp_f[i], base.c_str());

	// check if main_init exists
	sprintf(path, "%s/boot/main_init", base.c_str());
	if(access(path, F_OK) < 0)
		system_args("mv \"%s/boot/init\" \"%s/boot/main_init\"", base.c_str(), base.c_str());

	system("rm -r /tmp/boot");
	system_args("cd \"%s/boot\" && rm cmdline ramdisk.gz zImage", base.c_str());

	if (DataManager::GetIntValue("tw_multirom_share_kernel") == 0)
	{
		gui_print("Injecting boot.img..\n");
		if(!injectBoot(base + "/boot.img") != 0)
			return false;
	}
	else
		system_args("rm \"%s/boot.img\"", base.c_str());
	return true;
}

bool MultiROM::ubuntuExtractImage(std::string name, std::string img_path, std::string dest)
{
	if(img_path.find("img.gz") != std::string::npos)
	{
		gui_print("Decompressing the image (may take a while)...\n");
		system_args("busybox gzip -d \"%s\"", img_path.c_str());

		img_path.erase(img_path.size()-3);
		if(access(img_path.c_str(), F_OK) < 0)
		{
			gui_print("Failed to decompress the image, more space needed?");
			return false;
		}
	}

	system("mkdir /mnt_ub_img");
	system("umount -d /mnt_ub_img");

	gui_print("Converting the image (may take a while)...\n");
	if(system_args("simg2img \"%s\" /tmp/rootfs.img", img_path.c_str()) != 0)
	{
		system("rm /tmp/rootfs.img");
		gui_print("Failed to convert the image!\n");
		return false;
	}

	system("mount /tmp/rootfs.img /mnt_ub_img");

	if(access("/mnt_ub_img/rootfs.tar.gz", F_OK) < 0)
	{
		system("umount -d /mnt_ub_img");
		system("rm /tmp/rootfs.img");
		gui_print("Invalid Ubuntu image (rootfs.tar.gz not found)!\n");
		return false;
	}

	gui_print("Extracting rootfs.tar.gz (will take a while)...\n");
	if(system_args("zcat /mnt_ub_img/rootfs.tar.gz | gnutar x --numeric-owner -C \"%s\"",  dest.c_str()) != 0)
	{
		system("umount -d /mnt_ub_img");
		system("rm /tmp/rootfs.img");
		gui_print("Failed to extract rootfs.tar.gz archive!\n");
		return false;
	}

	sync();

	system("umount -d /mnt_ub_img");
	system("rm /tmp/rootfs.img");

	char buff[256];
	snprintf(buff, sizeof(buff), "%s/boot/vmlinuz", dest.c_str());
	if(access(buff, F_OK) < 0)
	{
		gui_print("Failed to extract rootfs!\n");
		return false;
	}
	return true;
}

bool MultiROM::patchUbuntuInit(std::string rootDir)
{
	gui_print("Patching ubuntu init...\n");

	std::string initPath = rootDir + "/usr/share/initramfs-tools/";
	std::string locPath = rootDir + "/usr/share/initramfs-tools/scripts/";

	if(access(initPath.c_str(), F_OK) < 0 || access(locPath.c_str(), F_OK) < 0)
	{
		gui_print("init paths do not exits\n");
		return false;
	}

	system_args("cp -a \"%s/ubuntu-init/init\" \"%s\"", m_path.c_str(), initPath.c_str());
	system_args("cp -a \"%s/ubuntu-init/local\" \"%s\"", m_path.c_str(), locPath.c_str());

	system_args("echo \"none	 /proc 	proc 	nodev,noexec,nosuid 	0 	0\" > \"%s/etc/fstab\"", rootDir.c_str());
	return true;
}

void MultiROM::setUpChroot(bool start, std::string rootDir)
{
	static const char *dirs[] = { "dev", "sys", "proc" };
	for(size_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); ++i)
	{
		if(start)
			system_args("mount -o bind /%s \"%s/%s\"", dirs[i], rootDir.c_str(), dirs[i]);
		else
			system_args("umount \"%s/%s\"", rootDir.c_str(), dirs[i]);
	}
}

bool MultiROM::ubuntuUpdateInitramfs(std::string rootDir)
{
	gui_print("Removing tarball installer...\n");

	setUpChroot(true, rootDir);

	system_args("chroot \"%s\" apt-get -y --force-yes purge ac100-tarball-installer flash-kernel", rootDir.c_str());

	ubuntuDisableFlashKernel(false, rootDir);

	gui_print("Updating initramfs...\n");
	system_args("chroot \"%s\" update-initramfs -u", rootDir.c_str());

	// make proper link to initrd.img
	system_args("chroot \"%s\" bash -c 'cd /boot; ln -sf $(ls initrd.img-* | head -n1) initrd.img'", rootDir.c_str());

	setUpChroot(false, rootDir);
	return true;
}

void MultiROM::ubuntuDisableFlashKernel(bool initChroot, std::string rootDir)
{
	gui_print("Disabling flash-kernel\n");
	if(initChroot)
	{
		setUpChroot(true, rootDir);
		system_args("chroot \"%s\" apt-get -y --force-yes purge flash-kernel", rootDir.c_str());
	}

	// We don't want flash-kernel to be active, ever.
	system_args("chroot \"%s\" bash -c \"echo flash-kernel hold | dpkg --set-selections\"", rootDir.c_str());

	system_args("if [ \"$(grep FLASH_KERNEL_SKIP '%s/etc/environment')\" == \"\" ]; then "
			"chroot \"%s\" bash -c \"echo FLASH_KERNEL_SKIP=1 >> /etc/environment\"; fi;",
			rootDir.c_str(), rootDir.c_str());

	if(initChroot)
		setUpChroot(false, rootDir);
}

bool MultiROM::disableFlashKernelAct(std::string name, std::string loc)
{
	int type = getType(2, loc);
	std::string dest = getRomsPath() + "/" + name + "/root";
	if(type == ROM_UBUNTU_USB_IMG && !mountUbuntuImage(name, dest))
		return false;

	ubuntuDisableFlashKernel(true, dest);

	sync();

	if(type == ROM_UBUNTU_USB_IMG)
		umount(dest.c_str());
	return true;
}

int MultiROM::getType(int os, std::string loc)
{
	bool images = installLocNeedsImages(loc);
	switch(os)
	{
		case 1: // android
			if(loc == INTERNAL_MEM_LOC_TXT)
				return ROM_ANDROID_INTERNAL;
			else if(!images)
				return ROM_ANDROID_USB_DIR;
			else
				return ROM_ANDROID_USB_IMG;
			break;
		case 2: // ubuntu
			if(loc == INTERNAL_MEM_LOC_TXT)
				return ROM_UBUNTU_INTERNAL;
			else if(!images)
				return ROM_UBUNTU_USB_DIR;
			else
				return ROM_UBUNTU_USB_IMG;
			break;
		case 3: // installer
			return m_installer->getRomType();
		case 5: // SailfishOS
			if(loc == INTERNAL_MEM_LOC_TXT)
				return ROM_SAILFISH_INTERNAL;
			else
			{
				gui_print("Installation of SailfishOS to external memory is not supported at this time.\n");
				return ROM_UNKNOWN;
			}
			break;
	}
	return ROM_UNKNOWN;
}

bool MultiROM::mountUbuntuImage(std::string name, std::string& dest)
{
	mkdir("/mnt_ubuntu", 0777);

	char cmd[256];
	sprintf(cmd, "mount -o loop %s/%s/root.img /mnt_ubuntu", getRomsPath().c_str(), name.c_str());

	if(system(cmd) != 0)
	{
		gui_print("Failed to mount ubuntu image!\n");
		return false;
	}
	dest = "/mnt_ubuntu";
	return true;
}

bool MultiROM::addROM(std::string zip, int os, std::string loc)
{
	if(!MultiROM::setRomsPath(loc))
	{
		MultiROM::setRomsPath(INTERNAL_MEM_LOC_TXT);
		return false;
	}

	std::string name;
	if(m_installer)
		name = m_installer->getValue("rom_name", name);

	name = getNewRomName(zip, name);
	if(name.empty())
	{
		gui_print("Failed to fixup ROMs name!\n");
		return false;
	}
	gui_print("Installing ROM %s...\n", name.c_str());

	int type = getType(os, loc);

	if((M(type) & MASK_INSTALLER) && !m_installer->checkFreeSpace(getRomsPath(), type == ROM_INSTALLER_USB_IMG))
		return false;

	if(!createDirs(name, type))
		return false;

	std::string root = getRomsPath() + "/" + name;
	bool res = false;
	switch(type)
	{
		case ROM_ANDROID_INTERNAL:
		case ROM_ANDROID_USB_DIR:
		case ROM_ANDROID_USB_IMG:
		{
			std::string src = DataManager::GetStrValue("tw_multirom_add_source");
			if(src == "zip")
			{
				if(!flashZip(name, zip))
					break;

				if(!extractBootForROM(root))
					break;
			}
			else if(src == "backup")
			{
				if(!installFromBackup(name, zip, type))
					break;
			}
			else
			{
				gui_print("Wrong source: %s\n", src.c_str());
				break;
			}
			res = true;
			break;
		}
		case ROM_UBUNTU_INTERNAL:
		case ROM_UBUNTU_USB_DIR:
		case ROM_UBUNTU_USB_IMG:
		{
			std::string dest = root + "/root";
			if(type == ROM_UBUNTU_USB_IMG && !mountUbuntuImage(name, dest))
				break;

			if (ubuntuExtractImage(name, zip, dest) &&
				patchUbuntuInit(dest) && ubuntuUpdateInitramfs(dest))
				res = true;

			char cmd[512];
			sprintf(cmd, "touch %s/var/lib/oem-config/run", dest.c_str());
			system(cmd);

			sprintf(cmd, "cp \"%s/infos/ubuntu.txt\" \"%s/%s/rom_info.txt\"",
					m_path.c_str(), getRomsPath().c_str(), name.c_str());
			system(cmd);

			if(type == ROM_UBUNTU_USB_IMG)
				umount(dest.c_str());
			break;
		}
		case ROM_INSTALLER_INTERNAL:
		case ROM_INSTALLER_USB_DIR:
		case ROM_INSTALLER_USB_IMG:
		{
			std::string text = m_installer->getValue("install_text");
			if(!text.empty())
			{
				size_t start_pos = 0;
				while((start_pos = text.find("\\n", start_pos)) != std::string::npos) {
					text.replace(start_pos, 2, "\n");
					++start_pos;
				}

				gui_print("  \n");
				gui_print(text.c_str());
				gui_print("  \n");
			}

			std::string base = root;
			if(type == ROM_INSTALLER_USB_IMG && !mountBaseImages(root, base))
				break;

			res = true;

			if(res && !m_installer->runScripts("pre_install", base, root))
				res = false;

			if(res && !m_installer->extractDir("root_dir", root))
				res = false;

			if(res && !m_installer->extractTarballs(base))
				res = false;

			if(res && !m_installer->runScripts("post_install", base, root))
				res = false;

			if(type == ROM_INSTALLER_USB_IMG)
				 umountBaseImages(base);
			break;
		}
		case ROM_SAILFISH_INTERNAL:
		{
			std::string base_zip = DataManager::GetStrValue("tw_sailfish_filename_base");
			std::string rootfs_zip = DataManager::GetStrValue("tw_sailfish_filename_rootfs");

			gui_print("  \n");
			gui_print("Flashing base zip...\n");
			if(!flashZip(name, base_zip))
				break;

			gui_print("  \n");
			gui_print("Flashing rootfs zip...\n");

			system("ln -sf /sbin/gnutar /sbin/tar");
			bool flash_res = flashZip(name, rootfs_zip);
			system("ln -sf /sbin/busybox /sbin/tar");
			if(!flash_res)
				break;

			if(!sailfishProcessBoot(root))
				break;

			if(!sailfishProcess(root, name))
				break;

			res = true;
			break;
		}
	}

	if(!res)
	{
		gui_print("Erasing incomplete ROM...\n");
		std::string cmd = "rm -rf \"" + root + "\"";
		system(cmd.c_str());
	}

	sync();

	MultiROM::setRomsPath(INTERNAL_MEM_LOC_TXT);

	delete m_installer;
	m_installer = NULL;

	DataManager::SetValue("tw_multirom_add_source", "");

	return res;
}

bool MultiROM::patchInit(std::string name)
{
	gui_print("Patching init for rom %s...\n", name.c_str());
	int type = getType(name);
	if(!(M(type) & MASK_UBUNTU))
	{
		gui_print("This is not ubuntu ROM. (%d)\n", type);
		return false;
	}
	std::string dest;
	switch(type)
	{
		case ROM_UBUNTU_INTERNAL:
		case ROM_UBUNTU_USB_DIR:
			dest = getRomsPath() + name + "/root/";
			break;
		case ROM_UBUNTU_USB_IMG:
		{
			mkdir("/mnt_ubuntu", 0777);

			char cmd[256];
			sprintf(cmd, "mount -o loop %s/%s/root.img /mnt_ubuntu", getRomsPath().c_str(), name.c_str());

			if(system(cmd) != 0)
			{
				gui_print("Failed to mount ubuntu image!\n");
				return false;
			}
			dest = "/mnt_ubuntu/";
			break;
		}
	}

	bool res = false;
	if(patchUbuntuInit(dest) && ubuntuUpdateInitramfs(dest))
		res = true;

	sync();

	if(type == ROM_UBUNTU_USB_IMG)
		system("umount -d /mnt_ubuntu");;
	return res;
}

bool MultiROM::installFromBackup(std::string name, std::string path, int type)
{
	struct stat info;
	std::string base = getRomsPath() + "/" + name;
	int has_system = 0, has_data = 0;

	if(stat((path + "/boot.emmc.win").c_str(), &info) < 0)
	{
		gui_print("Backup must contain boot image!\n");
		return false;
	}

	DIR *d = opendir(path.c_str());
	if(!d)
	{
		gui_print("Failed to list backup folder\n");
		return false;
	}

	struct dirent *dr;
	while((!has_system || !has_data) && (dr = readdir(d)))
	{
		if(strstr(dr->d_name, "system.") == dr->d_name)
			has_system = 1;
		else if(strstr(dr->d_name, "data.") == dr->d_name)
			has_data = 1;
	}
	closedir(d);

	if(!has_system)
	{
		gui_print("Backup must contain system image!\n");
		return false;
	}

	system_args("cp \"%s/boot.emmc.win\" \"%s/boot.img\"", path.c_str(), base.c_str());

	if(!extractBootForROM(base))
		return false;

	gui_print("Changing mountpoints\n");
	if(!changeMounts(name))
	{
		gui_print("Failed to change mountpoints!\n");
		return false;
	}

	// real /data is mounted to /realdata
	if(path.find("/data/media") == 0)
		path.replace(0, 5, REALDATA);

	unsigned long long total_restore_size = 0, already_restored_size = 0;
	const int partCnt = has_data ? 2 : 1;
	bool res = false;
	TWPartition *sys_part = PartitionManager.Find_Partition_By_Path("/system");
	TWPartition *data_part = PartitionManager.Find_Partition_By_Path("/data");
	if(sys_part && data_part)
	{
		PartitionManager.Set_Restore_Files(path);
		res = PartitionManager.Restore_Partition(sys_part, path, partCnt, &total_restore_size, &already_restored_size) &&
				(!has_data || PartitionManager.Restore_Partition(data_part, path, partCnt, &total_restore_size, &already_restored_size));
	}
	else
	{
		gui_print("Failed to find /system and /data partition!");
	}

	restoreMounts();
	return res;
}

void MultiROM::setInstaller(MROMInstaller *i)
{
	m_installer = i;
}

MROMInstaller *MultiROM::getInstaller(MROMInstaller *i)
{
	return m_installer;
}

void MultiROM::clearBaseFolders()
{
	m_base_folder_cnt = 0;
	m_base_folders.clear();

	char name[32];
	for(int i = 1; i <= MAX_BASE_FOLDER_CNT; ++i)
	{
		sprintf(name, "tw_mrom_image%d", i);
		DataManager::SetValue(name, "");
		DataManager::SetValue(std::string(name) + "_size", 0);
	}
}

void MultiROM::updateImageVariables()
{
	char name[32];
	int i = 1;
	for(baseFolders::const_iterator itr = m_base_folders.begin(); itr != m_base_folders.end(); ++itr)
	{
		sprintf(name, "tw_mrom_image%d", i++);
		DataManager::SetValue(name, itr->first);
		DataManager::SetValue(std::string(name) + "_size", itr->second.size);
	}
}

const base_folder& MultiROM::addBaseFolder(const std::string& name, int min, int def)
{
	base_folder b(name, min, def);
	return addBaseFolder(b);
}

const base_folder& MultiROM::addBaseFolder(const base_folder& b)
{
	LOGINFO("MROMInstaller: base folder: %s (min: %dMB def: %dMB)\n", b.name.c_str(), b.min_size, b.size);
	return m_base_folders.insert(std::make_pair(b.name, b)).first->second;
}

MultiROM::baseFolders& MultiROM::getBaseFolders()
{
	return m_base_folders;
}

base_folder *MultiROM::getBaseFolder(const std::string& name)
{
	baseFolders::iterator itr = m_base_folders.find(name);
	if(itr == m_base_folders.end())
		return NULL;
	return &itr->second;
}

bool MultiROM::mountBaseImages(std::string base, std::string& dest)
{
	mkdir("/mnt_installer", 0777);

	char cmd[256];

	for(baseFolders::const_iterator itr = m_base_folders.begin(); itr != m_base_folders.end(); ++itr)
	{
		sprintf(cmd, "/mnt_installer/%s", itr->first.c_str());
		mkdir(cmd, 0777);

		sprintf(cmd, "mount -o loop %s/%s.img /mnt_installer/%s", base.c_str(), itr->first.c_str(), itr->first.c_str());
		if(system(cmd) != 0)
		{
			gui_print("Failed to mount image %s image!\n", itr->first.c_str());
			return false;
		}
	}
	dest = "/mnt_installer";
	return true;
}

void MultiROM::umountBaseImages(const std::string& base)
{
	sync();

	char cmd[256];
	for(baseFolders::const_iterator itr = m_base_folders.begin(); itr != m_base_folders.end(); ++itr)
	{
		sprintf(cmd, "umount -d %s/%s", base.c_str(), itr->first.c_str());
		system(cmd);

		sprintf(cmd, "%s/%s", base.c_str(), itr->first.c_str());
		rmdir(cmd);
	}
	rmdir(base.c_str());
}

bool MultiROM::ubuntuTouchProcessBoot(const std::string& root, const char *init_folder)
{
	int rd_cmpr;
	struct bootimg img;

	gui_print("Processing boot.img for Ubuntu Touch\n");
	system("rm /tmp/boot.img");
	system_args("cp %s/boot.img /tmp/boot.img", root.c_str());

	if(access("/tmp/boot.img", F_OK) < 0)
	{
		gui_print("boot.img was not found!\b");
		return false;
	}

	// EXTRACT BOOTIMG
	gui_print("Extracting boot image...\n");
	system("rm -r /tmp/boot; mkdir /tmp/boot");

	if (libbootimg_init_load(&img, "/tmp/boot.img", LIBBOOTIMG_LOAD_ALL) < 0 ||
		libbootimg_dump_ramdisk(&img, "/tmp/boot/initrd.img") < 0 ||
		libbootimg_dump_kernel(&img, "/tmp/boot/zImage") < 0)
	{
		gui_print("Failed to unpack boot img!\n");
		goto fail_inject;
	}
	if (libbootimg_dump_dtb(&img, "/tmp/boot/dtb.img") < 0)
	{
		gui_print("Didn't find dtb, ignoring\n");
	}
	else
	{
		gui_print("Found dtb\n");
		system_args("cp /tmp/boot/dtb.img %s/dtb.img", root.c_str());
	}

	// DECOMPRESS RAMDISK
	gui_print("Decompressing ramdisk...\n");
	system("mkdir /tmp/boot/rd");
	rd_cmpr = decompressRamdisk("/tmp/boot/initrd.img", "/tmp/boot/rd/");
	if(rd_cmpr == -1 || access("/tmp/boot/rd/init", F_OK) < 0)
	{
		gui_print("Failed to decompress ramdisk!\n");
		goto fail_inject;
	}

	// COPY INIT FILES
	system_args("cp -ra %s/%s/* /tmp/boot/rd/; chmod 755 /tmp/boot/rd/init", m_path.c_str(), init_folder);

	// COMPRESS RAMDISK
	gui_print("Compressing ramdisk...\n");
	if(!compressRamdisk("/tmp/boot/rd", "/tmp/boot/initrd.img", rd_cmpr))
		return false;

	// DEPLOY
	system_args("cp /tmp/boot/initrd.img %s/initrd.img", root.c_str());
	system_args("cp /tmp/boot/zImage %s/zImage", root.c_str());

	if (libbootimg_load_ramdisk(&img, "/tmp/boot/initrd.img") < 0 ||
		libbootimg_write_img_and_destroy(&img, (root + "/boot.img").c_str()) < 0)
	{
		gui_print("Failed to deploy boot.img!\n");
		goto fail_inject;
	}

	system("rm /tmp/boot.img");
	system("rm -r /tmp/boot");
	return true;

fail_inject:
	libbootimg_destroy(&img);
	system("rm /tmp/boot.img");
	system("rm -r /tmp/boot");
	return false;
}

bool MultiROM::ubuntuTouchProcess(const std::string& root, const std::string& name)
{
	// rom_info.txt
	system_args("cp %s/infos/ubuntu_touch.txt %s/rom_info.txt", m_path.c_str(), root.c_str());

	struct ut_part_info {
		const char *path_twrp;
		const char *path_ubuntu;
		const char *flags;
		std::string block_dev;
		std::string fs;
	} parts[] = {
		{ "/system", "/systemorig", "ro", "", "" },
		{ "/persist", "/persist", "rw", "", "" },
		{ "", "/vendor", "bind,ro", "/system/vendor", "auto" },

		{ 0, 0, 0, "", "" }
	};

	for(ut_part_info *p = parts; p->path_twrp != NULL; ++p)
	{
		TWPartition *tw_part = PartitionManager.Find_Partition_By_Path(p->path_twrp);
		if(!tw_part)
		{
			if(*p->path_twrp != 0)
				gui_print("Couldn't find %s partiton in PartitionManager!\n", p->path_twrp);
			continue;
		}

		p->fs = tw_part->Current_File_System;
		p->block_dev = tw_part->Actual_Block_Device;
		size_t idx = p->block_dev.find("/block");
		if(idx != std::string::npos)
			p->block_dev.erase(idx, sizeof("/block")-1);
	}

	gui_print("Changing mountpoints\n");
	if(!changeMounts(name))
	{
		gui_print("Failed to change mountpoints\n");
		return false;
	}

	// fstab
	if(system("grep -q '/systemorig' /data/ubuntu/etc/fstab") != 0)
	{
		for(ut_part_info *p = parts; p->path_twrp != NULL; ++p)
		{
			if(p->block_dev.empty())
				continue;

			system_args("mkdir -p /data/ubuntu%s;"
						"echo -e \"%s\t%s\t%s\t%s\t0\t0\" >> /data/ubuntu/etc/fstab",
						p->path_ubuntu,
						p->block_dev.c_str(), p->path_ubuntu, p->fs.c_str(), p->flags);
		}
	}

	// change the way android lxc is initiated
	if(system("grep -q 'if \\[ \"$INITRD\" = \"/boot/android-ramdisk.img\" \\]; then' /data/ubuntu/var/lib/lxc/android/pre-start.sh") != 0)
	{
		system("echo -e \""
			"if [ \\\"\\$INITRD\\\" = \\\"/boot/android-ramdisk.img\\\" ]; then\\n"
			"    sed -i \\\"/mount_all /d\\\" \\$LXC_ROOTFS_PATH/init.*.rc\\n"
			"    sed -i \\\"/on nonencrypted/d\\\" \\$LXC_ROOTFS_PATH/init.rc\\n"
			"    folders=\\\"data system cache\\\"\\n"
			"    for dir in \\$folders; do\\n"
			"        mkdir -p \\$LXC_ROOTFS_PATH/\\$dir\\n"
			"        mount -n -o bind,recurse /mrom_dir/\\$dir \\$LXC_ROOTFS_PATH/\\$dir\\n"
			"    done\\n\" >> /data/ubuntu/var/lib/lxc/android/pre-start.sh");

#if MR_DEVICE_RECOVERY_HOOKS >= 1
		system_args("echo -e \"%s \" >> /data/ubuntu/var/lib/lxc/android/pre-start.sh", mrom_hook_ubuntu_touch_get_extra_mounts());
#endif

		system("echo -e \"fi\\n\" >> /data/ubuntu/var/lib/lxc/android/pre-start.sh");
	}

	gui_print("Restoring mounts\n");
	restoreMounts();
	return true;
}

bool MultiROM::sailfishProcessBoot(const std::string& root)
{
	int rd_cmpr;
	struct bootimg img;
	bool res = false;
	int ret;

	gui_print("Processing boot.img for SailfishOS\n");
	system("rm /tmp/boot.img");
	system_args("cp %s/boot.img /tmp/boot.img", root.c_str());

	if(access("/tmp/boot.img", F_OK) < 0)
	{
		gui_print("boot.img was not found!\n");
		return false;
	}

	// EXTRACT BOOTIMG
	gui_print("Extracting boot image...\n");
	system("rm -r /tmp/boot; mkdir /tmp/boot");

	if (libbootimg_init_load(&img, "/tmp/boot.img", LIBBOOTIMG_LOAD_ALL) < 0 ||
		libbootimg_dump_ramdisk(&img, "/tmp/boot/initrd.img") < 0 ||
		libbootimg_dump_kernel(&img, "/tmp/boot/zImage") < 0)
	{
		gui_print("Failed to unpack boot img!\n");
		goto fail_inject;
	}

	ret = libbootimg_dump_dtb(&img, "/tmp/boot/dtb.img");
	if(ret < 0 && ret != LIBBOOTIMG_ERROR_NO_BLOB_DATA)
		gui_print("Failed to extract dtb.img from boot.img!\n");
	else if(ret >= 0)
		system_args("cp /tmp/boot/dtb.img \"%s/dtb.img\"", root.c_str());

	// DEPLOY
	system_args("cp /tmp/boot/initrd.img \"%s/initrd.img\"", root.c_str());
	system_args("cp /tmp/boot/zImage \"%s/zImage\"", root.c_str());

	res = true;
fail_inject:
	system("rm /tmp/boot.img");
	system("rm -r /tmp/boot");
	libbootimg_destroy(&img);
	return res;
}

bool MultiROM::sailfishProcess(const std::string& root, const std::string& name)
{
	char buff[256];

	// rom_info.txt
	if(system_args("cp %s/infos/sailfishos.txt \"%s/rom_info.txt\"", m_path.c_str(), root.c_str()) != 0)
	{
		gui_print("Failed to copy rom_info.txt!\n");
		return false;
	}

	// Disable /system mounting
	snprintf(buff, sizeof(buff), "%s/data/.stowaways/sailfishos/etc/systemd/system/local-fs.target.wants/system.mount", root.c_str());
	remove(buff);
	snprintf(buff, sizeof(buff), "%s/data/.stowaways/sailfishos/sailfishos/lib/systemd/system/system.mount", root.c_str());
	remove(buff);

	// Move /system to the rootfs
	snprintf(buff, sizeof(buff), "%s/system", root.c_str());
	if(access(buff, F_OK) >= 0 && system_args("mv \"%s/system\" \"%s/data/.stowaways/sailfishos/\"", root.c_str(), root.c_str()) != 0)
	{
		gui_print("Failed to move the /system to rootfs\n");
		return false;
	}
	return true;
}

int MultiROM::system_args(const char *fmt, ...)
{
	int ret;
	char cmd[512];
	va_list ap;
	va_start(ap, fmt);

	ret = vsnprintf(cmd, sizeof(cmd), fmt, ap);
	if(ret < (int)sizeof(cmd))
	{
		LOGINFO("Running cmd \"%s\"\n", cmd);
		ret = system(cmd);
	}
	else
	{
		char *buff = new char[ret+1];
		vsnprintf(buff, ret+1, fmt, ap);

		LOGINFO("Running cmd \"%s\"\n", buff);
		ret = system(buff);

		delete[] buff;
	}
	va_end(ap);

	return ret;
}

bool MultiROM::fakeBootPartition(const char *fakeImg)
{
	if(access((m_boot_dev + "-orig").c_str(), F_OK) >= 0)
	{
		gui_print("Failed to fake boot partition, %s-orig already exists!\n", m_boot_dev.c_str());
		return false;
	}

	if(access(fakeImg, F_OK) < 0)
	{
		int fd = creat(fakeImg, 0644);
		if(fd < 0)
		{
			gui_print("Failed to create fake boot image file %s (%s)!\n", fakeImg, strerror(errno));
			return false;
		}
		close(fd);

		// Copy current boot.img as base
		system_args("dd if=\"%s\" of=\"%s\"", m_boot_dev.c_str(), fakeImg);
		gui_print("Current boot sector was used as base for fake boot.img!\n");
	}

	system_args("echo '%s' > /tmp/mrom_fakebootpart", m_boot_dev.c_str());
	system_args("mv \"%s\" \"%s-orig\"", m_boot_dev.c_str(), m_boot_dev.c_str());
	system_args("ln -s \"%s\" \"%s\"", fakeImg, m_boot_dev.c_str());

#ifdef BOARD_BOOTIMAGE_PARTITION_SIZE
	LOGINFO("Truncating fake boot.img to %d bytes\n", BOARD_BOOTIMAGE_PARTITION_SIZE);
	truncate(fakeImg, BOARD_BOOTIMAGE_PARTITION_SIZE);
#endif
	return true;
}

void MultiROM::restoreBootPartition()
{
	if(access((m_boot_dev + "-orig").c_str(), F_OK) < 0)
	{
		gui_print("Failed to restore boot partition, %s-orig does not exist!\n", m_boot_dev.c_str());
		return;
	}

	system_args("rm \"%s\"", m_boot_dev.c_str());
	system_args("mv \"%s\"-orig \"%s\"", m_boot_dev.c_str(), m_boot_dev.c_str());
	remove("/tmp/mrom_fakebootpart");
}

void MultiROM::failsafeCheckPartition(const char *path)
{
	std::string dev;
	if(access(path, F_OK) < 0 || TWFunc::read_file(path, dev) != 0)
		return;

	while(isspace(*(dev.end()-1)))
		dev.erase(dev.end()-1, dev.end());

	struct stat info;
	int res = lstat(dev.c_str(), &info);
	if(access((dev + "-orig").c_str(), F_OK) < 0 || (res >= 0 && !S_ISLNK(info.st_mode)))
		return;

	system_args("rm \"%s\"", dev.c_str());
	system_args("mv \"%s\"-orig \"%s\"", dev.c_str(), dev.c_str());
	remove(path);
}

bool MultiROM::calculateMD5(const char *path, unsigned char *md5sum/*len: 16*/)
{
	FILE *f = fopen(path, "rb");
	if(!f)
	{
		gui_print("Failed to open file %s to calculate MD5 sum!\n", path);
		return false;
	}

	struct MD5Context md5c;
	int len;
	unsigned char buff[1024];

	MD5Init(&md5c);
	while((len = fread(buff, 1, sizeof(buff), f)) > 0)
		MD5Update(&md5c, buff, len);

	MD5Final(md5sum ,&md5c);
	fclose(f);
	return true;
}

bool MultiROM::compareFiles(const char *path1, const char *path2)
{
	unsigned char md5sum1[MD5LENGTH];
	unsigned char md5sum2[MD5LENGTH];

	if(!calculateMD5(path1, md5sum1) || !calculateMD5(path2, md5sum2))
		return false;

	int i;
	for(i = 0; i < MD5LENGTH; ++i)
		if(md5sum1[i] != md5sum2[i])
			return false;

	return true;
}

int MultiROM::getTrampolineVersion()
{
	return getTrampolineVersion(m_path + "/trampoline", false);
}

int MultiROM::getTrampolineVersion(const std::string& path, bool silent)
{
	std::string result = "";
	char cmd[384];

	// check the return value, if the -v is cut off, bad things happen.
	if (snprintf(cmd, sizeof(cmd),
		"strings \"%s\" | grep -q 'Running trampoline' && \"%s\" -v",
		path.c_str(), path.c_str()) >= (int)sizeof(cmd))
	{
		if(!silent)
			gui_print("Failed to get trampoline version, path is too long!\n");
		return -1;
	}

	if(TWFunc::Exec_Cmd(cmd, result) != 0)
	{
		if(!silent)
			gui_print("Failed to get trampoline version!\n");
		return -1;
	}
	return atoi(result.c_str());
}

void MultiROM::executeCacheScripts()
{
	if(m_path.empty() && !folderExists())
		return;

	DIR *roms = opendir(m_curr_roms_path.c_str());
	if(!roms)
	{
		LOGERR("Failed to open ROMs folder %s\n", m_curr_roms_path.c_str());
		return;
	}

	struct dirent *dt;
	struct stat info;
	struct script_t  {
		unsigned long mtime;
		std::string name;
		int type;
	} script;

	script.mtime = 0;

	while((dt = readdir(roms)))
	{
		if(dt->d_type != DT_DIR || dt->d_name[0] == '.')
			continue;

		int type = M(getType(dt->d_name));
		std::string path = m_curr_roms_path + dt->d_name + "/";

		if(type & MASK_ANDROID)
		{
			if(stat((path + SCRIPT_FILE_CACHE).c_str(), &info) < 0)
				continue;

			if(info.st_mtime > script.mtime)
			{
				script.mtime = info.st_mtime;
				script.name = dt->d_name;
				script.type = type;
			}
		}
		else if(type & MASK_UTOUCH)
		{
			if(stat((path + UBUNTU_COMMAND_FILE).c_str(), &info) < 0)
				continue;

			if(info.st_mtime > script.mtime)
			{
				script.mtime = info.st_mtime;
				script.name = dt->d_name;
				script.type = type;
			}
		}
	}
	closedir(roms);

	if(script.mtime == 0)
		return;

	LOGINFO("Running script for ROM %s, type %d\n", script.name.c_str(), script.type);

	if(!changeMounts(script.name))
		return;

	int had_boot;
	std::string boot, boot_orig;

	boot = getRomsPath() + script.name;
	normalizeROMPath(boot);
	boot += "/boot.img";

	boot_orig = boot;

	translateToRealdata(boot);

	had_boot = access(boot.c_str(), F_OK) >= 0;

	if(!fakeBootPartition(boot.c_str()))
	{
		restoreMounts();
		return;
	}

	DataManager::SetValue("multirom_rom_name_title", 1);
	DataManager::SetValue("tw_multirom_rom_name", script.name);

	if(script.type & MASK_ANDROID)
	{
		DataManager::SetValue(TW_ORS_IS_SECONDARY_ROM, 1);
		OpenRecoveryScript::Run_OpenRecoveryScript();
	}
	else if(script.type & MASK_UTOUCH)
	{
		startSystemImageUpgrader();
		system("umount -d /cache/system");
	}

	restoreBootPartition();
	restoreMounts();

	if(script.type & MASK_UTOUCH)
	{
		ubuntuTouchProcessBoot(getRomsPath() + script.name, "ubuntu-touch-sysimage-init");
		if(DataManager::GetIntValue("system-image-upgrader-res") == 0)
		{
			gui_print("\nSUCCESS, rebooting...\n");
			usleep(2000000); // Sleep for 2 seconds before rebooting
			TWFunc::tw_reboot(rb_system);
		}
	}
	else if(script.type & MASK_ANDROID)
	{
		if(!had_boot && compareFiles(getBootDev().c_str(), boot_orig.c_str()))
			unlink(boot_orig.c_str());
		else
		{
			DataManager::SetValue("tw_multirom_share_kernel", !had_boot);
			extractBootForROM(getRomsPath() + script.name);
		}

		gui_print("\nRebooting...\n");
		usleep(2000000); // Sleep for 2 seconds before rebooting
		TWFunc::tw_reboot(rb_system);
	}

	DataManager::SetValue("multirom_rom_name_title", 0);
}

void MultiROM::startSystemImageUpgrader()
{
	DataManager::SetValue("tw_back", "main");
	DataManager::SetValue("tw_action", "system_image_upgrader");
	DataManager::SetValue("tw_has_action2", "0");
	DataManager::SetValue("tw_action2", "");
	DataManager::SetValue("tw_action2_param", "");
	DataManager::SetValue("tw_action_text1", "Ubuntu Touch");
	DataManager::SetValue("tw_action_text2", "Running system-image-upgrader");
	DataManager::SetValue("tw_complete_text1", "system-image-upgrader Complete");
	DataManager::SetValue("tw_has_cancel", 0);
	DataManager::SetValue("tw_show_reboot", 0);
	gui_startPage("action_page", 0, 1);
}

bool MultiROM::copyPartWithXAttrs(const std::string& src, const std::string& dst, const std::string& part, bool skipMedia)
{
	if(!skipMedia)
	{
		gui_print("Copying /%s...\n", part.c_str());
		if(system_args("IFS=$'\n'; for f in $(find \"%s/%s\" -maxdepth 1 -mindepth 1); do cp -a \"$f\" \"%s/%s/\"; done", src.c_str(), part.c_str(), dst.c_str(), part.c_str()) != 0)
		{
			LOGERR("Copying failed, see log for more info!\n");
			return false;
		}

		if(!cp_xattrs_recursive(src + "/" + part, dst + "/" + part, DT_DIR))
			return false;

		return true;
	}
	else
	{
		char path1[256];
		char path2[256];
		DIR *d;
		struct dirent *dt;
		bool res = true;

		gui_print("Copying /%s...\n", part.c_str());

		snprintf(path1, sizeof(path1), "%s/%s", src.c_str(), part.c_str());
		snprintf(path2, sizeof(path2), "%s/%s", dst.c_str(), part.c_str());

		if(!cp_xattrs_single_file(path1, path2))
			return false;

		d = opendir(path1);
		if(!d)
		{
			LOGERR("Failed to open %s!\n", path1);
			return false;
		}

		while((dt = readdir(d)))
		{
			if (dt->d_type == DT_DIR && dt->d_name[0] == '.' &&
				(dt->d_name[1] == '.' || dt->d_name[1] == 0))
				continue;

			if(dt->d_type == DT_DIR && strcmp(dt->d_name, "media") == 0)
			{
				struct stat st;
				snprintf(path1, sizeof(path1), "%s/%s/media", src.c_str(), part.c_str());
				snprintf(path2, sizeof(path2), "%s/%s/media", dst.c_str(), part.c_str());

				if(stat(path1, &st) >= 0)
				{
					mkdir(path2, st.st_mode);
					cp_xattrs_single_file(path1, path2);
				}
				continue;
			}

			if(system_args("cp -a \"%s/%s/%s\" \"%s/%s/\"", src.c_str(), part.c_str(), dt->d_name, dst.c_str(), part.c_str()) != 0)
			{
				LOGERR("Copying failed, see log for more info!\n");
				res = false;
				break;
			}

			snprintf(path1, sizeof(path1), "%s/%s/%s", src.c_str(), part.c_str(), dt->d_name);
			snprintf(path2, sizeof(path2), "%s/%s/%s", dst.c_str(), part.c_str(), dt->d_name);

			if(!cp_xattrs_recursive(path1, path2, dt->d_type))
			{
				res = false;
				break;
			}
		}

		closedir(d);
		return res;
	}
}

bool MultiROM::copyInternal(const std::string& dest_name)
{
	gui_print("Copying Internal ROM to \"%s\"\n", dest_name.c_str());

	std::string dest_dir = getRomsPath() + dest_name + "/";
	if(access(dest_dir.c_str(), F_OK) >= 0)
	{
		LOGERR("This ROM name is taken!\n");
		return false;
	}

	if (!PartitionManager.Mount_By_Path("/system", true) ||
		!PartitionManager.Mount_By_Path("/data", true) ||
		!PartitionManager.Mount_By_Path("/cache", true))
	{
		LOGERR("Failed to mount all partitions!\n");
		return false;
	}

	if(!createDirs(dest_name, ROM_ANDROID_INTERNAL))
		goto erase_incomplete;

	gui_print("Copying boot partition...\n");
	if(system_args("dd if=%s of=\"%s/boot.img\" bs=4096", getBootDev().c_str(), dest_dir.c_str()) != 0)
	{
		gui_print("Dumping boot dev failed!\n");
		goto erase_incomplete;
	}

	DataManager::SetValue("tw_multirom_share_kernel", 0);
	if(!extractBootForROM(dest_dir))
		goto erase_incomplete;

	static const char *parts[] = { "system", "data", "cache" };
	for(size_t i = 0; i < sizeof(parts)/sizeof(parts[0]); ++i)
		if(!copyPartWithXAttrs("", dest_dir, parts[i], strcmp(parts[i], "data") == 0))
			goto erase_incomplete;

	return true;

erase_incomplete:
	gui_print("Failed, removing incomplete ROM...\n");
	system_args("rm -rf \"%s\"", dest_dir.c_str());
	return false;
}

bool MultiROM::wipeInternal()
{
	if(!PartitionManager.Wipe_By_Path("/cache") || !PartitionManager.Wipe_By_Path("/system"))
		return false;

	if(!PartitionManager.Factory_Reset())
	{
		LOGERR("Wiping /data without datamedia failed!\n");
		return false;
	}
	return true;
}

bool MultiROM::copySecondaryToInternal(const std::string& rom_name)
{
	gui_print("Copying secondary ROM \"%s\" to Internal...\n", rom_name.c_str());

	std::string src_dir = getRomsPath() + rom_name + "/";
	if(access(src_dir.c_str(), F_OK) < 0)
	{
		LOGERR("This ROM does not exist!\n");
		return false;
	}

	if (!PartitionManager.Mount_By_Path("/system", true) ||
		!PartitionManager.Mount_By_Path("/data", true) ||
		!PartitionManager.Mount_By_Path("/cache", true))
	{
		LOGERR("Failed to mount all partitions!\n");
		return false;
	}

	gui_print("Writing boot partition...\n");
	if(system_args("dd if=\"%s/boot.img\" of=\"%s\" bs=4096", src_dir.c_str(), getBootDev().c_str()) != 0)
	{
		gui_print("Writing boot.img has failed!\n");
		return false;
	}

	injectBoot(getBootDev(), true);

	static const char *parts[] = { "system", "data", "cache" };
	for(size_t i = 0; i < sizeof(parts)/sizeof(parts[0]); ++i)
		if(!copyPartWithXAttrs(src_dir, "", parts[i], strcmp(parts[i], "data") == 0))
			return false;

	return true;
}

bool MultiROM::duplicateSecondary(const std::string& src, const std::string& dst)
{
	gui_print("Copying secondary ROM \"%s\" to \"%s\"\n", src.c_str(), dst.c_str());

	std::string src_dir = getRomsPath() + src;
	std::string dest_dir = getRomsPath() + dst;
	if(access(dest_dir.c_str(), F_OK) >= 0)
	{
		LOGERR("This ROM name is taken!\n");
		return false;
	}

	if(system_args("cp -a \"%s\" \"%s\"", src_dir.c_str(), dest_dir.c_str()) != 0)
	{
		LOGERR("Copying failed, see log for more info!\n");
		goto erase_incomplete;
	}

	if(!cp_xattrs_recursive(src_dir, dest_dir, DT_DIR))
		goto erase_incomplete;

	return true;

erase_incomplete:
	gui_print("Failed, removing incomplete ROM...\n");
	system_args("rm -rf \"%s\"", dest_dir.c_str());
	return false;
}

std::string MultiROM::getRecoveryVersion()
{
	TWPartition *recovery = PartitionManager.Find_Partition_By_Path("/recovery");
	if(!recovery)
		return std::string();

	struct boot_img_hdr hdr;
	if(libbootimg_load_header(&hdr, recovery->Actual_Block_Device.c_str()) < 0)
		return std::string();

	hdr.name[BOOT_NAME_SIZE-1] = 0; // to be sure

	if (strncmp((char*)hdr.name, "mrom", 4) != 0 ||
		strlen((char*)hdr.name) != sizeof("mromYYYYMMDD-PP")-1)
	{
		return std::string();
	}

	int patch = atoi((char*)hdr.name+sizeof("mromYYYYMMDD-")-1);
	std::string res((char*)hdr.name+4, sizeof("YYYYMMDD")-1);

	res.insert(6, "-");
	res.insert(4, "-");

	if(patch > 0)
	{
		char buff[5];
		snprintf(buff, sizeof(buff), " p%d", patch);
		res += buff;
	}

	return res;
}
