/*
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

#include "orscmd.h"
#include "../variables.h"
#include "../twcommon.h"

class twrpback {
public:
	twrpback(void);
	virtual ~twrpback(void);
	int backup(char *command);
	int restore(void);

private:
	int read_fd;
	int write_fd;
	int ors_fd;
	int adb_control_fd;
	int adb_read_fd;
	int adb_write_fd;
	int breakloop;
	char result[512];
	char cmd[512];
	char operation[512];
	FILE* twadblog;
	void adblogwrite(char* msg);
	void adbloginit(void);
};
