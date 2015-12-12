/*
	Copyright 2012 bigbiff/Dees_Troy TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _OPENRECOVERYSCRIPT_HPP
#define _OPENRECOVERYSCRIPT_HPP

#include <string>

using namespace std;

class OpenRecoveryScript
{
	typedef void (*VoidFunction)();
	static VoidFunction call_after_cli_command;                                    // callback to GUI after Run_CLI_Command

	static int check_for_script_file();                                            // Checks to see if the ORS file is present in /cache
	static int copy_script_file(string filename);                                  // Copies a script file to the temp folder
	static int run_script_file();                                                  // Executes the commands in the ORS file
	static int Install_Command(string Zip);                                        // Installs a zip
	static string Locate_Zip_File(string Path, string File);                       // Attempts to locate the zip file in storage
	static int Backup_Command(string Options);                                     // Runs a backup
public:
	static int Insert_ORS_Command(string Command);                                 // Inserts the Command into the SCRIPT_FILE_TMP file
	static void Run_OpenRecoveryScript();                                          // Starts the GUI Page for running OpenRecoveryScript
	static int Run_OpenRecoveryScript_Action();                                    // Actually runs the ORS scripts for the GUI action
	static void Call_After_CLI_Command(VoidFunction fn) { call_after_cli_command = fn; }
	static void Run_CLI_Command(const char* command);                              // Runs a command for orscmd (twrp binary)
	static int Backup_ADB_Command(string Options);                                 // Runs adbbackup
	static int Restore_ADB_Backup();                                               // Restore adb backup through ors
	static int remountrw();                                                        // Remount system and vendor rw
};

#endif // _OPENRECOVERYSCRIPT_HPP
