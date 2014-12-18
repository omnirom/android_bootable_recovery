LOCAL_PATH := $(call my-dir)
ifeq ($(TW_INCLUDE_CRYPTO), true)
include $(CLEAR_VARS)

common_c_includes := \
	system/extras/ext4_utils \
	system/extras/f2fs_utils \
	external/openssl/include \
	external/stlport/stlport \
	bionic \
	$(commands_recovery_local_path)/crypto/scrypt/lib/crypto \
	frameworks/native/include \
	system/security/keystore \
	hardware/libhardware/include/hardware \
	system/security/softkeymaster/include/keymaster

common_shared_libraries := \
	libsysutils \
	libstlport \
	libbinder \
	libcutils \
	liblog \
	libdiskconfig \
	liblogwrap \
	libext4_utils \
	libf2fs_sparseblock \
	libcrypto \
	libselinux \
	libutils \
	libminhardware \
	libsoftkeymaster

common_shared_libraries := libcrypto libselinux libhardware libsoftkeymaster libcutils

LOCAL_MODULE := libcryptfslollipop
LOCAL_MODULE_TAGS := eng optional
LOCAL_CFLAGS :=
LOCAL_SRC_FILES = cryptfs.c
#LOCAL_C_INCLUDES += \
#    system/extras/ext4_utils \
#    external/openssl/include \
#    system/extras/f2fs_utils \
#    external/scrypt/lib/crypto \
#    system/security/keystore \
#    hardware/libhardware/include/hardware \
#	system/security/softkeymaster/include/keymaster
#LOCAL_SHARED_LIBRARIES += libc liblog libcutils libcrypto libext4_utils
LOCAL_SHARED_LIBRARIES := $(common_shared_libraries) libmincrypttwrp
LOCAL_C_INCLUDES := external/openssl/include $(common_c_includes)
LOCAL_WHOLE_STATIC_LIBRARIES += libscrypttwrp_static

include $(BUILD_SHARED_LIBRARY)

endif
