# ---------------------------------------------------------------------------
# OpenAES License
# ---------------------------------------------------------------------------
# Copyright (c) 2013, Nabil S. Al Ramli, www.nalramli.com
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

set (HDR
    ${CMAKE_CURRENT_SOURCE_DIR}/inc/oaes_common.h
    ${CMAKE_CURRENT_SOURCE_DIR}/inc/oaes_base64.h
    ${CMAKE_CURRENT_SOURCE_DIR}/inc/oaes_lib.h
  )

set (SRC_lib
    ${CMAKE_CURRENT_SOURCE_DIR}/src/oaes_base64.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/oaes_lib.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/isaac/rand.c
  )

set (HDR_lib
    ${CMAKE_CURRENT_SOURCE_DIR}/inc/oaes_config.h
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

add_library( oaes_lib ${SRC_lib} ${HDR_lib} ${HDR} )
add_executable( test_encrypt ${SRC_test_encrypt} ${HDR} )
add_executable( test_keys ${SRC_test_keys} ${HDR} )
add_executable( test_performance ${SRC_test_performance} ${HDR} )
add_executable( vt_aes ${SRC_vt_aes} ${HDR} )
add_executable( oaes ${SRC_oaes} ${HDR} )

target_link_libraries( test_encrypt oaes_lib )
target_link_libraries( test_keys oaes_lib )
target_link_libraries( test_performance oaes_lib )
target_link_libraries( vt_aes oaes_lib )
if( MSVC )
  target_link_libraries( oaes oaes_lib )
else()
  target_link_libraries( oaes oaes_lib pthread )
endif()

file( COPY
  "${CMAKE_CURRENT_SOURCE_DIR}/src/oaes_setup.vdproj"
  DESTINATION "${PROJECT_BINARY_DIR}"
)
add_custom_target( oaes_setup_configure
  ALL
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/src/oaes_setup.vdproj"
    "${PROJECT_BINARY_DIR}"
    &
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG"
    "${PROJECT_BINARY_DIR}"
    &
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${PROJECT_BINARY_DIR}"
    &
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/README"
    "${PROJECT_BINARY_DIR}"
    &
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"
    "${PROJECT_BINARY_DIR}"
)
include_external_msproject(
  oaes_setup "${PROJECT_BINARY_DIR}/oaes_setup.vdproj"
  TYPE "8:{978C614F-708E-4E1A-B201-565925725DBA}"
)
if( MSVC )
  add_dependencies( oaes_setup oaes_setup_configure oaes )
endif()

# set BUILD_SHARED_LIBS=1 to build oaes_lib shared library, or BUILD_SHARED_LIBS=0 to build static library
if( BUILD_SHARED_LIBS )
  set_property(
    TARGET "oaes_lib" "test_encrypt" "test_keys" "test_performance" "vt_aes" "oaes"
    APPEND PROPERTY COMPILE_DEFINITIONS OAES_SHARED=1
  )
else()
  set_property(
    TARGET "oaes_lib" "test_encrypt" "test_keys" "test_performance" "vt_aes" "oaes"
    APPEND PROPERTY COMPILE_DEFINITIONS OAES_STATIC=1
  )
endif()
