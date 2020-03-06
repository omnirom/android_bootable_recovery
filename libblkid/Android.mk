LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libutil-linux
LOCAL_MODULE_TAGS := optional
#LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_CFLAGS := -D_FILE_OFFSET_BITS=64 -DHAVE_LOFF_T -DHAVE_ERR_H -DHAVE_MEMPCPY -DHAVE_FSYNC
LOCAL_CFLAGS += -Wno-missing-field-initializers -Wno-sign-compare -Wno-unused-parameter -Wno-format -Wno-pointer-arith
LOCAL_SRC_FILES = 	lib/at.c \
			lib/blkdev.c \
			lib/canonicalize.c \
			lib/colors.c \
			lib/crc32.c \
			lib/crc64.c \
			lib/env.c \
			lib/exec_shell.c \
			lib/fileutils.c \
			lib/ismounted.c \
			lib/langinfo.c \
			lib/linux_version.c \
			lib/loopdev.c \
			lib/mangle.c \
			lib/match.c \
			lib/mbsalign.c \
			lib/md5.c \
			lib/pager.c \
			lib/path.c \
			lib/procutils.c \
			lib/randutils.c \
			lib/setproctitle.c \
			lib/strutils.c \
			lib/sysfs.c \

LOCAL_C_INCLUDES += \
			$(LOCAL_PATH)/include \
		    $(LOCAL_PATH)/src

LOCAL_SHARED_LIBRARIES += libc
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libuuid
LOCAL_MODULE_TAGS := optional
#LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_CFLAGS := -D_FILE_OFFSET_BITS=64 -DHAVE_LOFF_T -DHAVE_ERR_H -DHAVE_MEMPCPY -DHAVE_FSYNC -DHAVE_SYS_FILE_H
LOCAL_CFLAGS += -Wno-missing-field-initializers -Wno-sign-compare -Wno-unused-parameter -Wno-format -Wno-pointer-arith
LOCAL_SRC_FILES =	libuuid/src/clear.c \
			libuuid/src/copy.c \
			libuuid/src/isnull.c \
			libuuid/src/parse.c \
			libuuid/src/unpack.c \
			libuuid/src/uuid_time.c \
			libuuid/src/compare.c \
			libuuid/src/gen_uuid.c \
			libuuid/src/pack.c \
			libuuid/src/test_uuid.c \
			libuuid/src/unparse.c

LOCAL_C_INCLUDES += 	$(LOCAL_PATH)/libuuid/src \
			$(LOCAL_PATH)/include \
			$(LOCAL_PATH)/src

LOCAL_SHARED_LIBRARIES += libc libutil-linux

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libfdisk
LOCAL_MODULE_TAGS := optional
#LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_CFLAGS := -D_FILE_OFFSET_BITS=64 -DHAVE_LOFF_T -DHAVE_ERR_H -DHAVE_MEMPCPY -DHAVE_FSYNC
LOCAL_CFLAGS += -Wno-missing-field-initializers -Wno-sign-compare -Wno-unused-parameter -Wno-format -Wno-pointer-arith
LOCAL_SRC_FILES = 	libfdisk/src/alignment.c \
			libfdisk/src/context.c  \
			libfdisk/src/init.c   \
			libfdisk/src/partition.c \
			libfdisk/src/sgi.c \
			libfdisk/src/test.c \
			libfdisk/src/ask.c \
			libfdisk/src/dos.c \
			libfdisk/src/iter.c \
			libfdisk/src/parttype.c \
			libfdisk/src/sun.c \
			libfdisk/src/utils.c \
			libfdisk/src/bsd.c \
			libfdisk/src/gpt.c \
			libfdisk/src/label.c \
			libfdisk/src/script.c \
			libfdisk/src/table.c

LOCAL_C_INCLUDES += 	$(LOCAL_PATH)/libfdisk/src \
			$(LOCAL_PATH)/include \
			$(LOCAL_PATH)/libuuid/src \
			$(LOCAL_PATH)/src

LOCAL_SHARED_LIBRARIES += libc libutil-linux libuuid
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libblkid
LOCAL_MODULE_TAGS := optional
#LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_CFLAGS := -D_FILE_OFFSET_BITS=64 -DHAVE_LOFF_T -DHAVE_ERR_H -DHAVE_MEMPCPY -DHAVE_FSYNC
LOCAL_CFLAGS += -Wno-missing-field-initializers -Wno-sign-compare -Wno-unused-parameter -Wno-format -Wno-pointer-arith
LOCAL_SRC_FILES = 	src/cache.c \
			src/config.c \
			src/dev.c \
			src/devname.c \
			src/devno.c \
			src/encode.c \
			src/evaluate.c \
			src/getsize.c \
			src/init.c \
			src/llseek.c \
			src/probe.c \
			src/read.c \
			src/resolve.c \
			src/save.c \
			src/tag.c \
			src/verify.c \
			src/version.c \
			src/partitions/aix.c \
			src/partitions/bsd.c \
			src/partitions/dos.c \
			src/partitions/gpt.c \
			src/partitions/mac.c \
			src/partitions/minix.c \
			src/partitions/partitions.c \
			src/partitions/sgi.c \
			src/partitions/solaris_x86.c \
			src/partitions/sun.c \
			src/partitions/ultrix.c \
			src/partitions/unixware.c \
			src/superblocks/adaptec_raid.c  \
			src/superblocks/bcache.c  \
			src/superblocks/befs.c  \
			src/superblocks/bfs.c \
			src/superblocks/btrfs.c \
			src/superblocks/cramfs.c \
			src/superblocks/ddf_raid.c \
			src/superblocks/drbd.c \
			src/superblocks/drbdproxy_datalog.c \
			src/superblocks/exfat.c \
			src/superblocks/ext.c \
			src/superblocks/f2fs.c \
			src/superblocks/gfs.c \
			src/superblocks/hfs.c \
			src/superblocks/highpoint_raid.c \
			src/superblocks/hpfs.c \
			src/superblocks/iso9660.c \
			src/superblocks/isw_raid.c \
			src/superblocks/jfs.c \
			src/superblocks/jmicron_raid.c \
			src/superblocks/linux_raid.c \
			src/superblocks/lsi_raid.c \
			src/superblocks/luks.c \
			src/superblocks/lvm.c \
			src/superblocks/minix.c \
			src/superblocks/netware.c \
			src/superblocks/nilfs.c \
			src/superblocks/ntfs.c \
			src/superblocks/nvidia_raid.c \
			src/superblocks/ocfs.c \
			src/superblocks/promise_raid.c \
			src/superblocks/refs.c \
			src/superblocks/reiserfs.c \
			src/superblocks/romfs.c \
			src/superblocks/silicon_raid.c \
			src/superblocks/squashfs.c \
			src/superblocks/superblocks.c \
			src/superblocks/swap.c \
			src/superblocks/sysv.c \
			src/superblocks/ubifs.c \
			src/superblocks/udf.c \
			src/superblocks/ufs.c \
			src/superblocks/vfat.c \
			src/superblocks/via_raid.c \
			src/superblocks/vmfs.c \
			src/superblocks/vxfs.c \
			src/superblocks/xfs.c \
			src/superblocks/zfs.c \
			src/topology/dm.c \
			src/topology/evms.c \
			src/topology/ioctl.c \
			src/topology/lvm.c \
			src/topology/md.c \
			src/topology/sysfs.c \
			src/topology/topology.c \

LOCAL_C_INCLUDES += $(LOCAL_PATH)/include \
			$(LOCAL_PATH)/src

LOCAL_SHARED_LIBRARIES += libc libutil-linux
include $(BUILD_SHARED_LIBRARY)
