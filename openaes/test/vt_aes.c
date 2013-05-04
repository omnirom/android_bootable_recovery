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
#include "oaes_lib.h"

static int _is_step = 1;

static int step_cb(
		const uint8_t state[OAES_BLOCK_SIZE],
		const char * step_name,
		int step_count,
		void * user_data )
{
	size_t _buf_len;
	char * _buf;


	if( NULL == state )
		return 1;

	oaes_sprintf( NULL, &_buf_len, state, OAES_BLOCK_SIZE );
	_buf = (char *) calloc( _buf_len, sizeof( char ) );

	if( _buf )
	{
		oaes_sprintf( _buf, &_buf_len, state, OAES_BLOCK_SIZE );
		printf( "round[%2d].%-7s --> %s", step_count, step_name, _buf );
		free( _buf );
	}
	
	if( 1 == _is_step && '\n' != getchar( ) )
		_is_step = 0;

	return 0;
}

static int to_binary(uint8_t * buf, size_t * buf_len, const char * data)
{
	size_t _i, _buf_len_in;
	
	if( NULL == buf_len )
		return 1;

	if( NULL == data )
		return 1;

	_buf_len_in = *buf_len;
	*buf_len = strlen( data ) / 2;
	
	if( NULL == buf )
		return 0;

	if( *buf_len > _buf_len_in )
		return 1;

	memset( buf, 0, strlen( data ) / 2 );
	
	// lookup ascii table
	for( _i = 0; _i < strlen( data ); _i++ )
	{
		// 0-9
		if( data[_i] >= 0x30 && data[_i] <= 0x39 )
			buf[_i / 2] += ( data[_i] - 0x30 ) << ( 4 * ( ( _i + 1 ) % 2 ) ) ;
		// a-f
		else if( data[_i] >= 0x41 && data[_i] <= 0x46 )
			buf[_i / 2] += ( data[_i] - 0x37 ) << ( 4 * ( ( _i + 1 ) % 2 ) );
		// A-F
		else if( data[_i] >= 0x61 && data[_i] <= 0x66 )
			buf[_i / 2] += ( data[_i] - 0x57 ) << ( 4 * ( ( _i + 1 ) % 2 ) );
		// invalid character
		else
			return 1;
	}
	
	return 0;
}

static void usage(const char * exe_name)
{
	if( NULL == exe_name )
		return;
	
	printf(
			"Usage:\n"
			"  %s [-step] [-ecb] [[-key < 128 | 192 | 256 | key_data >] [-bin] <text>\n",
			exe_name
	);
}

