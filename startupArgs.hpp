/*
	Copyright 2012-2020 TeamWin
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

#ifndef STARTUPARGS_HPP
#define STARTUPARGS_HPP
#include <android-base/properties.h>

#include "data.hpp"
#include "gui/gui.hpp"
#include "openrecoveryscript.hpp"
#include "partitions.hpp"
#include "twcommon.h"
#include "twrp-functions.hpp"
#include "variables.h"
#include "bootloader_message/include/bootloader_message/bootloader_message.h"
#include "twinstall/get_args.h"

class startupArgs {
public:
	static inline std::string const UPDATE_PACKAGE = "--update_package";
	static inline std::string const WIPE_CACHE = "--wipe_cache";
	static inline std::string const WIPE_DATA = "--wipe_data";
	static inline std::string const SEND_INTENT = "--send_intent";
	static inline std::string const SIDELOAD = "--sideload";
	static inline std::string const REASON = "--reason";
	static inline std::string const FASTBOOT = "--fastboot";
	static inline std::string const NANDROID = "--nandroid";
	static inline std::string const RESCUE_PARTY = "--prompt_and_wipe_data";
	void parse(int *argc, char ***argv);
	bool Should_Skip_Decryption();
	std::string Get_Intent();
	bool Get_Fastboot_Mode();

private:
	bool SkipDecryption = false;
	bool fastboot_mode = false;
	std::string Send_Intent;
};
#endif
