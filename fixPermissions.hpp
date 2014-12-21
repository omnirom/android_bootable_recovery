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
#include <errno.h>
#include "gui/rapidxml.hpp"
#include "twrp-functions.hpp"

using namespace std;

class fixPermissions {
	public:
		fixPermissions();
		~fixPermissions();
		int fixPerms(bool enable_debug, bool remove_data_for_missing_apps);
		int fixContexts();
		int fixDataInternalContexts(void);

	private:
		int pchown(string fn, int puid, int pgid);
		int pchmod(string fn, mode_t mode);
		vector <string> listAllDirectories(string path);
		vector <string> listAllFiles(string path);
		void deletePackages();
		int getPackages(const string& packageFile);
		int fixApps();
		int fixAllFiles(string directory, int uid, int gid, mode_t file_perms);
		int fixDir(const string& dir, int diruid, int dirgid, mode_t dirmode, int fileuid, int filegid, mode_t filemode);
		int fixDataData(string dataDir);
		int restorecon(string entry, struct stat *sb);
		int fixDataDataContexts(void);
		int fixContextsRecursively(string path, int level);

		struct package {
			string pkgName;
			string codePath;
			string appDir;
			string dDir;
			int gid;
			int uid;
			package *next;
		};
		bool debug;
		bool remove_data;
		package* head;
};
