LOCAL_PATH := $(call my-dir)

# Static executable
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    ../exclude.cpp \
    ../gui/twmsg.cpp \
    ../progresstracking.cpp \
    ../tarWrite.c \
    ../twrp-functions.cpp \
    ../twrpTar.cpp \
    twrpTarMain.cpp

LOCAL_CFLAGS := \
    -DBUILD_TWRPTAR_MAIN \
    -g -c -W

ifneq ($(RECOVERY_SDCARD_ON_DATA),)
    LOCAL_CFLAGS += -DRECOVERY_SDCARD_ON_DATA
endif
ifeq ($(TW_EXCLUDE_ENCRYPTED_BACKUPS), true)
    LOCAL_CFLAGS += -DTW_EXCLUDE_ENCRYPTED_BACKUPS
endif

LOCAL_STATIC_LIBRARIES := \
    libc \
    libselinux \
    libstdc++ \
    libtar_static \
    libz

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_STATIC_LIBRARIES += libstlport_static
endif
ifneq ($(TW_EXCLUDE_ENCRYPTED_BACKUPS), true)
    LOCAL_STATIC_LIBRARIES += libopenaes_static
endif

LOCAL_C_INCLUDES := \
    bionic \
    external/libselinux/include

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_C_INCLUDES += external/stlport/stlport bionic/libstdc++/include
endif

LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_MODULE := twrpTar_static
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := UTILITY_EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities

include $(BUILD_EXECUTABLE)

# Shared executable
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    ../exclude.cpp \
    ../gui/twmsg.cpp \
    ../progresstracking.cpp \
    ../tarWrite.c \
    ../twrp-functions.cpp \
    ../twrpTar.cpp \
    twrpTarMain.cpp

LOCAL_CFLAGS := \
    -DBUILD_TWRPTAR_MAIN \
    -g -c -W

ifneq ($(RECOVERY_SDCARD_ON_DATA),)
    LOCAL_CFLAGS += -DRECOVERY_SDCARD_ON_DATA
endif
ifeq ($(TW_EXCLUDE_ENCRYPTED_BACKUPS), true)
    LOCAL_CFLAGS += -DTW_EXCLUDE_ENCRYPTED_BACKUPS
endif

LOCAL_C_INCLUDES := \
    bionic \
    external/libselinux/include

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_C_INCLUDES += \
        bionic/libstdc++/include \
        external/stlport/stlport
endif

LOCAL_SHARED_LIBRARIES := \
    libc \
    libselinux \
    libstdc++ \
    libtar \
    libz

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_SHARED_LIBRARIES += libstlport_static
endif
ifneq ($(TW_EXCLUDE_ENCRYPTED_BACKUPS), true)
    LOCAL_SHARED_LIBRARIES += libopenaes
endif

LOCAL_MODULE := twrpTar
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := UTILITY_EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities

include $(BUILD_EXECUTABLE)
