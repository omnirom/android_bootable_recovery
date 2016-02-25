LOCAL_PATH:= $(call my-dir)

# Build static binary
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	twrpTarMain.cpp \
	../twrp-functions.cpp \
	../twrpTar.cpp \
	../tarWrite.c \
	../twrpDU.cpp \
	../progresstracking.cpp \
	../gui/twmsg.cpp
LOCAL_CFLAGS:= -g -c -W -DBUILD_TWRPTAR_MAIN

LOCAL_C_INCLUDES += bionic
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_C_INCLUDES += external/stlport/stlport bionic/libstdc++/include
endif

LOCAL_STATIC_LIBRARIES := libc libtar_static
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_STATIC_LIBRARIES += libstlport_static
endif
LOCAL_STATIC_LIBRARIES += libstdc++

ifeq ($(TWHAVE_SELINUX), true)
    LOCAL_C_INCLUDES += external/libselinux/include
    LOCAL_STATIC_LIBRARIES += libselinux
    LOCAL_CFLAGS += -DHAVE_SELINUX
endif
ifneq ($(RECOVERY_SDCARD_ON_DATA),)
	LOCAL_CFLAGS += -DRECOVERY_SDCARD_ON_DATA
endif
ifeq ($(TW_EXCLUDE_ENCRYPTED_BACKUPS), true)
    LOCAL_CFLAGS += -DTW_EXCLUDE_ENCRYPTED_BACKUPS
else
	LOCAL_STATIC_LIBRARIES += libopenaes_static
endif

LOCAL_MODULE:= twrpTar_static
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_TAGS:= optional
LOCAL_MODULE_CLASS := UTILITY_EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities
include $(BUILD_EXECUTABLE)


# Build shared binary
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	twrpTarMain.cpp \
	../twrp-functions.cpp \
	../twrpTar.cpp \
	../tarWrite.c \
	../twrpDU.cpp \
	../progresstracking.cpp \
	../gui/twmsg.cpp
LOCAL_CFLAGS:= -g -c -W -DBUILD_TWRPTAR_MAIN

LOCAL_C_INCLUDES += bionic external/stlport/stlport
LOCAL_SHARED_LIBRARIES := libc libtar libstlport libstdc++

ifeq ($(TWHAVE_SELINUX), true)
    LOCAL_C_INCLUDES += external/libselinux/include
    LOCAL_SHARED_LIBRARIES += libselinux
    LOCAL_CFLAGS += -DHAVE_SELINUX
endif
ifneq ($(RECOVERY_SDCARD_ON_DATA),)
	LOCAL_CFLAGS += -DRECOVERY_SDCARD_ON_DATA
endif
ifeq ($(TW_EXCLUDE_ENCRYPTED_BACKUPS), true)
    LOCAL_CFLAGS += -DTW_EXCLUDE_ENCRYPTED_BACKUPS
else
	LOCAL_SHARED_LIBRARIES += libopenaes
endif

LOCAL_MODULE:= twrpTar
LOCAL_MODULE_TAGS:= optional
LOCAL_MODULE_CLASS := UTILITY_EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities
include $(BUILD_EXECUTABLE)
