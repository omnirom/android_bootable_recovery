LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

ifneq ($(TW_EXCLUDE_ENCRYPTED_BACKUPS), true)
	# Build shared binary
	LOCAL_SRC_FILES:= src/oaes.c \
	LOCAL_C_INCLUDES := \
		bootable/recovery/openaes/src/isaac \
		bootable/recovery/openaes/inc
	LOCAL_CFLAGS:= -g -c -W
	LOCAL_MODULE:=openaes
	LOCAL_MODULE_TAGS:= eng
	LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
	LOCAL_SHARED_LIBRARIES = libopenaes libc
	include $(BUILD_EXECUTABLE)

	# Build shared library
	include $(CLEAR_VARS)
	LOCAL_MODULE := libopenaes
	LOCAL_MODULE_TAGS := eng
	LOCAL_C_INCLUDES := \
		bootable/recovery/openaes/src/isaac \
		bootable/recovery/openaes/inc
	LOCAL_SRC_FILES = src/oaes_lib.c src/isaac/rand.c
	LOCAL_SHARED_LIBRARIES = libc
	include $(BUILD_SHARED_LIBRARY)

	# Build static library
	include $(CLEAR_VARS)
	LOCAL_MODULE := libopenaes_static
	LOCAL_MODULE_TAGS := eng
	LOCAL_C_INCLUDES := \
		bootable/recovery/openaes/src/isaac \
		bootable/recovery/openaes/inc
	LOCAL_SRC_FILES = src/oaes_lib.c src/isaac/rand.c
	LOCAL_STATIC_LIBRARIES = libc
	include $(BUILD_STATIC_LIBRARY)
endif
