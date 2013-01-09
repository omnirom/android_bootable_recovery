#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include "gui/rapidxml.hpp"
#include "twrp-functions.hpp"

using namespace std;

class fixPermissions {
	public:
		int fixPerms(bool enable_debug, bool remove_data_for_missing_apps);

	private:
		int pchown(std::string fn, int puid, int pgid);
		int pchmod(std::string fn, string mode);
		vector <string> listAllDirectories(std::string path);
		vector <string> listAllFiles(std::string path);
		int getPackages();
		int fixSystemApps();
		int fixDataApps();
		int fixAllFiles(string directory, int gid, int uid, string file_perms);
		int fixDataData(string dataDir);
		struct package {
			string pkgName;
			string codePath;
			string appDir;
			string app;
			string dDir;
			int gid;
			int uid;
			package *next;
		};
		bool debug;
		bool remove_data;
		bool multi_user;
		package* head;
		package* temp;		
		string packageFile;
};
