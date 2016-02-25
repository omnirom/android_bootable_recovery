/*
        Copyright 2013 bigbiff/Dees_Troy TeamWin
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

#ifndef _TARWRITE_HEADER
#define _TARWRITE_HEADER

void reinit_libtar_buffer();
void init_libtar_buffer(unsigned new_buff_size, int pipe_fd);
void free_libtar_buffer();
writefunc_t write_libtar_buffer(int fd, const void *buffer, size_t size);
void flush_libtar_buffer(int fd);

void init_libtar_no_buffer(int pipe_fd);
writefunc_t write_libtar_no_buffer(int fd, const void *buffer, size_t size);

#endif  // _TARWRITE_HEADER
