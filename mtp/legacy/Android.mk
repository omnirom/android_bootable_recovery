LOCAL_PATH := $(call my-dir)

# Build libtwrpmtp library

include $(CLEAR_VARS)
LOCAL_MODULE := libtwrpmtp-legacy
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS = -D_FILE_OFFSET_BITS=64 -DMTP_DEVICE -DMTP_HOST -fno-strict-aliasing -Wno-unused-variable -Wno-format -Wno-unused-parameter -Wno-unused-private-field
LOCAL_C_INCLUDES += $(LOCAL_PATH) bionic frameworks/base/include system/core/include bionic/libc/private/
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_C_INCLUDES += external/stlport/stlport
    LOCAL_SHARED_LIBRARIES += libstlport
else
    LOCAL_SHARED_LIBRARIES += libc++
endif

LOCAL_SRC_FILES = \
    btree.cpp \
    MtpDataPacket.cpp \
    MtpDebug.cpp \
    MtpDevice.cpp \
    MtpDeviceInfo.cpp \
    MtpEventPacket.cpp \
    MtpObjectInfo.cpp \
    MtpPacket.cpp \
    MtpProperty.cpp \
    MtpRequestPacket.cpp \
    MtpResponsePacket.cpp \
    MtpServer.cpp \
    MtpStorage.cpp \
    MtpStorageInfo.cpp \
    MtpStringBuffer.cpp \
    MtpUtils.cpp \
    mtp_MtpServer.cpp \
    twrpMtp.cpp \
    mtp_MtpDatabase.cpp \
    node.cpp
LOCAL_SHARED_LIBRARIES += libz libc libusbhost libstdc++ libdl libcutils libutils libaosprecovery libselinux

ifneq ($(TW_MTP_DEVICE),)
	LOCAL_CFLAGS += -DUSB_MTP_DEVICE=$(TW_MTP_DEVICE)
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 25; echo $$?),0)
    LOCAL_CFLAGS += -DHAS_USBHOST_TIMEOUT
endif

include $(BUILD_SHARED_LIBRARY)
