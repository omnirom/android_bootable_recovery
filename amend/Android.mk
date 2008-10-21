# Copyright 2007 The Android Open Source Project
#

LOCAL_PATH := $(call my-dir)

amend_src_files := \
	amend.c \
	lexer.l \
	parser_y.y \
	ast.c \
	symtab.c \
	commands.c \
	permissions.c \
	execute.c

amend_test_files := \
	test_symtab.c \
	test_commands.c \
	test_permissions.c

# "-x c" forces the lex/yacc files to be compiled as c;
# the build system otherwise forces them to be c++.
amend_cflags := -Wall -x c

#
# Build the host-side command line tool
#
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
		$(amend_src_files) \
		$(amend_test_files) \
		register.c \
		main.c

LOCAL_CFLAGS := $(amend_cflags) -g -O0
LOCAL_MODULE := amend
LOCAL_YACCFLAGS := -v

include $(BUILD_HOST_EXECUTABLE)

#
# Build the device-side library
#
include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(amend_src_files)
LOCAL_SRC_FILES += $(amend_test_files)

LOCAL_CFLAGS := $(amend_cflags)
LOCAL_MODULE := libamend

include $(BUILD_STATIC_LIBRARY)
