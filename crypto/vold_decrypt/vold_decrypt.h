/*
    Copyright 2017 TeamWin
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

#ifndef _VOLD_DECRYPT_H
#define _VOLD_DECRYPT_H

#include <string>

// -_-
enum {
	VD_SUCCESS                      = 0,
	VD_ERR_DECRYPTION_FAILED        = -1,
	VD_ERR_UNABLE_TO_MOUNT_SYSTEM   = -2,
	VD_ERR_MISSING_VOLD             = -3,
	VD_ERR_MISSING_VDC              = -4,
	VD_ERR_VDC_FAILED_TO_CONNECT    = -5,
	VD_ERR_VOLD_FAILED_TO_START     = -6,
	VD_ERR_VOLD_UNEXPECTED_RESPONSE = -7,
	VD_ERR_VOLD_OPERATION_TIMEDOUT  = -8,
	VD_ERR_FORK_EXECL_ERROR         = -9,
	VD_ERR_PASSWORD_EMPTY           = -10,
};


int vold_decrypt(const std::string& Password);

#endif // _VOLD_DECRYPT_H
