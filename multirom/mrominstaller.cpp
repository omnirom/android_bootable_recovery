#include <cstring>
#include <algorithm>
#include <limits>

// android does not have statvfs
//#include <sys/statvfs.h>
#include <sys/vfs.h>
#define statvfs statfs

#include "mrominstaller.h"
#include "../minzip/SysUtil.h"
#include "../minzip/Zip.h"
#include "multirom_Zip.h"
#include "../common.h"
#include "multirom.h"
#include "cutils/properties.h"


extern "C" {
#include "../twcommon.h"
}

#define INTERNAL_MEM_LOC_TXT "Internal memory"

MROMInstaller::MROMInstaller()
{
	m_type = ROM_UNKNOWN;
}

MROMInstaller::~MROMInstaller()
{

}

int gui_changePage(std::string newPage);
int MROMInstaller::destroyWithErrorMsg(const std::string& ex)
{
	delete this;
	DataManager::SetValue("tw_mrom_text1", ex);
	return gui_changePage("multirom_msg");
}

std::string MROMInstaller::open(const std::string& file)
{
	char* manifest = NULL;
	const ZipEntry *script_entry;
	ZipArchive zip;

	MemMapping map;
	if (sysMapFile(file.c_str(), &map) != 0) {
		LOGERR("Failed to sysMapFile '%s'\n", file.c_str());
		return false;
	}

	if (mzOpenZipArchive(map.addr, map.length, &zip) != 0)
		return "Failed to open installer file!";

	script_entry = mzFindZipEntry(&zip, "manifest.txt");
	if(!script_entry)
	{
		mzCloseZipArchive(&zip);
		sysReleaseMap(&map);
		return "Failed to find manifest.txt";
	}

	int res = read_data(&zip, script_entry, &manifest, NULL);

	mzCloseZipArchive(&zip);
	sysReleaseMap(&map);

	if(res < 0)
		return "Failed to read manifest.txt!";

	int line_cnt = 1;
	for(char *line = strtok(manifest, "\r\n"); line; line = strtok(NULL, "\r\n"), ++line_cnt)
	{
		if(line[0] == '#')
			continue;

		char *val = strchr(line, '=');
		if(!val)
			continue;

		std::string key = std::string(line, val-line);
		++val; // skip '=' char

		char *start = strchr(val, '"');
		char *end = strrchr(val, '"');

		if(!start || start == end || start+1 == end)
			gui_print("Line %d: failed to parse string\n", line_cnt);
		else
		{
			++start;
			m_vals[key] = std::string(start, end-start);
			LOGI("MROMInstaller: got tag %s=%s\n", key.c_str(), m_vals[key].c_str());
		}
	}

	free(manifest);

	static const char* needed[] = {
		"manifest_ver", "devices", "base_folders"
	};

	for(uint32_t i = 0; i < sizeof(needed)/sizeof(needed[0]); ++i)
	{
		std::map<std::string, std::string>::const_iterator itr = m_vals.find(needed[i]);
		if(itr == m_vals.end())
			return std::string("Required key not found in manifest: ") + needed[i];
	}

	m_file = file;
	return std::string();
}

int MROMInstaller::getIntValue(const std::string& name, int def) const
{
	std::map<std::string, std::string>::const_iterator itr = m_vals.find(name);
	if(itr == m_vals.end())
		return def;
	return atoi(itr->second.c_str());
}

std::string MROMInstaller::getValue(const std::string& name, std::string def) const
{
	std::map<std::string, std::string>::const_iterator itr = m_vals.find(name);
	if(itr == m_vals.end())
		return def;
	return itr->second;
}

int MROMInstaller::getStringList(std::vector<std::string>& list, const std::string& name) const
{
	std::string str = getValue(name);
	if(str.empty())
		return 0;

	size_t idx = 0, idx_next = 0, len;
	int cnt = 0;

	do
	{
		idx_next = str.find(' ', idx+1);
		if(idx != 0)
			++idx;

		len = std::min(idx_next, str.size()) - idx;

		list.push_back(str.substr(idx, len));
		idx = idx_next;
		++cnt;
	} while(idx != std::string::npos);

	if(cnt == 0)
	{
		++cnt;
		list.push_back(str);
	}

	return cnt;
}

std::string MROMInstaller::setInstallLoc(const std::string& loc, bool &images)
{
	if(loc.compare(INTERNAL_MEM_LOC_TXT) == 0)
	{
		if(getIntValue("enable_internal"))
		{
			images = false;
			m_type = ROM_INSTALLER_INTERNAL;
			return std::string();
		}
		else
			return "ROM can't be installed to internal memory!";
	}
	else
	{
		size_t start = loc.find('(');
		size_t end = loc.find(')');
		if(start == std::string::npos || end == std::string::npos)
			return "Failed to get filesystem of target location";

		++start;
		std::string fs = loc.substr(start, end-start);

		// try to be dir first
		std::vector<std::string> supported;
		getStringList(supported, "usb_dir_fs");
		if(std::find(supported.begin(), supported.end(), fs) != supported.end())
		{
			images = false;
			m_type = ROM_INSTALLER_USB_DIR;
			return std::string();
		}

		// now images
		supported.clear();
		getStringList(supported, "usb_img_fs");
		if(std::find(supported.begin(), supported.end(), fs) != supported.end())
		{
			images = true;
			m_type = ROM_INSTALLER_USB_IMG;
			return std::string();
		}
		return std::string("Install location has unsupported fs (") + fs + ")";
	}
	return std::string();
}

