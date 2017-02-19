LOCAL_PATH := $(call my-dir)

# libtwrpmtp shared library

include $(CLEAR_VARS)

LOCAL_CFLAGS := \
    -D_FILE_OFFSET_BITS=64 \
    -DMTP_DEVICE \
    -DMTP_HOST \
    -fno-strict-aliasing

ifneq ($(TW_MTP_DEVICE),)
    LOCAL_CFLAGS += -DUSB_MTP_DEVICE=$(TW_MTP_DEVICE)
endif

LOCAL_C_INCLUDES := \
    bionic \
    bionic/libc/private \
    frameworks/base/include \
    system/core/include

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_C_INCLUDES += external/stlport/stlport
endif

LOCAL_SRC_FILES := \
    btree.cpp \
    mtp_MtpDatabase.cpp \
    mtp_MtpServer.cpp \
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
    node.cpp \
    twrpMtp.cpp

LOCAL_SHARED_LIBRARIES := \
    libaosprecovery \
    libc \
    libcutils \
    libdl \
    libselinux \
    libstdc++ \
    libusbhost \
    libutils \
    libz

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_SHARED_LIBRARIES += libstlport
else
    LOCAL_SHARED_LIBRARIES += libc++
endif

LOCAL_MODULE := libtwrpmtp
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

# twrpmtp executable

include $(CLEAR_VARS)

LOCAL_CFLAGS := \
    -D_FILE_OFFSET_BITS=64 \
    -DMTP_DEVICE \
    -DMTP_HOST \
    -DTWRPMTP

LOCAL_C_INCLUDES := \
    bionic \
    bionic/libc/private \
    frameworks/base/include \
    system/core/include

LOCAL_SRC_FILES := \
    btree.cpp \
    mtp_MtpDatabase.cpp \
    mtp_MtpServer.cpp \
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
    node.cpp \
    twrpMtp.cpp

LOCAL_SHARED_LIBRARIES := \
    libaosprecovery \
    libc \
    libcutils \
    libdl \
    libstdc++ \
    libusbhost \
    libutils \
    libz

LOCAL_MODULE := twrpmtp
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin

include $(BUILD_EXECUTABLE)
