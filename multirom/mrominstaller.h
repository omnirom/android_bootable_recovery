#ifndef MROM_INSTALLER_H
#define MROM_INSTALLER_H

#include <map>
#include <string>
#include <vector>

class MROMInstaller
{
public:
	MROMInstaller();
	~MROMInstaller();

	int destroyWithErrorMsg(const std::string& ex);

	std::string open(const std::string& file);
	std::string setInstallLoc(const std::string& loc, bool &images);
	std::string checkDevices() const;
	std::string checkVersion() const;
	std::string parseBaseFolders(bool ntfs);
	bool checkFreeSpace(const std::string& base, bool images);

	int getRomType() const { return m_type; }

	int getIntValue(const std::string& name, int def = 0) const;
	std::string getValue(const std::string& name, std::string def = "") const;
	int getStringList(std::vector<std::string>& list, const std::string& name) const;

	bool runScripts(const std::string& dir, const std::string& base, const std::string& root);
	bool extractDir(const std::string& name, const std::string& dest);
	bool extractFile(const std::string& name, const std::string& dest);
	bool extractTarballs(const std::string& base);

private:
	bool hasEntry(const std::string& name);
	bool extractBase(const std::string& base, const std::string& name);
	bool extractTarball(const std::string& base, const std::string& name, const std::string& tarball);

	std::map<std::string, std::string> m_vals;
	std::string m_file;
	int m_type;
};

#endif