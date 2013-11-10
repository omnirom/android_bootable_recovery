local_c_flags := -DUSE_OPENSSL_PBKDF2

local_c_includes := $(log_c_includes) external/openssl/include

local_additional_dependencies := $(LOCAL_PATH)/android-config.mk $(LOCAL_PATH)/Scrypt.mk

include $(LOCAL_PATH)/Scrypt-config.mk

#######################################
# target static library
include $(CLEAR_VARS)
include $(LOCAL_PATH)/android-config.mk

LOCAL_SHARED_LIBRARIES := $(log_shared_libraries)

# If we're building an unbundled build, don't try to use clang since it's not
# in the NDK yet. This can be removed when a clang version that is fast enough
# in the NDK.
ifeq (,$(TARGET_BUILD_APPS))
LOCAL_CLANG := true
else
LOCAL_SDK_VERSION := 9
endif

LOCAL_SRC_FILES += $(target_src_files)
LOCAL_CFLAGS += $(target_c_flags)
LOCAL_C_INCLUDES += $(target_c_includes)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE:= libscrypttwrp_static
LOCAL_ADDITIONAL_DEPENDENCIES := $(local_additional_dependencies)
include $(BUILD_STATIC_LIBRARY)

########################################
# host static library

include $(CLEAR_VARS)
include $(LOCAL_PATH)/android-config.mk
LOCAL_SHARED_LIBRARIES := $(log_shared_libraries)
LOCAL_SRC_FILES += $(host_src_files)
LOCAL_CFLAGS += $(host_c_flags)
LOCAL_C_INCLUDES += $(host_c_includes)
LOCAL_LDLIBS += -ldl
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE:= libscrypttwrp_static
LOCAL_ADDITIONAL_DEPENDENCIES := $(local_additional_dependencies)
include $(BUILD_HOST_STATIC_LIBRARY)