std::string MROMInstaller::checkDevices() const
{
	bool found = false;
	char device[255];

	if(property_get("ro.product.device", device, NULL) <= 0)
		return "Could not get ro.product.device property!";

	std::string list = getValue("devices");
	char *p = strtok((char*)list.c_str(), "  ");
	while(p && !found)
	{
		if(strcmp(device, p) == 0)
			found = true;
		p = strtok(NULL, " ");
	}

	if(!found)
		return std::string("device (") + device + ") not supported (allowed: " + list + ")!";

	return std::string();
}

std::string MROMInstaller::checkVersion() const
{
	int min_ver = getIntValue("min_mrom_ver", -1);
	if(min_ver == -1)
		return std::string();

	char cmd[256];
	sprintf(cmd, "rm /tmp/ver; sh -c \"%s/multirom -v > /tmp/ver\"", MultiROM::getPath().c_str());
	system(cmd);

	FILE *f = fopen("/tmp/ver", "r");
	if(!f)
		return "Failed to check MultiROM version!";

	fgets(cmd, sizeof(cmd), f);
	fclose(f);

	int ver = atoi(cmd);
	if(ver < min_ver)
	{
		sprintf(cmd, "Required ver: %d, curr ver: %d", min_ver, ver);
		return std::string(cmd);
	}

	return std::string();
}

std::string MROMInstaller::parseBaseFolders(bool ntfs)
{
	MultiROM::clearBaseFolders();

	char *itr, *itr2, *p, *t;
	base_folder b;
	std::string s = getValue("base_folders");

	p = strtok_r((char*)s.c_str(), " ", &itr);
	for(int i = 0; p; ++i)
	{
		t = strtok_r(p, ":", &itr2);
		for(int x = 0; t; ++x)
		{
			switch(x)
			{
				case 0:
					b.name = t;
					break;
				case 1:
					b.min_size = std::min(atoi(t), ntfs ? INT_MAX : 4095);
					break;
				case 2:
					b.size = std::min(atoi(t), ntfs ? INT_MAX : 4095);
					MultiROM::addBaseFolder(b);
					break;
			}
			t = strtok_r(NULL, ":", &itr2);
		}
		p = strtok_r(NULL, " ", &itr);
	}

	if(MultiROM::getBaseFolders().size() > MAX_BASE_FOLDER_CNT)
		return "Too many base folders!";

	return std::string();
}

bool MROMInstaller::extractDir(const std::string& name, const std::string& dest)
{
	ZipArchive zip;

	MemMapping map;
	if (sysMapFile(m_file.c_str(), &map) != 0) {
		LOGERR("Failed to sysMapFile '%s'\n", m_file.c_str());
		return false;
	}

	if (mzOpenZipArchive(map.addr, map.length, &zip) != 0)
	{
		gui_print("Failed to open ZIP file %s\n", m_file.c_str());
		sysReleaseMap(&map);
		return false;
	}

	// To create a consistent system image, never use the clock for timestamps.
    struct utimbuf timestamp = { 1217592000, 1217592000 };  // 8/1/2008 default
	bool success = mzExtractRecursive(&zip, name.c_str(), dest.c_str(), &timestamp, NULL, NULL, NULL);

	mzCloseZipArchive(&zip);
	sysReleaseMap(&map);

	if(!success)
	{
		gui_print("Failed to extract dir %s from zip %s\n", name.c_str(), m_file.c_str());
		return false;
	}
	return true;
}

bool MROMInstaller::extractFile(const std::string& name, const std::string& dest)
{
	ZipArchive zip;
	MemMapping map;
	if (sysMapFile(m_file.c_str(), &map) != 0) {
		LOGERR("Failed to sysMapFile '%s'\n", m_file.c_str());
		return false;
	}

	if (mzOpenZipArchive(map.addr, map.length, &zip) != 0)
	{
		gui_print("Failed to open ZIP file %s\n", m_file.c_str());
		sysReleaseMap(&map);
		return false;
	}

	bool res = false;
	FILE* f = NULL;
	const ZipEntry* entry = mzFindZipEntry(&zip, name.c_str());
	if (entry == NULL)
	{
		gui_print("Could not find file %s in zip %s\n", name.c_str(), m_file.c_str());
		goto exit;
	}

	f = fopen(dest.c_str(), "wb");
	if(!f)
	{
		gui_print("Could not open dest file %s for writing!\n", dest.c_str());
		goto exit;
	}

	res = mzExtractZipEntryToFile(&zip, entry, fileno(f));
	if(!res)
		gui_print("Failed to extract file %s from the ZIP!", name.c_str());

	fclose(f);
exit:
	mzCloseZipArchive(&zip);
	sysReleaseMap(&map);
	return res;
}

