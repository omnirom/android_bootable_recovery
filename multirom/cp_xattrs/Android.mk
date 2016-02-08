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


#Relink cp_xattrs, ls_xattrs, and bbootimg
RELINK := $(LOCAL_PATH)/../../prebuilt/relink.sh

#dummy file to trigger required modules
include $(CLEAR_VARS)
LOCAL_MODULE := teamwin_mrom
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin

RELINK_SOURCE_FILES += $(TARGET_OUT_OPTIONAL_EXECUTABLES)/cp_xattrs
RELINK_SOURCE_FILES += $(TARGET_OUT_OPTIONAL_EXECUTABLES)/ls_xattrs
RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/bbootimg

GEN := $(intermediates)/teamwin_mrom
$(GEN): $(RELINK)
$(GEN): $(RELINK_SOURCE_FILES) $(call intermediates-dir-for,EXECUTABLES,recovery)/recovery
	$(RELINK) $(TARGET_RECOVERY_ROOT_OUT)/sbin $(RELINK_SOURCE_FILES)

LOCAL_GENERATED_SOURCES := $(GEN)
LOCAL_SRC_FILES := teamwin_mrom $(GEN)
include $(BUILD_PREBUILT)
