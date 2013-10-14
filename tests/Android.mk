# Build the unit tests.
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

# Build the unit tests.
test_src_files := \
    asn1_decoder_test.cpp

shared_libraries := \
    liblog \
    libcutils

static_libraries := \
    libgtest \
    libgtest_main \
    libverifier

$(foreach file,$(test_src_files), \
    $(eval include $(CLEAR_VARS)) \
    $(eval LOCAL_SHARED_LIBRARIES := $(shared_libraries)) \
    $(eval LOCAL_STATIC_LIBRARIES := $(static_libraries)) \
    $(eval LOCAL_SRC_FILES := $(file)) \
    $(eval LOCAL_MODULE := $(notdir $(file:%.cpp=%))) \
    $(eval LOCAL_C_INCLUDES := $(LOCAL_PATH)/..) \
    $(eval include $(BUILD_NATIVE_TEST)) \
)