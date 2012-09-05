LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

BOARD_RECOVERY_DEFINES := BOARD_BML_BOOT BOARD_BML_RECOVERY

$(foreach board_define,$(BOARD_RECOVERY_DEFINES), \
  $(if $($(board_define)), \
    $(eval LOCAL_CFLAGS += -D$(board_define)=\"$($(board_define))\") \
  ) \
  )

LOCAL_STATIC_LIBRARIES := libcrecovery
LOCAL_C_INCLUDES := bootable/recovery/libcrecovery

LOCAL_SRC_FILES := bmlutils.c
LOCAL_MODULE := libbmlutils
LOCAL_MODULE_TAGS := eng
include $(BUILD_STATIC_LIBRARY)

#Added for building TWRP dynamic:
include $(CLEAR_VARS)

BOARD_RECOVERY_DEFINES := BOARD_BML_BOOT BOARD_BML_RECOVERY

$(foreach board_define,$(BOARD_RECOVERY_DEFINES), \
  $(if $($(board_define)), \
    $(eval LOCAL_CFLAGS += -D$(board_define)=\"$($(board_define))\") \
  ) \
  )

LOCAL_SRC_FILES := bmlutils.c
LOCAL_MODULE := libbmlutils
LOCAL_MODULE_TAGS := eng
include $(BUILD_SHARED_LIBRARY)
