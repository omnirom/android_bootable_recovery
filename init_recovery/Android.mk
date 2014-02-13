

LOCAL_PATH:= system/core/init
include $(CLEAR_VARS)

common_src_files := \
	builtins.c \
	init.c \
	devices.c \
	property_service_old.c \
	util.c \
	parser.c \
	keychords.c \
	signal_handler.c \
	init_parser.c \
	ueventd.c \
	ueventd_parser.c \
	watchdogd.c \
	vendor_init.c

ifeq ($(strip $(INIT_BOOTCHART)),true)
common_src_files += bootchart.c
LOCAL_CFLAGS    += -DBOOTCHART=1
endif

ifneq (,$(filter userdebug eng,$(TARGET_BUILD_VARIANT)))
LOCAL_CFLAGS += -DALLOW_LOCAL_PROP_OVERRIDE=1
endif

ifeq ($(BOARD_WANTS_EMMC_BOOT),true)
LOCAL_CFLAGS += -DWANTS_EMMC_BOOT
endif

ifneq ($(TARGET_NO_INITLOGO),true)
common_src_files += logo.c
LOCAL_CFLAGS += -DINITLOGO
endif

ifneq ($(TARGET_NR_SVC_SUPP_GIDS),)
LOCAL_CFLAGS += -DNR_SVC_SUPP_GIDS=$(TARGET_NR_SVC_SUPP_GIDS)
endif

SYSTEM_CORE_INIT_DEFINES := BOARD_CHARGING_MODE_BOOTING_LPM \
    BOARD_LPM_BOOT_ARGUMENT_NAME \
    BOARD_LPM_BOOT_ARGUMENT_VALUE

$(foreach system_core_init_define,$(SYSTEM_CORE_INIT_DEFINES), \
  $(if $($(system_core_init_define)), \
    $(eval LOCAL_CFLAGS += -D$(system_core_init_define)=\"$($(system_core_init_define))\") \
  ) \
  )

LOCAL_MODULE:= init_recovery

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)
#LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)

common_static_libs := \
	libfs_mgr \
	liblogwrap \
	libcutils \
	liblog \
	libselinux \
	libmincrypt \
	libext4_utils_static

ifneq ($(strip $(TARGET_INIT_VENDOR_LIB)),)
LOCAL_WHOLE_STATIC_LIBRARIES += $(TARGET_INIT_VENDOR_LIB)
endif
LOCAL_SRC_FILES := $(common_src_files)
LOCAL_STATIC_LIBRARIES := $(common_static_libs) libc_oldprops
LOCAL_CFLAGS += -DBUILD_OLD_SYS_PROPS

include $(BUILD_EXECUTABLE)
