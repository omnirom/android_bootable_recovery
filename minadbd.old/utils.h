/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _ADB_UTILS_H
#define _ADB_UTILS_H

/* bounded buffer functions */

/* all these functions are used to append data to a bounded buffer.
 *
 * after each operation, the buffer is guaranteed to be zero-terminated,
 * even in the case of an overflow. they all return the new buffer position
 * which allows one to use them in succession, only checking for overflows
 * at the end. For example:
 *
 *    BUFF_DECL(temp,p,end,1024);
 *    char*    p;
 *
 *    p = buff_addc(temp, end, '"');
 *    p = buff_adds(temp, end, string);
 *    p = buff_addc(temp, end, '"');
 *
 *    if (p >= end) {
 *        overflow detected. note that 'temp' is
 *        zero-terminated for safety. 
 *    }
 *    return strdup(temp);
 */

/* tries to add a character to the buffer, in case of overflow
 * this will only write a terminating zero and return buffEnd.
 */
char*   buff_addc (char*  buff, char*  buffEnd, int  c);

/* tries to add a string to the buffer */
char*   buff_adds (char*  buff, char*  buffEnd, const char*  s);

/* tries to add a bytes to the buffer. the input can contain zero bytes,
 * but a terminating zero will always be appended at the end anyway
 */
char*   buff_addb (char*  buff, char*  buffEnd, const void*  data, int  len);

/* tries to add a formatted string to a bounded buffer */
char*   buff_add  (char*  buff, char*  buffEnd, const char*  format, ... );

/* convenience macro used to define a bounded buffer, as well as
 * a 'cursor' and 'end' variables all in one go.
 *
 * note: this doesn't place an initial terminating zero in the buffer,
 * you need to use one of the buff_ functions for this. or simply
 * do _cursor[0] = 0 manually.
 */
#define  BUFF_DECL(_buff,_cursor,_end,_size)   \
    char   _buff[_size], *_cursor=_buff, *_end = _cursor + (_size)

#endif /* _ADB_UTILS_H */
