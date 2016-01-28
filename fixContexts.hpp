/*
	Copyright 2012-2016 bigbiff/Dees_Troy TeamWin
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

#ifndef __FIXCONTEXTS_HPP
#define __FIXCONTEXTS_HPP

#include <string>

using namespace std;

class fixContexts {
	public:
		static int fixDataMediaContexts(string Mount_Point);

	private:
		static int restorecon(string entry, struct stat *sb);
		static int fixContextsRecursively(string path, int level);
};

#endif
