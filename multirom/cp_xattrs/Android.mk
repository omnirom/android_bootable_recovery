LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := libcp_xattrs.cpp
LOCAL_MODULE := libcp_xattrs
LOCAL_MODULE_TAGS := eng
LOCAL_SHARED_LIBRARIES += libc
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_SHARED_LIBRARIES += libstlport
else
    LOCAL_SHARED_LIBRARIES += libc++
endif
LOCAL_C_INCLUDES += bionic
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_C_INCLUDES += external/stlport/stlport
endif
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := cp_xattrs.cpp
LOCAL_MODULE := cp_xattrs
LOCAL_MODULE_TAGS := eng
LOCAL_C_INCLUDES += bionic
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_C_INCLUDES += external/stlport/stlport
endif
LOCAL_SHARED_LIBRARIES += libc
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_SHARED_LIBRARIES += libstlport
else
    LOCAL_SHARED_LIBRARIES += libc++
endif
LOCAL_STATIC_LIBRARIES += libcp_xattrs
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)
LOCAL_UNSTRIPPED_PATH := $(TARGET_OUT_EXECUTABLES_UNSTRIPPED)
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := ls_xattrs.cpp
LOCAL_MODULE := ls_xattrs
LOCAL_MODULE_TAGS := eng
LOCAL_C_INCLUDES += bionic
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_C_INCLUDES += external/stlport/stlport
endif
LOCAL_SHARED_LIBRARIES += libc
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_SHARED_LIBRARIES += libstlport
else
    LOCAL_SHARED_LIBRARIES += libc++
endif
LOCAL_STATIC_LIBRARIES += libcp_xattrs
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)
LOCAL_UNSTRIPPED_PATH := $(TARGET_OUT_EXECUTABLES_UNSTRIPPED)
include $(BUILD_EXECUTABLE)
