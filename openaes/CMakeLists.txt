# ---------------------------------------------------------------------------
# OpenAES License
# ---------------------------------------------------------------------------
# Copyright (c) 2012, Nabil S. Al Ramli, www.nalramli.com
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
#   - Redistributions of source code must retain the above copyright notice,
#     this list of conditions and the following disclaimer.
#   - Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
# ---------------------------------------------------------------------------

cmake_minimum_required (VERSION 2.8.0)

project ( "oaes" )

include_directories (
		${CMAKE_CURRENT_SOURCE_DIR}/inc
		${CMAKE_CURRENT_SOURCE_DIR}/src/isaac
	)

set (SRC
		${CMAKE_CURRENT_SOURCE_DIR}/src/oaes_lib.c
		${CMAKE_CURRENT_SOURCE_DIR}/src/isaac/rand.c
	)

set (HDR
		${CMAKE_CURRENT_SOURCE_DIR}/inc/oaes_config.h
		${CMAKE_CURRENT_SOURCE_DIR}/inc/oaes_lib.h
		${CMAKE_CURRENT_SOURCE_DIR}/src/isaac/rand.h
		${CMAKE_CURRENT_SOURCE_DIR}/src/isaac/standard.h
	)

set (SRC_test_encrypt
		${CMAKE_CURRENT_SOURCE_DIR}/test/test_encrypt.c
	)

set (SRC_test_keys
		${CMAKE_CURRENT_SOURCE_DIR}/test/test_keys.c
	)

set (SRC_test_performance
		${CMAKE_CURRENT_SOURCE_DIR}/test/test_performance.c
	)

set (SRC_vt_aes
		${CMAKE_CURRENT_SOURCE_DIR}/test/vt_aes.c
	)

set (SRC_oaes
		${CMAKE_CURRENT_SOURCE_DIR}/src/oaes.c
	)

add_executable (test_encrypt ${SRC_test_encrypt} ${SRC} ${HDR})
add_executable (test_keys ${SRC_test_keys} ${SRC} ${HDR})
add_executable (test_performance ${SRC_test_performance} ${SRC} ${HDR})
add_executable (vt_aes ${SRC_vt_aes} ${SRC} ${HDR})
add_executable (oaes ${SRC_oaes} ${SRC} ${HDR})
