LOCAL_PATH := $(call my-dir)
ifeq ($(TW_INCLUDE_CRYPTO), true)
include $(CLEAR_VARS)

LOCAL_MODULE := libtwrpfscrypt
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -Wno-unused-variable -Wno-sign-compare -Wno-unused-parameter -Wno-comment -Wno-missing-field-initializers \
    -DHAVE_LIBKEYUTILS -std=gnu++2a -Wno-macro-redefined -Wno-unused-function
LOCAL_SRC_FILES := Decrypt.cpp ScryptParameters.cpp Utils.cpp HashPassword.cpp \
    FsCrypt.cpp KeyUtil.cpp Keymaster.cpp KeyStorage.cpp MetadataCrypt.cpp KeyBuffer.cpp \
    Process.cpp EncryptInplace.cpp Weaver1.cpp fscrypt_policy.cpp
LOCAL_SHARED_LIBRARIES := libselinux libc libc++ libext4_utils libbase libcrypto libcutils \
libkeymaster_messages libhardware libprotobuf-cpp-lite libfscrypt android.hardware.confirmationui@1.0 \
android.hardware.keymaster@3.0 libkeystore_binder libhidlbase libutils libbinder android.hardware.gatekeeper@1.0 \
libfs_mgr android.hardware.keymaster@4.0 libkeymaster4support libf2fs_sparseblock libkeystore_parcelables \
libkeystore_aidl android.hardware.weaver@1.0 libkeyutils liblog libhwbinder libchrome
LOCAL_STATIC_LIBRARIES := libscrypt_static
LOCAL_C_INCLUDES := system/extras/ext4_utils \
    system/extras/ext4_utils/include/ext4_utils \
    external/scrypt/lib/crypto \
    system/security/keystore/include \
    hardware/libhardware/include/hardware \
    system/security/softkeymaster/include/keymaster \
    system/keymaster/include \
    system/extras/libfscrypt/include \
    system/core/fs_mgr/libfs_avb/include/ \
    system/core/fs_mgr/include_fstab/ \
    system/core/fs_mgr/include/ \
    system/core/fs_mgr/libdm/include/ \
    system/core/fs_mgr/liblp/include/ \
    system/gsid/include/ \
    system/core/init/ \
    system/vold/model \
    system/vold/ \
    system/extras/f2fs_utils/

ifneq ($(wildcard hardware/libhardware/include/hardware/keymaster0.h),)
    LOCAL_CFLAGS += -DTW_CRYPTO_HAVE_KEYMASTERX
    LOCAL_C_INCLUDES +=  external/boringssl/src/include
endif

LOCAL_REQUIRED_MODULES := keystore_auth keystore
LOCAL_CLANG := true
include $(BUILD_SHARED_LIBRARY)



include $(CLEAR_VARS)
LOCAL_MODULE := twrpfbe
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin
LOCAL_SRC_FILES := main.cpp
LOCAL_SHARED_LIBRARIES := libtwrpfscrypt

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := fscryptpolicyget
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin
LOCAL_SRC_FILES := fscryptpolicyget.cpp
LOCAL_SHARED_LIBRARIES := libtwrpfscrypt

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := keystore_auth
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin
LOCAL_SRC_FILES := keystore_auth.cpp
LOCAL_SHARED_LIBRARIES := libc libkeystore_binder libutils libbinder liblog
LOCAL_CFLAGS += -DUSE_SECURITY_NAMESPACE
LOCAL_SHARED_LIBRARIES += libkeystore_aidl

include $(BUILD_EXECUTABLE)

endif
