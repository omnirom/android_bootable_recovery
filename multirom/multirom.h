#ifndef MULTIROM_H
#define MULTIROM_H

#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <vector>
#include <errno.h>
#include <sys/mount.h>

#include "../data.hpp"
#include "mrominstaller.h"

enum
{
	ROM_INTERNAL_PRIMARY  = 0,
	ROM_ANDROID_INTERNAL  = 1,
	ROM_ANDROID_USB_DIR   = 2,
	ROM_ANDROID_USB_IMG   = 3,
	ROM_UBUNTU_INTERNAL   = 4,
	ROM_UBUNTU_USB_DIR    = 5,
	ROM_UBUNTU_USB_IMG    = 6,
	ROM_INSTALLER_INTERNAL= 7,
	ROM_INSTALLER_USB_DIR = 8,
	ROM_INSTALLER_USB_IMG = 9,
	ROM_UTOUCH_INTERNAL   = 10,
	ROM_UTOUCH_USB_DIR    = 11,
	ROM_UTOUCH_USB_IMG    = 12,
	ROM_SAILFISH_INTERNAL = 13,

	ROM_UNKNOWN,
};

enum
{
	CMPR_GZIP   = 0,
	CMPR_LZ4    = 1,
	CMPR_LZMA   = 2,
};

#define M(x) (1 << x)
#define MASK_UBUNTU (M(ROM_UBUNTU_INTERNAL) | M(ROM_UBUNTU_USB_IMG)| M(ROM_UBUNTU_USB_DIR))
#define MASK_ANDROID (M(ROM_INTERNAL_PRIMARY) | M(ROM_ANDROID_USB_DIR) | M(ROM_ANDROID_USB_IMG) | M(ROM_ANDROID_INTERNAL))
#define MASK_IMAGES (M(ROM_ANDROID_USB_IMG) | M(ROM_UBUNTU_USB_IMG) | M(ROM_INSTALLER_USB_IMG) | M(ROM_UTOUCH_USB_IMG))
#define MASK_INTERNAL (M(ROM_INTERNAL_PRIMARY) | M(ROM_ANDROID_INTERNAL) | M(ROM_UBUNTU_INTERNAL) | M(ROM_INSTALLER_INTERNAL) | M(ROM_UTOUCH_INTERNAL))
#define MASK_INSTALLER (M(ROM_INSTALLER_INTERNAL) | M(ROM_INSTALLER_USB_DIR) | M(ROM_INSTALLER_USB_IMG))
#define MASK_UTOUCH (M(ROM_UTOUCH_INTERNAL) | M(ROM_UTOUCH_USB_IMG) | M(ROM_UTOUCH_USB_DIR))
#define MASK_SAILFISH (M(ROM_SAILFISH_INTERNAL))
#define MASK_ALL 0xFFFFFFFF

#define INTERNAL_NAME "Internal"
#define REALDATA "/realdata"
#define MAX_ROM_NAME 26
#define INTERNAL_MEM_LOC_TXT "Internal memory"

// Not defined in android includes?
#define MS_RELATIME (1<<21)

#define MAX_BASE_FOLDER_CNT 5

// default image sizes
#ifndef BOARD_SYSTEMIMAGE_PARTITION_SIZE
#define SYS_IMG_DEFSIZE 640
#else
#define SYS_IMG_DEFSIZE (BOARD_SYSTEMIMAGE_PARTITION_SIZE/1024/1024)
#endif
#define DATA_IMG_DEFSIZE 1024
#define CACHE_IMG_DEFSIZE 436

#define SYS_IMG_MINSIZE 450
#define DATA_IMG_MINSIZE 150
#define CACHE_IMG_MINSIZE 50

#define SAILFISH_DATA_IMG_MINSIZE 1024
#define SAILFISH_DATA_IMG_DEFSIZE 1536

#define UB_DATA_IMG_MINSIZE 2048
#define UB_DATA_IMG_DEFSIZE 4095

#define MROM_SWAP_WITH_SECONDARY 0
#define MROM_SWAP_COPY_SECONDARY 1
#define MROM_SWAP_COPY_INTERNAL  2
#define MROM_SWAP_MOVE_INTERNAL  3
#define MROM_SWAP_DUPLICATE      4

#define MROM_AUTOBOOT_LAST       0x01
#define MROM_AUTOBOOT_CHECK_KEYS 0x04

#define MROM_AUTOBOOT_TRIGGER_DISABLED 0
#define MROM_AUTOBOOT_TRIGGER_TIME     1
#define MROM_AUTOBOOT_TRIGGER_KEYS     2

struct base_folder
{
	base_folder(const std::string& name, int min_size, int size);
	base_folder(const base_folder& other);
	base_folder();

	std::string name;
	int min_size;
	int size;
};

class EdifyHacker;

class MultiROM
{
public:
	typedef std::map<std::string, base_folder> baseFolders;

	struct config {
		config();

