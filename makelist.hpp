/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * The code was written from scratch by Dees_Troy dees_troy at
 * yahoo
 *
 * Copyright (c) 2012
*/
#ifndef _MAKELIST_HEADER
#define _MAKELIST_HEADER

#include <string>

using namespace std;

// Partition class
class MakeList
{
public:
	static int Make_File_List(string Path);

private:
	static int Add_Item(string Item_Name);
	static int Generate_File_Lists(string Path);

};

#endif // _MAKELIST_HEADER

