/*		Copyright TeamWin 2015
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <string>
#include <fstream>

#include "../orscmd/orscmd.h"
#include "../variables.h"
#include "../twcommon.h"

class twrpback {
public:
	twrpback(void);
	virtual ~twrpback(void);                                                 // deconstructor
	int backup(char *command);                                               // adb backup stream
	int restore(void);                                                       // adb restore stream
	void adblogwrite(std::string writemsg);                                  // adb debugging log function
	void close_backup_fds();                                                 // close backup resources
	void close_restore_fds();                                                // close restore resources

private:
	int read_fd;                                                             // ors input fd
	int write_fd;                                                            // ors operation fd
	int ors_fd;                                                              // ors output fd
	int adb_control_twrp_fd;                                                 // fd for bu to twrp communication
	int adb_control_bu_fd;                                                   // fd for twrp to bu communication
	int adb_read_fd;                                                         // adb read data stream
	int adb_write_fd;                                                        // adb write data stream
	int breakloop;                                                           // tells adb that operations are complete
	bool firstPart;                                                          // first partition in the stream
	char result[512];                                                        // store result of stream reads
	char cmd[512];                                                           // store result of commands
	char operation[512];                                                     // operation to send to ors
	std::ofstream adblogfile;                                                // adb stream log file
	void adbloginit(void);                                                   // setup adb log stream file
};