bool MROMInstaller::hasEntry(const std::string& name)
{
	ZipArchive zip;
	MemMapping map;
	if (sysMapFile(m_file.c_str(), &map) != 0) {
		LOGERR("Failed to sysMapFile '%s'\n", m_file.c_str());
		return false;
	}

	if (mzOpenZipArchive(map.addr, map.length, &zip) != 0)
	{
		gui_print("Failed to open ZIP file %s\n", m_file.c_str());
		sysReleaseMap(&map);
		return false;
	}

	// Check also for entry with / - according to minzip, folders
	// usually (but not always) end with /
	const ZipEntry *entry1 = mzFindZipEntry(&zip, name.c_str());
	const ZipEntry *entry2 = mzFindZipEntry(&zip, (name + "/").c_str());

	mzCloseZipArchive(&zip);
	sysReleaseMap(&map);

	return entry1 || entry2;
}

bool MROMInstaller::runScripts(const std::string& dir, const std::string& base, const std::string& root)
{
	system("rm -r /tmp/script; mkdir /tmp/script");

	if(!hasEntry(dir))
	{
		LOGI("Skippping scripts in %s, not in the ZIP\n", dir.c_str());
		return true;
	}

	if(!extractDir(dir, "/tmp/script/"))
		return false;

	if(system("ls /tmp/script/*.sh") == 0)
	{
		system("chmod -R 777 /tmp/script/*");

		gui_print("Running %s scripts...\n", dir.c_str());

		char cmd[512];
		sprintf(cmd, "sh -c 'for x in $(ls /tmp/script/*.sh); do echo Running script $x; sh $x %s %s || exit 1; done'", base.c_str(), root.c_str());
		if(system(cmd) != 0)
		{
			system("rm -r /tmp/script/");
			gui_print("One of the ROM scripts returned error status!");
			return false;
		}
	}
	else
		LOGI("Skipping folder %s, no bash scripts\n", dir.c_str());

	system("rm -r /tmp/script");
	return true;
}

bool MROMInstaller::extractTarballs(const std::string& base)
{
	system("rm -r /tmp/tarballs; mkdir /tmp/tarballs");

	if(!hasEntry("rom"))
	{
		gui_print("Skippping tarball extractions - no rom folder in the ZIP file\n");
		return true;
	}

	bool res = false;
	const MultiROM::baseFolders& folders = MultiROM::getBaseFolders();
	MultiROM::baseFolders::const_iterator itr;
	for(itr = folders.begin(); itr != folders.end(); ++itr)
	{
		if(!extractBase(base, itr->first))
		{
			gui_print("Failed to extract base %s\n", itr->first.c_str());
			goto exit;
		}
	}

	res = true;
exit:
	system("rm -r /tmp/tarballs");
	return res;
}

bool MROMInstaller::extractBase(const std::string& base, const std::string& name)
{
	char cmd[256];
	sprintf(cmd, "rom/%s.tar.gz", name.c_str());

	if(hasEntry(cmd))
		return extractTarball(base, name, cmd);
	else
	{
		char cmd[256];
		for(int i = 0; true; ++i)
		{
			sprintf(cmd, "rom/%s_%02d.tar.gz", name.c_str(), i);
			if(!hasEntry(cmd))
				break;

			if(!extractTarball(base, name, cmd))
				return false;
		}
	}

	return true;
}

bool MROMInstaller::extractTarball(const std::string& base, const std::string& name, const std::string& tarball)
{
	gui_print("Extrating tarball %s...\n", tarball.c_str());

	system("rm /tmp/tarballs/rom.tar.gz");
	if(!extractFile(tarball, "/tmp/tarballs/rom.tar.gz"))
		return false;

	bool res = true;

	char cmd[256];
	sprintf(cmd, "gnutar --numeric-owner --overwrite -C \"%s\" -xf /tmp/tarballs/rom.tar.gz", (base + "/" + name).c_str());
	if(system(cmd) != 0)
	{
		gui_print("Failed to extract tarball %s for folder %s!\n", tarball.c_str(), name.c_str());
		res = false;
	}

	system("rm /tmp/tarballs/rom.tar.gz");
	return res;
}

bool MROMInstaller::checkFreeSpace(const std::string& base, bool images)
{
	struct statvfs s;
	int res = statvfs(base.c_str(), &s);
	if(res < 0)
	{
		gui_print("Check for free space failed: %s %d %d %s!\n", base.c_str(), res, errno, strerror(errno));
		return false;
	}

	int free = (s.f_bavail * (s.f_bsize/1024) ) / 1024;
	int req = 0;

	const MultiROM::baseFolders& folders = MultiROM::getBaseFolders();
	MultiROM::baseFolders::const_iterator itr;
	for(itr = folders.begin(); itr != folders.end(); ++itr)
	{
		if(images)
			req += itr->second.size;
		else
			req += itr->second.min_size;
	}

	gui_print("Free space check: Required: %d MB, free: %d MB\n", req, free);
	if(free < req)
		LOGERR("Not enough free space!\n");

	return free >= req;
}
