#ifndef _TWRPFUNCTIONS_HPP
#define _TWRPFUNCTIONS_HPP

#include <string>
#include <vector>

using namespace std;

typedef enum
{
    rb_current = 0,
    rb_system,
    rb_recovery,
    rb_poweroff,
    rb_bootloader,     // May also be fastboot
    rb_download,
} RebootCommand;

// Partition class
class TWFunc
{
public:
	static string Get_Root_Path(string Path);                                   // Trims any trailing folders or filenames from the path, also adds a leading / if not present
	static string Get_Path(string Path);                                        // Trims everything after the last / in the string
	static string Get_Filename(string Path);                                    // Trims the path off of a filename

	static void install_htc_dumlock(void);                                      // Installs HTC Dumlock
	static void htc_dumlock_restore_original_boot(void);                        // Restores the backup of boot from HTC Dumlock
	static void htc_dumlock_reflash_recovery_to_boot(void);                     // Reflashes the current recovery to boot
	static int Recursive_Mkdir(string Path);                                    // Recursively makes the entire path
	static unsigned long long Get_Folder_Size(const string& Path, bool Display_Error); // Gets the size of a folder and all of its subfolders using dirent and stat
	static bool Path_Exists(string Path);                                       // Returns true if the path exists
	static void GUI_Operation_Text(string Read_Value, string Default_Text);     // Updates text for display in the GUI, e.g. Backing up %partition name%
	static void GUI_Operation_Text(string Read_Value, string Partition_Name, string Default_Text); // Same as above but includes partition name
	static unsigned long Get_File_Size(string Path);                            // Returns the size of a file
	static void Update_Log_File(void);                                          // Writes the log to last_log
	static void Update_Intent_File(string Intent);                              // Updates intent file
	static int tw_reboot(RebootCommand command);                                // Prepares the device for rebooting
	static void check_and_run_script(const char* script_file, const char* display_name); // checks for the existence of a script, chmods it to 755, then runs it
	static int Exec_Cmd(string cmd, string &result); //execute a command and return the result as a string by reference
	static int removeDir(const string path, bool removeParent); //recursively remove a directory
	static int copy_file(string src, string dst, int mode); //copy file from src to dst with mode permissions
	static unsigned int Get_D_Type_From_Stat(string Path);                      // Returns a dirent dt_type value using stat instead of dirent
	static timespec timespec_diff(timespec& start, timespec& end);	            // Return a diff for 2 times
	static int read_file(string fn, vector<string>& results); //read from file
	static int read_file(string fn, string& results); //read from file
	static int write_file(string fn, string& line); //write from file
	static int drop_caches(void); //drop linux cache memory
	static int Check_su_Perms(void); // check perms and owner of su binary in various locations
	static bool Fix_su_Perms(void); // sets proper permissions for su binaries and superuser apk
	static int tw_chmod(string fn, string mode); // chmod function that converts a 4 char string into st_mode automatically
	static bool Install_SuperSU(void); // Installs su binary and apk and sets proper permissions

private:
	static void Copy_Log(string Source, string Destination);

};

extern int Log_Offset;

#endif // _TWRPFUNCTIONS_HPP
