/* 
 * ---------------------------------------------------------------------------
 * OpenAES License
 * ---------------------------------------------------------------------------
 * Copyright (c) 2012, Nabil S. Al Ramli, www.nalramli.com
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OAES_DEBUG 1
#include "../inc/oaes_lib.h"

#if defined(_WIN32) && !defined(__SYMBIAN32__)
#include <io.h>
#else
__inline static int setmode(int a, int b)
{
	return 0;
}
#endif

#ifndef __max
	#define __max(a,b)  (((a) > (b)) ? (a) : (b))
#endif // __max

#ifndef __min
	#define __min(a,b)  (((a) < (b)) ? (a) : (b))
#endif // __min

#define OAES_BUF_LEN_ENC 4096 - 2 * OAES_BLOCK_SIZE
#define OAES_BUF_LEN_DEC 4096

static void usage( const char * exe_name )
{
	if( NULL == exe_name )
		return;
	
	fprintf( stderr,
			"Usage:\n"
			"  %s <command> --key <key_data> [options]\n"
			"\n"
			"    command:\n"
			"      enc: encrypt\n"
			"      dec:  decrypt\n"
			"\n"
			"    options:\n"
			"      --ecb: use ecb mode instead of cbc\n"
			"      --in <path_in>\n"
			"      --out <path_out>\n"
			"\n",
			exe_name
	);
}

int main(int argc, char** argv)
{
	size_t _i = 0, _j = 0;
	OAES_CTX * ctx = NULL;
	uint8_t _buf_in[OAES_BUF_LEN_DEC];
	uint8_t *_buf_out = NULL, _key_data[32] = "";
	size_t _buf_in_len = 0, _buf_out_len = 0, _read_len = 0;
	size_t _key_data_len = 0;
	short _is_ecb = 0;
	char *_file_in = NULL, *_file_out = NULL;
	int _op = 0;
	FILE *_f_in = stdin, *_f_out = stdout;
	
	fprintf( stderr, "\n"
		"*******************************************************************************\n"
		"* OpenAES %-10s                                                          *\n"
		"* Copyright (c) 2012, Nabil S. Al Ramli, www.nalramli.com                     *\n"
		"*******************************************************************************\n\n",
		OAES_VERSION );

	// pad the key
	for( _j = 0; _j < 32; _j++ )
		_key_data[_j] = _j + 1;

	if( argc < 2 )
	{
		usage( argv[0] );
		return EXIT_FAILURE;
	}

	if( 0 == strcmp( argv[1], "enc" ) )
	{
		_op = 0;
		_read_len = OAES_BUF_LEN_ENC;
	}
	else if( 0 == strcmp( argv[1], "dec" ) )
	{
		_op = 1;
		_read_len = OAES_BUF_LEN_DEC;
	}
	else
	{
		fprintf(stderr, "Error: Unknown command '%s'.", argv[1]);
		usage( argv[0] );
		return EXIT_FAILURE;
	}

	for( _i = 2; _i < argc; _i++ )
	{
		int _found = 0;

		if( 0 == strcmp( argv[_i], "--ecb" ) )
		{
			_found = 1;
			_is_ecb = 1;
		}
		
		if( 0 == strcmp( argv[_i], "--key" ) )
		{
			_found = 1;
			_i++; // key_data
			if( _i >= argc )
			{
				fprintf(stderr, "Error: No value specified for '%s'.\n",
						"--key");
				usage( argv[0] );
				return EXIT_FAILURE;
			}
			_key_data_len = strlen(argv[_i]);
			if( 16 >= _key_data_len )
				_key_data_len = 16;
			else if( 24 >= _key_data_len )
				_key_data_len = 24;
			else
				_key_data_len = 32;
			memcpy(_key_data, argv[_i], __min(32, strlen(argv[_i])));
		}
		
		if( 0 == strcmp( argv[_i], "--in" ) )
		{
			_found = 1;
			_i++; // path_in
			if( _i >= argc )
			{
				fprintf(stderr, "Error: No value specified for '%s'.\n",
						"--in");
				usage( argv[0] );
				return EXIT_FAILURE;
			}
			_file_in = argv[_i];
		}
		
		if( 0 == strcmp( argv[_i], "--out" ) )
		{
			_found = 1;
			_i++; // path_out
			if( _i >= argc )
			{
				fprintf(stderr, "Error: No value specified for '%s'.\n",
						"--out");
				usage( argv[0] );
				return EXIT_FAILURE;
			}
			_file_out = argv[_i];
		}
		
		if( 0 == _found )
		{
			fprintf(stderr, "Error: Invalid option '%s'.\n", argv[_i]);
			usage( argv[0] );
			return EXIT_FAILURE;
		}			
	}

	if( 0 == _key_data_len )
	{
		fprintf(stderr, "Error: --key must be specified.\n");
		return EXIT_FAILURE;
	}

	if( _file_in )
	{
		_f_in = fopen(_file_in, "rb");
		if( NULL == _f_in )
		{
			fprintf(stderr,
				"Error: Failed to open '-%s' for reading.\n", _file_in);
			return EXIT_FAILURE;
		}
	}
	else
	{
		if( setmode(fileno(stdin), 0x8000) < 0 )
			fprintf(stderr,"Error: Failed in setmode().\n");
		_f_in = stdin;
	}

	if( _file_out )
	{
		_f_out = fopen(_file_out, "wb");
		if( NULL == _f_out )
		{
			fprintf(stderr,
				"Error: Failed to open '-%s' for writing.\n", _file_out);
			if( _file_in )
				fclose(_f_in);
			return EXIT_FAILURE;
		}
	}
	else
	{
		if( setmode(fileno(stdout), 0x8000) < 0 )
			fprintf(stderr, "Error: Failed in setmode().\n");
		_f_out = stdout;
	}

	ctx = oaes_alloc();
	if( NULL == ctx )
	{
		fprintf(stderr, "Error: Failed to initialize OAES.\n");
		if( _file_in )
			fclose(_f_in);
		if( _file_out )
			fclose(_f_out);
		return EXIT_FAILURE;
	}
	if( _is_ecb )
		if( OAES_RET_SUCCESS != oaes_set_option( ctx, OAES_OPTION_ECB, NULL ) )
			fprintf(stderr, "Error: Failed to set OAES options.\n");

	oaes_key_import_data( ctx, _key_data, _key_data_len );

	while( _buf_in_len =
		fread(_buf_in, sizeof(uint8_t), _read_len, _f_in) )
	{
		switch(_op)
		{
		case 0:
			if( OAES_RET_SUCCESS != oaes_encrypt( ctx,
					_buf_in, _buf_in_len, NULL, &_buf_out_len ) )
				fprintf( stderr,
					"Error: Failed to retrieve required buffer size for "
					"encryption.\n" );
			_buf_out = (uint8_t *) calloc( _buf_out_len, sizeof( char ) );
			if( NULL == _buf_out )
			{
				fprintf(stderr,  "Error: Failed to allocate memory.\n" );
				if( _file_in )
					fclose(_f_in);
				if( _file_out )
					fclose(_f_out);
				return EXIT_FAILURE;
			}
			if( OAES_RET_SUCCESS != oaes_encrypt( ctx,
					_buf_in, _buf_in_len, _buf_out, &_buf_out_len ) )
				fprintf(stderr, "Error: Encryption failed.\n");
			fwrite(_buf_out, sizeof(uint8_t), _buf_out_len, _f_out);
			free(_buf_out);
			break;
		case 1:
			if( OAES_RET_SUCCESS != oaes_decrypt( ctx,
					_buf_in, _buf_in_len, NULL, &_buf_out_len ) )
				fprintf( stderr,
					"Error: Failed to retrieve required buffer size for "
					"encryption.\n" );
			_buf_out = (uint8_t *) calloc( _buf_out_len, sizeof( char ) );
			if( NULL == _buf_out )
			{
				fprintf(stderr,  "Error: Failed to allocate memory.\n" );
				free( _buf_out );
				if( _file_in )
					fclose(_f_in);
				if( _file_out )
					fclose(_f_out);
				return EXIT_FAILURE;
			}
			if( OAES_RET_SUCCESS != oaes_decrypt( ctx,
					_buf_in, _buf_in_len, _buf_out, &_buf_out_len ) )
				fprintf(stderr, "Error: Decryption failed.\n");
			fwrite(_buf_out, sizeof(uint8_t), _buf_out_len, _f_out);
			free(_buf_out);
			break;
		default:
			break;
		}
	}


	if( OAES_RET_SUCCESS !=  oaes_free( &ctx ) )
		fprintf(stderr, "Error: Failed to uninitialize OAES.\n");
	
	if( _file_in )
		fclose(_f_in);
	if( _file_out )
		fclose(_f_out);

	return (EXIT_SUCCESS);
}
