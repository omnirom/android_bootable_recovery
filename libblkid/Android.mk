LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libblkid
LOCAL_MODULE_TAGS := optional
#LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_CFLAGS = -D_FILE_OFFSET_BITS=64 -DHAVE_LOFF_T
LOCAL_SRC_FILES = aix.c at.c befs.c bfs.c blkdev.c bsd.c btrfs.c cache.c canonicalize.c colors.c config.c cramfs.c crc32.c ddf_raid.c dev.c devname.c devno.c dm.c dos.c drbd.c drbdproxy_datalog.c encode.c env.c evaluate.c evms.c exec_shell.c exfat.c ext.c f2fs.c fileutils.c getsize.c gfs.c gpt.c hfs.c highpoint_raid.c hpfs.c ioctl.c ismounted.c iso9660.c isw_raid.c jfs.c jmicron_raid.c langinfo.c linux_raid.c linux_version.c llseek.c loopdev.c lsi_raid.c luks.c lvm1.c lvm2.c mac.c mangle.c match.c mbsalign.c md5.c md.c minix1.c minix2.c netware.c nilfs.c ntfs.c nvidia_raid.c ocfs.c pager.c partitions.c path.c probe.c procutils.c promise_raid.c randutils.c read.c reiserfs.c resolve.c romfs.c save.c setproctitle.c sgi.c silicon_raid.c solaris_x86.c squashfs.c sun.c superblocks.c swap.c sysfs1.c sysfs2.c sysv.c tag.c topology.c  ubifs.c udf.c ufs.c ultrix.c unixware.c verify.c version.c vfat.c via_raid.c vmfs.c vxfs.c wholedisk.c xfs.c zfs.c adaptec_raid.c
LOCAL_C_INCLUDES += $(LOCAL_PATH) \
LOCAL_SHARED_LIBRARIES += libc

include $(BUILD_SHARED_LIBRARY)