int main(int argc, char** argv)
{
	size_t _i;
	OAES_CTX * ctx = NULL;
	uint8_t *_encbuf, *_decbuf, *_key_data = NULL, *_bin_data = NULL;
	size_t _encbuf_len, _decbuf_len, _buf_len;
	size_t _key_data_len = 0, _bin_data_len = 0;
	char *_buf;
	short _is_ecb = 0, _is_bin = 0;
	char * _text = NULL, * _key_text = NULL;
	int _key_len = 128;
	
	if( argc < 2 )
	{
		usage( argv[0] );
		return EXIT_FAILURE;
	}

	for( _i = 1; _i < argc; _i++ )
	{
		int _found = 0;

		if( 0 == strcmp( argv[_i], "-nostep" ) )
		{
			_found = 1;
			_is_step = 0;
		}

		if( 0 == strcmp( argv[_i], "-ecb" ) )
		{
			_found = 1;
			_is_ecb = 1;
		}
		
		if( 0 == strcmp( argv[_i], "-bin" ) )
		{
			_found = 1;
			_is_bin = 1;
		}
		
		if( 0 == strcmp( argv[_i], "-key" ) )
		{
			_found = 1;
			_i++; // len
			if( _i >= argc )
			{
				printf("Error: No value specified for '-%s'.\n",
						"key");
				usage( argv[0] );
				return EXIT_FAILURE;
			}
			_key_len = atoi( argv[_i] );
			switch( _key_len )
			{
				case 128:
				case 192:
				case 256:
					break;
				default:
					_key_text = argv[_i];
					if( to_binary( NULL, &_key_data_len, _key_text ) )
					{
						printf( "Error: Invalid value [%s] specified for '-%s'.\n",
								argv[_i], "key" );
						return EXIT_FAILURE;
					}
					switch( _key_data_len )
					{
						case 16:
						case 24:
						case 32:
							break;
						default:
							printf("Error: key_data [%s] specified for '-%s' has an invalid "
									"size.\n", argv[_i], "key");
							usage( argv[0] );
							return EXIT_FAILURE;
					}
			}
		}
		
		if( 0 == _found )
		{
			if( _text )
			{
				printf("Error: Invalid option '%s'.\n", argv[_i]);
				usage( argv[0] );
				return EXIT_FAILURE;
			}
			else
			{
				_text = argv[_i];
				if( _is_bin && to_binary( NULL, &_bin_data_len, _text ) )
				{
					printf( "Error: Invalid value [%s] specified for '-%s'.\n",
							argv[_i], "bin" );
					return EXIT_FAILURE;
				}
			}
		}			
	}

	if( NULL == _text )
	{
		usage( argv[0] );
		return EXIT_FAILURE;
	}

	if( _is_step )
		printf( "\nEnabling step mode, press Return to step.\n\n" );

	if( _is_bin )
	{
		_bin_data = (uint8_t *) calloc(_bin_data_len, sizeof(uint8_t));
		if( NULL == _bin_data )
		{
			printf( "Error: Failed to allocate memory.\n" );
			return EXIT_FAILURE;
		}
		if( to_binary( _bin_data, &_bin_data_len, _text ) )
		{
			printf( "Error: Could not load data [%s].\n", _text);
			free( _bin_data );
			return EXIT_FAILURE;
		}
	}
	else
	{
		oaes_sprintf( NULL, &_buf_len, (const uint8_t *)_text, strlen(_text));
		_buf = (char *) calloc(_buf_len, sizeof(char));
		printf( "\n***** plaintext  *****\n" );
		if( _buf )
		{
			oaes_sprintf( _buf, &_buf_len,
					(const uint8_t *)_text, strlen( _text ) );
			printf( "%s", _buf );
		}
		printf( "\n**********************\n" );
		free( _buf );
	}
	
	ctx = oaes_alloc();
	if( NULL == ctx )
	{
		printf("Error: Failed to initialize OAES.\n");
		if( _bin_data )
			free( _bin_data );
		return EXIT_FAILURE;
	}
	if( OAES_RET_SUCCESS != oaes_set_option( ctx, OAES_OPTION_STEP_ON, step_cb ) )
		printf("Error: Failed to set OAES options.\n");
	if( _is_ecb )
		if( OAES_RET_SUCCESS != oaes_set_option( ctx, OAES_OPTION_ECB, NULL ) )
			printf("Error: Failed to set OAES options.\n");

	if( _key_text )
	{
		_key_data = (uint8_t *) calloc(_key_data_len, sizeof(uint8_t));
		if( NULL == _key_data )
		{
			printf( "Error: Failed to allocate memory.\n" );
			if( _bin_data )
				free( _bin_data );
			return EXIT_FAILURE;
		}
		if( to_binary( _key_data, &_key_data_len, _key_text ) )
		{
			printf( "Error: Could not load key [%s].\n", _key_text);
			free( _key_data );
			return EXIT_FAILURE;
		}
		oaes_key_import_data( ctx, _key_data, _key_data_len );
	}
	else
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

	if( _bin_data )
	{
		if( OAES_RET_SUCCESS != oaes_encrypt( ctx,
				_bin_data, _bin_data_len, NULL, &_encbuf_len ) )
			printf("Error: Failed to retrieve required buffer size for encryption.\n");
		_encbuf = (uint8_t *) calloc(_encbuf_len, sizeof(uint8_t));
		if( NULL == _encbuf )
		{
			printf( "Error: Failed to allocate memory.\n" );
			if( _key_data )
				free( _key_data );
			free( _bin_data );
			return EXIT_FAILURE;
		}
		printf( "\n" );
		if( OAES_RET_SUCCESS != oaes_encrypt( ctx,
				_bin_data, _bin_data_len, _encbuf, &_encbuf_len ) )
			printf("Error: Encryption failed.\n");
		printf( "\n**********************\n\n" );
	}
	else
	{
		if( OAES_RET_SUCCESS != oaes_encrypt( ctx,
				(const uint8_t *)_text, strlen( _text ), NULL, &_encbuf_len ) )
			printf("Error: Failed to retrieve required buffer size for encryption.\n");
		_encbuf = (uint8_t *) calloc(_encbuf_len, sizeof(uint8_t));
		if( NULL == _encbuf )
		{
			printf( "Error: Failed to allocate memory.\n" );
			if( _key_data )
				free( _key_data );
			return EXIT_FAILURE;
		}
		printf( "\n" );
		if( OAES_RET_SUCCESS != oaes_encrypt( ctx,
				(const uint8_t *)_text, strlen( _text ), _encbuf, &_encbuf_len ) )
			printf("Error: Encryption failed.\n");
		printf( "\n**********************\n\n" );
	}

	if( OAES_RET_SUCCESS != oaes_decrypt( ctx,
			_encbuf, _encbuf_len, NULL, &_decbuf_len ) )
		printf("Error: Failed to retrieve required buffer size for encryption.\n");
	_decbuf = (uint8_t *) calloc(_decbuf_len, sizeof(uint8_t));
	if( NULL == _decbuf )
	{
		printf( "Error: Failed to allocate memory.\n" );
		if( _key_data )
			free( _key_data );
		if( _bin_data )
			free( _bin_data );
		free( _encbuf );
		return EXIT_FAILURE;
	}
	if( OAES_RET_SUCCESS != oaes_decrypt( ctx,
			_encbuf, _encbuf_len, _decbuf, &_decbuf_len ) )
		printf("Error: Decryption failed.\n");

	if( OAES_RET_SUCCESS !=  oaes_free( &ctx ) )
		printf("Error: Failed to uninitialize OAES.\n");
	
	oaes_sprintf( NULL, &_buf_len, _encbuf, _encbuf_len );
	_buf = (char *) calloc(_buf_len, sizeof(char));
	printf( "\n***** cyphertext *****\n" );
	if( _buf )
	{
		oaes_sprintf( _buf, &_buf_len, _encbuf, _encbuf_len );
		printf( "%s", _buf );
	}
	printf( "\n**********************\n" );
	free( _buf );
	
	oaes_sprintf( NULL, &_buf_len, _decbuf, _decbuf_len );
	_buf = (char *) calloc(_buf_len, sizeof(char));
	printf( "\n***** plaintext  *****\n" );
	if( _buf )
	{
		oaes_sprintf( _buf, &_buf_len, _decbuf, _decbuf_len );
		printf( "%s", _buf );
	}
	printf( "\n**********************\n\n" );
	free( _buf );
	
	free( _encbuf );
	free( _decbuf );
	if( _key_data )
		free( _key_data );
	if( _bin_data )
		free( _bin_data );

	return (EXIT_SUCCESS);
}
