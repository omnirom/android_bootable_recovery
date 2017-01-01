/* 
 * ---------------------------------------------------------------------------
 * OpenAES License
 * ---------------------------------------------------------------------------
 * Copyright (c) 2013, Nabil S. Al Ramli, www.nalramli.com
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *   - Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   - Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * ---------------------------------------------------------------------------
 */
static const char _NR[] = {
  0x4e,0x61,0x62,0x69,0x6c,0x20,0x53,0x2e,0x20,
  0x41,0x6c,0x20,0x52,0x61,0x6d,0x6c,0x69,0x00 };

#include <stdlib.h>
#include <string.h>

#include "oaes_config.h"
#include "oaes_base64.h"

static const char _oaes_base64_table[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

OAES_RET oaes_base64_encode(
  const uint8_t *in, size_t in_len, char *out, size_t *out_len)
{
  size_t _i = 0, _j = 0;
  unsigned char _buf1[3];
  unsigned char _buf2[4];
  size_t _out_len_req = 4 * ( in_len / 3 + ( in_len % 3 ? 1 : 0 ) ) + 1;

  if( NULL == in || 0 == in_len || NULL == out_len )
    return OAES_RET_ERROR;

  if( NULL == out )
  {
      *out_len = _out_len_req;
      return OAES_RET_SUCCESS;
  }

  if( _out_len_req > *out_len )
      return OAES_RET_ERROR;

  memset(out, 0, *out_len);
  *out_len = 0;
  while( in_len-- )
  {
    _buf1[_i++] = *(in++);
    if( _i == 3 )
    {
      _buf2[0] = (_buf1[0] & 0xfc) >> 2;
      _buf2[1] = ((_buf1[0] & 0x03) << 4) + ((_buf1[1] & 0xf0) >> 4);
      _buf2[2] = ((_buf1[1] & 0x0f) << 2) + ((_buf1[2] & 0xc0) >> 6);
      _buf2[3] = _buf1[2] & 0x3f;

      for( _i = 0; _i < 4; _i++ )
      {
        *(out++) = _oaes_base64_table[_buf2[_i]];
        (*out_len)++;
      }
      _i = 0;
    }
  }

  if( _i )
  {
    for( _j = _i; _j < 3; _j++ )
      _buf1[_j] = '\0';

    _buf2[0] = (_buf1[0] & 0xfc) >> 2;
    _buf2[1] = ((_buf1[0] & 0x03) << 4) + ((_buf1[1] & 0xf0) >> 4);
    _buf2[2] = ((_buf1[1] & 0x0f) << 2) + ((_buf1[2] & 0xc0) >> 6);
    _buf2[3] = _buf1[2] & 0x3f;

    for( _j = 0; (_j < _i + 1); _j++ )
    {
      *(out++) = _oaes_base64_table[_buf2[_j]];
      (*out_len)++;
    }

    while( _i++ < 3 )
    {
      *(out++) = '=';
      (*out_len)++;
    }
  }

  return OAES_RET_SUCCESS;

}

OAES_RET oaes_base64_decode(
  const char *in, size_t in_len, uint8_t *out, size_t *out_len )
{
  size_t _i = 0, _j = 0, _idx = 0;
  uint8_t _buf2[4], _buf1[3];
  size_t _out_len_req = 3 * ( in_len / 4 + ( in_len % 4 ? 1 : 0 ) );

  if( NULL == in || 0 == in_len || NULL == out_len )
    return OAES_RET_ERROR;

  if( NULL == out )
  {
      *out_len = _out_len_req;
      return OAES_RET_SUCCESS;
  }

  if( _out_len_req > *out_len )
      return OAES_RET_ERROR;

  memset(out, 0, *out_len);
  *out_len = 0;
  while( in_len-- && strchr(_oaes_base64_table, in[_idx++]) )
  {
    _buf2[_i++] = in[_idx - 1];
    if( _i ==4 )
    {
      for (_i = 0; _i < 4; _i++)
        _buf2[_i] = strchr(_oaes_base64_table, _buf2[_i]) - _oaes_base64_table;

      _buf1[0] = (_buf2[0] << 2) + ((_buf2[1] & 0x30) >> 4);
      _buf1[1] = ((_buf2[1] & 0xf) << 4) + ((_buf2[2] & 0x3c) >> 2);
      _buf1[2] = ((_buf2[2] & 0x3) << 6) + _buf2[3];

      for( _i = 0; (_i < 3); _i++ )
      {
        *(out++) = _buf1[_i];
        (*out_len)++;
      }
      _i = 0;
    }
  }

  if( _i )
  {
    for( _j = _i; _j <4; _j++ )
      _buf2[_j] = 0;

    for( _j = 0; _j <4; _j++ )
      _buf2[_j] = strchr(_oaes_base64_table, _buf2[_j]) - _oaes_base64_table;

    _buf1[0] = (_buf2[0] << 2) + ((_buf2[1] & 0x30) >> 4);
    _buf1[1] = ((_buf2[1] & 0xf) << 4) + ((_buf2[2] & 0x3c) >> 2);
    _buf1[2] = ((_buf2[2] & 0x3) << 6) + _buf2[3];

    for( _j = 0; (_j < _i - 1); _j++ )
    {
        *(out++) = _buf1[_j];
        (*out_len)++;
    }
  }

  return OAES_RET_SUCCESS;
}