		std::string current_rom;
		int auto_boot_seconds;
		int auto_boot_type;
		std::string auto_boot_rom;
		int colors;
		int brightness;
		int enable_adb;
		int hide_internal;
		std::string int_display_name;
		int rotation;
		int force_generic_fb;
		int anim_duration_coef_pct;

		std::string unrecognized_opts;
	};

	static bool folderExists();
	static std::string getRomsPath();
	static std::string getPath();
	static int getType(std::string name);
	static std::string listRoms(uint32_t mask = MASK_ALL, bool with_bootimg_only = false);
	static void setInstaller(MROMInstaller *i);
	static MROMInstaller *getInstaller(MROMInstaller *i);
	static std::string getBootDev() { return m_boot_dev; }
	static bool hasFirmwareDev() { return m_has_firmware; }
	static void updateSupportedSystems();
	static bool installLocNeedsImages(const std::string& loc);

	static void clearBaseFolders();
	static const base_folder& addBaseFolder(const std::string& name, int min, int def);
	static const base_folder& addBaseFolder(const base_folder& b);
	static baseFolders& getBaseFolders();
	static base_folder *getBaseFolder(const std::string& name);
	static void updateImageVariables();

	static bool move(std::string from, std::string to);
	static bool erase(std::string name);
	static bool restorecon(std::string name);

	static bool flashZip(std::string rom, std::string file);
	static bool flashORSZip(std::string file, int *wipe_cache);
	static bool injectBoot(std::string img_path, bool only_if_older = false);
	static bool injectBootDeprecated(std::string img_path, bool only_if_older = false);
	static bool extractBootForROM(std::string base);
	static int copyBoot(std::string& orig, std::string rom);
	static bool wipe(std::string name, std::string what);
	static bool initBackup(const std::string& name);
	static void deinitBackup();

	static config loadConfig();
	static void saveConfig(const config& cfg);

	static bool addROM(std::string zip, int os, std::string loc);

	static std::string listInstallLocations();
	static bool setRomsPath(std::string loc);
	static bool patchInit(std::string name);
	static bool disableFlashKernelAct(std::string name, std::string loc);
	static bool fakeBootPartition(const char *fakeImg);
	static void restoreBootPartition();
	static void failsafeCheckPartition(const char *path);
	static bool compareFiles(const char *path1, const char *path2);

	static void executeCacheScripts();
	static void startSystemImageUpgrader();
	static bool ubuntuTouchProcessBoot(const std::string& root, const char *init_folder);
	static bool ubuntuTouchProcess(const std::string& root, const std::string& name);
	static bool sailfishProcessBoot(const std::string& root);
	static bool sailfishProcess(const std::string& root, const std::string& name);

	static bool copyInternal(const std::string& dest_name);
	static bool wipeInternal();
	static bool copySecondaryToInternal(const std::string& rom_name);
	static bool duplicateSecondary(const std::string& src, const std::string& dst);

	static std::string getRecoveryVersion();

private:
	static void findPath();
	static bool changeMounts(std::string base);
	static void restoreMounts();
	static bool prepareZIP(std::string& file, EdifyHacker *hacker, bool& restore_script);
	static bool verifyZIP(const std::string& file, int &verify_status);
	static std::string getNewRomName(std::string zip, std::string def);
	static bool createDirs(std::string name, int type);
	static bool compressRamdisk(const char *src, const char *dest, int cmpr);
	static int decompressRamdisk(const char *src, const char *dest);
	static bool installFromBackup(std::string name, std::string path, int type);
	static int getType(int os, std::string loc);
	static int getTrampolineVersion();
	static int getTrampolineVersion(const std::string& path, bool silent = false);

	static bool ubuntuExtractImage(std::string name, std::string img_path, std::string dest);
	static bool patchUbuntuInit(std::string rootDir);
	static bool ubuntuUpdateInitramfs(std::string rootDir);
	static void setUpChroot(bool start, std::string rootDir);
	static void ubuntuDisableFlashKernel(bool initChroot, std::string rootDir);
	static bool mountUbuntuImage(std::string name, std::string& dest);

	static bool createImage(const std::string& base, const char *img, int size);
	static bool createImagesFromBase(const std::string& base);
	static bool createDirsFromBase(const std::string& base);
	static bool mountBaseImages(std::string base, std::string& dest);
	static void umountBaseImages(const std::string& base);
	static bool createFakeSystemImg();

	static int system_args(const char *fmt, ...);
	static void translateToRealdata(std::string& path);
	static bool calculateMD5(const char *path, unsigned char *md5sum/*len: 16*/);
	static void normalizeROMPath(std::string& path);
	static void restoreROMPath();

	static bool copyPartWithXAttrs(const std::string& src, const std::string& dst, const std::string& part, bool skipMedia = false);

	static std::string m_path;
	static std::string m_mount_rom_paths[2];
	static std::string m_curr_roms_path;
	static MROMInstaller *m_installer;
	static baseFolders m_base_folders;
	static int m_base_folder_cnt;
	static std::string m_boot_dev;
	static bool m_has_firmware;
};


#endif
