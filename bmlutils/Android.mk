LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

BOARD_RECOVERY_DEFINES := BOARD_BML_BOOT BOARD_BML_RECOVERY
LOCAL_STATIC_LIBRARY := libcrecovery

$(foreach board_define,$(BOARD_RECOVERY_DEFINES), \
  $(if $($(board_define)), \
    $(eval LOCAL_CFLAGS += -D$(board_define)=\"$($(board_define))\") \
  ) \
  )

LOCAL_SRC_FILES := bmlutils.c
LOCAL_MODULE := libbmlutils
LOCAL_MODULE_TAGS := optional
include $(BUILD_STATIC_LIBRARY)

#Added for building TWRP dynamic:
include $(CLEAR_VARS)

BOARD_RECOVERY_DEFINES := BOARD_BML_BOOT BOARD_BML_RECOVERY
LOCAL_SHARED_LIBRARIES := libcrecovery

$(foreach board_define,$(BOARD_RECOVERY_DEFINES), \
  $(if $($(board_define)), \
    $(eval LOCAL_CFLAGS += -D$(board_define)=\"$($(board_define))\") \
  ) \
  )

LOCAL_SRC_FILES := bmlutils.c
LOCAL_MODULE := libbmlutils
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
