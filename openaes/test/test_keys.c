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

#include "oaes_lib.h"

/*
 * 
 */
int main(int argc, char** argv) {

	OAES_CTX * ctx = NULL;
	uint8_t * _buf;
	size_t _data_len;
	FILE * f = NULL;
	OAES_RET _rc;
	
	if( NULL == ( ctx = oaes_alloc() ) )
	{
		printf( "Error: Initialization failed.\n" );
		return 1;
	}
	
	/* ************** Generate 128-bit key and export it **************
	 * ****************************************************************/
	if( OAES_RET_SUCCESS != ( _rc = oaes_key_gen_128(ctx) ) )
	{
		printf( "Error: Failed to generate 128-bit key [%d].\n", _rc );
		oaes_free(&ctx);
		return 1;
	}
	
	if( OAES_RET_SUCCESS != ( _rc = oaes_key_export(ctx, NULL, &_data_len) ) )
	{
		printf( "Error: Failed to retrieve key length [%d].\n", _rc );
		oaes_free(&ctx);
		return 1;
	}
	
	_buf = (uint8_t *) calloc(_data_len, sizeof(uint8_t));
	if( _buf )
	{
		if( OAES_RET_SUCCESS != ( _rc = oaes_key_export(ctx, _buf, &_data_len) ) )
		{
			printf("Error: Failed to export key [%d].\n", _rc);
			free(_buf);
			oaes_free(&ctx);
			return 1;
		}
		
		f = fopen( "key_128", "wb" );
		if( f )
		{
			fwrite(_buf, _data_len, sizeof(uint8_t), f);
			fclose(f);
		}
		free(_buf);
	}
	
	/* ************** Generate 192-bit key and export it **************
	 * ****************************************************************/
	if( OAES_RET_SUCCESS != ( _rc = oaes_key_gen_192(ctx) ) )
	{
		printf( "Error: Failed to generate 192-bit key [%d].\n", _rc );
		oaes_free(&ctx);
		return 1;
	}
	
	if( OAES_RET_SUCCESS != ( _rc = oaes_key_export(ctx, NULL, &_data_len) ) )
	{
		printf( "Error: Failed to retrieve key length [%d].\n", _rc );
		oaes_free(&ctx);
		return 1;
	}
	
	_buf = (uint8_t *) calloc(_data_len, sizeof(uint8_t));
	if( _buf )
	{
		if( OAES_RET_SUCCESS != ( _rc = oaes_key_export(ctx, _buf, &_data_len) ) )
		{
			printf("Error: Failed to export key [%d].\n", _rc);
			free(_buf);
			oaes_free(&ctx);
			return 1;
		}
		
		f = fopen("key_192", "wb");
		if( f )
		{
			fwrite(_buf, _data_len, sizeof(uint8_t), f);
			fclose(f);
		}
		free(_buf);
	}
	
	/* ************** Generate 256-bit key and export it **************
	 * ****************************************************************/
	if( OAES_RET_SUCCESS != ( _rc = oaes_key_gen_256(ctx) ) )
	{
		printf("Error: Failed to generate 256-bit key [%d].\n", _rc);
		oaes_free(&ctx);
		return 1;
	}
	
	if( OAES_RET_SUCCESS != ( _rc = oaes_key_export(ctx, NULL, &_data_len) ) )
	{
		printf("Error: Failed to retrieve key length [%d].\n", _rc);
		oaes_free(&ctx);
		return 1;
	}
	
	_buf = (uint8_t *) calloc(_data_len, sizeof(uint8_t));
	if( _buf )
	{
		if( OAES_RET_SUCCESS != ( _rc = oaes_key_export(ctx, _buf, &_data_len) ) )
		{
			printf("Error: Failed to export key [%d].\n", _rc);
			free(_buf);
			oaes_free(&ctx);
			return 1;
		}
		
		f = fopen("key_256", "wb");
		if( f )
		{
			fwrite(_buf, _data_len, sizeof(uint8_t), f);
			fclose(f);
		}
		free(_buf);
	}
	
	/* ********************** Import 128-bit key **********************
	 * ****************************************************************/
	f = fopen("key_128", "rb");
	if( f )
	{
		fseek(f, 0L, SEEK_END);
		_data_len = ftell(f);
		fseek(f, 0L, SEEK_SET);
		_buf = (uint8_t *) calloc(_data_len, sizeof(uint8_t));
		if( _buf )
		{
			fread(_buf, _data_len, sizeof(uint8_t), f);
			
			if( OAES_RET_SUCCESS !=
					( _rc = oaes_key_import(ctx, _buf, _data_len) ) )
			{
				printf( "Error: Failed to import key [%d].\n", _rc );
				free(_buf);
				fclose(f);
				oaes_free(&ctx);
				return 1;
			}
			
			free(_buf);
		}
		fclose(f);
	}
	
	/* ********************** Import 192-bit key **********************
	 * ****************************************************************/
	f = fopen("key_192", "rb");
	if( f )
	{
		fseek(f, 0L, SEEK_END);
		_data_len = ftell(f);
		fseek(f, 0L, SEEK_SET);
		_buf = (uint8_t *) calloc(_data_len, sizeof(uint8_t));
		if( _buf )
		{
			fread(_buf, _data_len, sizeof(uint8_t), f);
			
			if( OAES_RET_SUCCESS !=
					( _rc = oaes_key_import(ctx, _buf, _data_len) ) )
			{
				printf("Error: Failed to import key [%d].\n", _rc);
				free(_buf);
				fclose(f);
				oaes_free(&ctx);
				return 1;
			}
			
			free(_buf);
		}
		fclose(f);
	}
	
	/* ********************** Import 256-bit key **********************
	 * ****************************************************************/
	f = fopen("key_256", "rb");
	if( f )
	{
		fseek(f, 0L, SEEK_END);
		_data_len = ftell(f);
		fseek(f, 0L, SEEK_SET);
		_buf = (uint8_t *) calloc(_data_len, sizeof(uint8_t));
		if( _buf )
		{
			fread(_buf, _data_len, sizeof(uint8_t), f);
			
			if( OAES_RET_SUCCESS !=
					( _rc = oaes_key_import(ctx, _buf, _data_len) ) )
			{
				printf("Error: Failed to import key [%d].\n", _rc);
				free(_buf);
				fclose(f);
				oaes_free(&ctx);
				return 1;
			}
			
			free(_buf);
		}
		fclose(f);
	}
	
	oaes_free(&ctx);

	return (EXIT_SUCCESS);
}
