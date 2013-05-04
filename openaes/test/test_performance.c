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
#include <time.h>

#include "oaes_lib.h"

void usage(const char * exe_name)
{
	if( NULL == exe_name )
		return;
	
	printf(
			"Usage:\n"
			"\t%s [-ecb] [-key < 128 | 192 | 256 >] [-data <data_len>]\n",
			exe_name
	);
}

/*
 * 
 */
int main(int argc, char** argv) {

	size_t _i, _j;
	time_t _time_start, _time_end;
	OAES_CTX * ctx = NULL;
	uint8_t *_encbuf, *_decbuf;
	size_t _encbuf_len, _decbuf_len;
	uint8_t _buf[1024 * 1024];
	short _is_ecb = 0;
	int _key_len = 128;
	size_t _data_len = 64;
	
	for( _i = 1; _i < argc; _i++ )
	{
		int _found = 0;
		
		if( 0 == strcmp( argv[_i], "-ecb" ) )
		{
			_found = 1;
			_is_ecb = 1;
		}
		
		if( 0 == strcmp( argv[_i], "-key" ) )
		{
			_found = 1;
			_i++; // key_len
			if( _i >= argc )
			{
				printf("Error: No value specified for '-%s'.\n",
						"key");
				usage( argv[0] );
				return 1;
			}
			_key_len = atoi( argv[_i] );
			switch( _key_len )
			{
				case 128:
				case 192:
				case 256:
					break;
				default:
					printf("Error: Invalid value [%d] specified for '-%s'.\n",
							_key_len, "key");
					usage( argv[0] );
					return 1;
			}
		}
		
		if( 0 == strcmp( argv[_i], "-data" ) )
		{
			_found = 1;
			_i++; // data_len
			if( _i >= argc )
			{
				printf("Error: No value specified for '-%s'.\n",
						"data");
				usage( argv[0] );
				return 1;
			}
			_data_len = atoi( argv[_i] );
		}
		
		if( 0 == _found )
		{
			printf("Error: Invalid option '%s'.\n", argv[_i]);
			usage( argv[0] );
			return 1;
		}			
	}

	// generate random test data
	time( &_time_start );
	srand( _time_start );
	for( _i = 0; _i < 1024 * 1024; _i++ )
		_buf[_i] = rand();
	
	ctx = oaes_alloc();
	if( NULL == ctx )
	{
		printf("Error: Failed to initialize OAES.\n");
		return EXIT_FAILURE;
	}
	if( _is_ecb )
		if( OAES_RET_SUCCESS != oaes_set_option( ctx, OAES_OPTION_ECB, NULL ) )
			printf("Error: Failed to set OAES options.\n");
	switch( _key_len )
	{
		case 128:
			if( OAES_RET_SUCCESS != oaes_key_gen_128(ctx) )
				printf("Error: Failed to generate OAES %d bit key.\n", _key_len);
			break;
		case 192:
			if( OAES_RET_SUCCESS != oaes_key_gen_192(ctx) )
				printf("Error: Failed to generate OAES %d bit key.\n", _key_len);
			break;
		case 256:
			if( OAES_RET_SUCCESS != oaes_key_gen_256(ctx) )
				printf("Error: Failed to generate OAES %d bit key.\n", _key_len);
			break;
		default:
			break;
	}

	if( OAES_RET_SUCCESS != oaes_encrypt( ctx,
			(const uint8_t *)_buf, 1024 * 1024, NULL, &_encbuf_len ) )
		printf("Error: Failed to retrieve required buffer size for encryption.\n");
	_encbuf = (uint8_t *) calloc( _encbuf_len, sizeof( char ) );
	if( NULL == _encbuf )
	{
		printf( "Error: Failed to allocate memory.\n" );
		return EXIT_FAILURE;
	}

	if( OAES_RET_SUCCESS != oaes_decrypt( ctx,
			_encbuf, _encbuf_len, NULL, &_decbuf_len ) )
		printf("Error: Failed to retrieve required buffer size for encryption.\n");
	_decbuf = (uint8_t *) calloc( _decbuf_len, sizeof( char ) );
	if( NULL == _decbuf )
	{
		free( _encbuf );
		printf( "Error: Failed to allocate memory.\n" );
		return EXIT_FAILURE;
	}

	time( &_time_start );
	
	for( _i = 0; _i < _data_len; _i++ )
	{
		if( OAES_RET_SUCCESS != oaes_encrypt( ctx,
				(const uint8_t *)_buf, 1024 * 1024, _encbuf, &_encbuf_len ) )
			printf("Error: Encryption failed.\n");
		if( OAES_RET_SUCCESS !=  oaes_decrypt( ctx,
				_encbuf, _encbuf_len, _decbuf, &_decbuf_len ) )
			printf("Error: Decryption failed.\n");
	}
	
	time( &_time_end );
	printf( "Test encrypt and decrypt:\n\ttime: %lld seconds\n\tdata: %ld MB"
			"\n\tkey: %d bits\n\tmode: %s\n",
			_time_end - _time_start, _data_len,
			_key_len, _is_ecb? "EBC" : "CBC" );
	free( _encbuf );
	free( _decbuf );
	if( OAES_RET_SUCCESS !=  oaes_free( &ctx ) )
		printf("Error: Failed to uninitialize OAES.\n");

	return (EXIT_SUCCESS);
}
