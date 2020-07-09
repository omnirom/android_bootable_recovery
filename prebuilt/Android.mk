LOCAL_PATH := $(call my-dir)

RELINK := $(LOCAL_PATH)/relink.sh

#dummy file to trigger required modules
include $(CLEAR_VARS)

LOCAL_MODULE := teamwin
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin

# Manage list
RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/dump_image
RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/flash_image
RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/erase_image
RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/bu

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 29; echo $$?),0)
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/bin/sh
else
    RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/sh
endif
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libcrypto.so
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 23; echo $$?),0)
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 29; echo $$?),0)
        RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/bin/grep
    else
        RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/grep
    endif
    LOCAL_POST_INSTALL_CMD += $(hide) if [ -e "$(TARGET_RECOVERY_ROOT_OUT)/sbin/egrep" ]; then \
                                rm $(TARGET_RECOVERY_ROOT_OUT)/sbin/egrep; fi; ln -s $(TARGET_RECOVERY_ROOT_OUT)/sbin/grep $(TARGET_RECOVERY_ROOT_OUT)/sbin/egrep; \
                                if [ -e "$(TARGET_RECOVERY_ROOT_OUT)/sbin/fgrep" ]; then \
                                rm $(TARGET_RECOVERY_ROOT_OUT)/sbin/fgrep; fi; ln -s $(TARGET_RECOVERY_ROOT_OUT)/sbin/grep $(TARGET_RECOVERY_ROOT_OUT)/sbin/fgrep;
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 23; echo $$?),0)
        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 29; echo $$?),0)
            RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/dd
        endif
    endif
    ifneq ($(wildcard external/zip/Android.mk),)
        RELINK_SOURCE_FILES += $(TARGET_OUT_OPTIONAL_EXECUTABLES)/zip
    endif
    ifneq ($(wildcard external/unzip/Android.mk),)
        RELINK_SOURCE_FILES += $(TARGET_OUT_OPTIONAL_EXECUTABLES)/unzip
    endif
    ifneq ($(wildcard system/core/libziparchive/Android.bp),)
        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 29; echo $$?),0)
            RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/bin/unzip
        else
            RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/unzip
        endif
    endif
    ifneq ($(wildcard external/one-true-awk/Android.bp),)
        RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/awk
    endif
endif

RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/pigz
RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/fsck.fat
RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/fatlabel
RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/mkfs.fat
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 27; echo $$?),0)
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -le 28; echo $$?),0)
        RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/adbd
    else
        RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/bin/adbd
        RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/bin/fastbootd
    endif
endif
RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/e2fsck
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
    RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/e2fsdroid
endif
RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/mke2fs
RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/tune2fs
RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/resize2fs
RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/simg2img
ifneq ($(TW_OZIP_DECRYPT_KEY),)
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/ozip_decrypt
endif
ifneq ($(TARGET_ARCH), x86_64)
    RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/linker
endif
ifeq ($(TARGET_ARCH), x86_64)
    RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/linker64
endif
ifeq ($(TARGET_ARCH), arm64)
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 29; echo $$?),0)
        RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/bin/linker64
    else
        RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/linker64
    endif
endif
#RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/twrpmtp
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 29; echo $$?),0)
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libc.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libdl.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libm.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libbootloader_message.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libfs_mgr.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libfscrypt.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libgsi.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libkeyutils.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/liblogwrap.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/liblp.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libprocessgroup.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libprocessgroup_setup.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libadbd.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libadbd_services.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libcap.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libminijail.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libunwindstack.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libasyncio.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libcgrouprc.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libbinderthreadstate.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libsquashfs_utils.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libjsoncpp.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libmdnssd.so
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libfec.so
    RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/lib64/libinit.so
    RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/lib64/libdl_android.so
    RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/lib64/libprotobuf-cpp-lite.so
    RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/lib64/libbinder.so
    RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/lib64/libchrome.so
    RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/lib64/libevent.so
    RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/bin/keystore
    RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/bin/keystore_cli_v2
    RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/bin/hwservicemanager
    RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/bin/servicemanager
    RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/bin/vold_prepare_subdirs
    RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../vendor/bin/vndservicemanager
    RELINK_SOURCE_FILES +=   $(TARGET_RECOVERY_ROOT_OUT)/system/bin/toybox
else
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libc.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libdl.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libm.so
endif
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libcutils.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libcrecovery.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libusbhost.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libutils.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libext2_com_err.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libext2_e2p.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libext2fs.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libext2_profile.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libext2_uuid.so
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 27; echo $$?),0)
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libext2_misc.so
endif
ifneq ($(wildcard external/e2fsprogs/lib/quota/Android.mk),)
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libext2_quota.so
endif
ifneq ($(wildcard external/e2fsprogs/lib/ext2fs/Android.*),)
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libext2fs.so
endif
ifneq ($(wildcard external/e2fsprogs/lib/blkid/Android.*),)
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libext2_blkid.so
endif
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libpng.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/liblog.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libstdc++.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libz.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libminuitwrp.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libminadbd.so
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 29; echo $$?),0)
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libminzip.so
endif
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libmtdutils.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libtar.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libtwadbbu.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libtwrpdigest.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libutil-linux.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libblkid.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libmmcutils.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libbmlutils.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libflashutils.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libfusesideload.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libbootloader_message_twrp.so
ifeq ($(PLATFORM_SDK_VERSION), 22)
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libc++.so
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    # These libraries are no longer present in M
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libstlport.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libgccdemangle.so
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 23; echo $$?),0)

    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libcrypto.so \
    $(if $(WITH_CRYPTO_UTILS),$(TARGET_OUT_SHARED_LIBRARIES)/libcrypto_utils.so)
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libpackagelistparser.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/liblzma.so
endif
ifneq (,$(filter $(PLATFORM_SDK_VERSION), 21 22))
    # libraries from lollipop
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libbacktrace.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libunwind.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libunwind-ptrace.so
    # Dynamically loaded by lollipop libc and may prevent unmounting system if it is not present in sbin
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libnetd_client.so
else
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 23; echo $$?),0)
        # Android M libraries
        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 29; echo $$?),0)
            RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/lib64/libbacktrace.so
        else
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libbacktrace.so
        endif
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libunwind.so
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libbase.so
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libc++.so
        # Dynamically loaded by libc and may prevent unmounting system if it is not present in sbin
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libnetd_client.so
    else
        # Not available in lollipop
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libcorkscrew.so
    endif
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 24; echo $$?),0)
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libmincrypttwrp.so
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/bin/toolbox
else
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/toolbox
endif

ifneq ($(TW_OEM_BUILD),true)
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/twrp
else
    TW_EXCLUDE_MTP := true
endif

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
ifneq ($(TW_EXCLUDE_MTP), true)
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libtwrpmtp-ffs.so
endif
else
ifneq ($(TW_EXCLUDE_MTP), true)
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libtwrpmtp-legacy.so
endif
endif

RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libext4_utils.so
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libaosprecovery.so
ifneq ($(TW_INCLUDE_JPEG),)
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libjpeg.so
endif
RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libselinux.so
ifeq ($(BUILD_ID), GINGERBREAD)
    TW_NO_EXFAT := true
endif
ifneq ($(TW_NO_EXFAT), true)
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/mkexfatfs
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/fsck.exfat
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libexfat_twrp.so
else
    TW_NO_EXFAT_FUSE := true
endif
ifneq ($(TW_NO_EXFAT_FUSE), true)
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/exfat-fuse
endif
ifeq ($(TW_INCLUDE_BLOBPACK), true)
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/blobpack
endif
ifeq ($(TW_INCLUDE_INJECTTWRP), true)
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/injecttwrp
endif
ifeq ($(TW_INCLUDE_DUMLOCK), true)
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/htcdumlock
endif
ifeq ($(TW_INCLUDE_CRYPTO), true)
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 29; echo $$?),0)
        RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/lib64/libcryptfsfde.so
        RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/lib64/libdexfile_support.so
        RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/lib64/libf2fs_sparseblock.so
        RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../vendor/lib64/libnos_transport.so
        RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../vendor/lib64/libnos_datagram.so
    else
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libcryptfsfde.so
    endif
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libcrypto.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libhardware.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libgpt_twrp.so
    ifeq ($(TARGET_HW_DISK_ENCRYPTION),true)
        ifeq ($(TARGET_CRYPTFS_HW_PATH),)
            RELINK_SOURCE_FILES += $(TARGET_OUT_VENDOR_SHARED_LIBRARIES)/libcryptfs_hw.so
        else
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libcryptfs_hw.so
        endif
    endif
    # FBE files
    ifeq ($(TW_INCLUDE_CRYPTO_FBE), true)
        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 29; echo $$?),0)
            RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/lib64/libtwrpfscrypt.so
        else
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libe4crypt.so
        endif
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libgatekeeper.so
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libkeymaster_messages.so
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libkeystore_binder.so
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libbinder.so
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libprotobuf-cpp-lite.so
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libsoftkeymasterdevice.so
        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 25; echo $$?),0)
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/android.hardware.gatekeeper@1.0.so
            RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/hwservicemanager
            RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/avbctl
            RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/keystore
            RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/keystore_cli
            RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/servicemanager
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/android.system.wifi.keystore@1.0.so
            ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
                RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/android.hardware.vibrator@1.0.so
                RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/android.hardware.vibrator@1.1.so
                RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/android.hardware.vibrator@1.2.so
            endif

            ifneq ($(wildcard system/keymaster/keymaster_stl.cpp),)
                RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libkeymaster_portable.so
                RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libkeymaster_staging.so
            endif
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libwifikeystorehal.so
            ifneq ($(wildcard hardware/interfaces/weaver/Android.bp),)
                RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/android.hardware.weaver@1.0.so
            endif
            ifneq ($(wildcard hardware/interfaces/weaver/1.0/Android.bp),)
                RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/android.hardware.weaver@1.0.so
            endif
            ifneq ($(wildcard hardware/interfaces/confirmationui/1.0/Android.bp),)
                RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/android.hardware.confirmationui@1.0.so
            endif

            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libhardware_legacy.so
        else
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libkeymaster1.so
        endif
        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 28; echo $$?),0)
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libsoftkeymaster.so
        endif
        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/android.hardware.keymaster@4.0.so
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libkeymaster4support.so
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libkeystore_aidl.so
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libkeystore_parcelables.so
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libutilscallstack.so
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libunwindstack.so
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libdexfile.so
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libservices.so
            RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libkeymaster_portable.so
         endif
         # lshal can be useful for seeing if you have things like the keymaster working properly, but it isn't needed for TWRP to work
         #RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/lshal
         #RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/liblshal.so
         #RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libssl.so
         #RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libhidl-gen-hash.so
    endif
endif
ifeq ($(AB_OTA_UPDATER), true)
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/system/bin/update_engine_sideload
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/android.hardware.boot@1.0.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/bootctl
    ifneq ($(TW_INCLUDE_CRYPTO), true)
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libhardware.so
    endif
endif
ifeq ($(TARGET_USERIMAGES_USE_EXT4), true)
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 28; echo $$?),0)
        RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/make_ext4fs
    endif
endif
ifneq ($(wildcard system/core/libsparse/Android.*),)
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libsparse.so
endif
ifneq ($(TW_EXCLUDE_ENCRYPTED_BACKUPS), true)
    RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/openaes
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libopenaes.so
endif
ifeq ($(TARGET_USERIMAGES_USE_F2FS), true)
    ifeq ($(shell test $(CM_PLATFORM_SDK_VERSION) -ge 4; echo $$?),0)
        RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/mkfs.f2fs
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libf2fs.so
    else ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26; echo $$?),0)
        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 29; echo $$?),0)
            RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/make_f2fs
        else
            RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/mkfs.f2fs
        endif
        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
            ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 29; echo $$?),0)
                RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/sload_f2fs
            else
                RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/sload.f2fs
            endif
        endif
    else ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?),0)
        RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/mkfs.f2fs
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libf2fs.so
    else ifeq ($(shell test $(PLATFORM_SDK_VERSION) -eq 23; echo $$?),0)
        RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/mkfs.f2fs
    else ifneq (,$(filter $(PLATFORM_SDK_VERSION), 21 22))
        RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT_SBIN)/mkfs.f2fs
    else
        RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/mkfs.f2fs
    endif
    RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/fsck.f2fs
endif
ifneq ($(wildcard system/core/reboot/Android.*),)
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 29; echo $$?),0)
        RELINK_SOURCE_FILES += $(TARGET_RECOVERY_ROOT_OUT)/sbin/reboot
    else
        RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/reboot
    endif
endif
ifneq ($(TW_DISABLE_TTF), true)
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libft2.so
endif
ifneq ($(TW_RECOVERY_ADDITIONAL_RELINK_FILES),)
    RELINK_SOURCE_FILES += $(TW_RECOVERY_ADDITIONAL_RELINK_FILES)
endif
ifneq ($(wildcard external/pcre/Android.mk),)
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libpcre.so
endif
ifeq ($(TW_INCLUDE_NTFS_3G),true)
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 22; echo $$?),0)
    RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/mount.ntfs
    RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/fsck.ntfs
    RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/mkfs.ntfs
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libntfs-3g.so
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libfuse-lite.so
    else
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libfuse.so
    endif
else
    RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/ntfs-3g
    RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/ntfsfix
    RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/mkntfs
endif
endif
ifeq ($(BOARD_HAS_NO_REAL_SDCARD),)
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 22; echo $$?),0)
        RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/sgdisk
    endif
endif
ifeq ($(TWRP_INCLUDE_LOGCAT), true)
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 29; echo $$?),0)
        RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/bin/logcat
    else
        RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/logcat
    endif
    ifeq ($(TARGET_USES_LOGD), true)
        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 29; echo $$?),0)
            RELINK_SOURCE_FILES += $(TARGET_ROOT_OUT)/../system/bin/logd
        else
            RELINK_SOURCE_FILES += $(TARGET_OUT_EXECUTABLES)/logd
        endif
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libsysutils.so
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libnl.so
    endif
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?),0)
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libpcrecpp.so
    endif
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26; echo $$?),0)
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/liblogcat.so
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libcap.so
    endif
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 25; echo $$?),0)
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libpcre2.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libvndksupport.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libhwbinder.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libhidlbase.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libhidltransport.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/android.hardware.keymaster@3.0.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libziparchive.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libext2_blkid.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libext2_quota.so

    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libhidl-gen-utils.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libvintf.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libtinyxml2.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/android.hidl.token@1.0.so
    ifneq ($(wildcard system/core/libkeyutils/Android.bp),)
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libkeyutils.so
    endif
    ifeq ($(wildcard system/libhidl/transport/HidlTransportUtils.cpp),)
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/android.hidl.base@1.0.so
    endif
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 27; echo $$?),0)
    ifeq ($(TARGET_ARCH), arm64)
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libclang_rt.ubsan_standalone-aarch64-android.so
    endif
    ifeq ($(TARGET_ARCH), arm)
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libclang_rt.ubsan_standalone-arm-android.so
    endif
    ifeq ($(TARGET_ARCH), x86_64)
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libclang_rt.ubsan_standalone-x86_64-android.so
    endif
    ifeq ($(TARGET_ARCH), x86)
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libclang_rt.ubsan_standalone-i686-android.so
    endif
    ifeq ($(TARGET_ARCH), mips)
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libclang_rt.ubsan_standalone-mips-android.so
    endif
    ifeq ($(TARGET_ARCH), mips64)
        RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libclang_rt.ubsan_standalone-mips64-android.so
    endif
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/liblogwrap.so
    RELINK_SOURCE_FILES += $(TARGET_OUT_SHARED_LIBRARIES)/libext2_misc.so
endif

#toybox links
toybox_links := acpi base64 basename bc blockdev cal cat chcon chgrp chmod chown chroot chrt cksum clear \
    cmp comm cp cpio cut date dd devmem df diff dirname dmesg dos2unix du echo env expand expr fallocate \
    false file find flock fmt free fsync getconf getenforce getprop groups gunzip gzip head hostname hwclock i2cdetect \
    i2cdump i2cget i2cset iconv id ifconfig inotifyd insmod install ionice iorenice kill killall ln load_policy \
    log logname losetup ls lsmod lsof lspci lsusb md5sum microcom mkdir mkfifo mknod mkswap mktemp modinfo modprobe \
    more mount mountpoint mv nc netcat netstat nice nl nohup nproc nsenter od paste patch pgrep pidof pkill pmap \
    printenv printf ps pwd readlink realpath renice restorecon rm rmdir rmmod runcon sed sendevent seq setenforce \
    setprop setsid sha1sum sha224sum sha256sum sha384sum sha512sum sleep sort split start stat stop strings stty \
    swapoff swapon sync sysctl tac tail tar taskset tee time timeout top touch tr true truncate tty ulimit \
    umount uname uniq unix2dos unlink unshare uptime usleep uudecode uuencode uuidgen vmstat watch wc which whoami \
    xargs xxd yes zcat
TOYBOX_LINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/system/bin/, $(toybox_links))

#relink recovery executables linker to /sbin and move symlinks
include $(CLEAR_VARS)
LOCAL_MODULE := relink
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_POST_INSTALL_CMD += $(RELINK) $(TARGET_RECOVERY_ROOT_OUT)/sbin $(RELINK_SOURCE_FILES) && \
    cp $(TOYBOX_LINKS) $(TARGET_RECOVERY_ROOT_OUT)/sbin/
TARGET_RELINK_FILES := $(notdir $(RELINK_SOURCE_FILES))
TARGET_BASE_RELINK_MODULES := $(basename $(TARGET_RELINK_FILES))
TARGET_RELINK_MODULES :=  $(filter-out libdexfile, $(TARGET_BASE_RELINK_MODULES))
LOCAL_REQUIRED_MODULES += $(TARGET_RELINK_MODULES)
include $(BUILD_PHONY_PACKAGE)

#relink init
include $(CLEAR_VARS)
LOCAL_MODULE := twrp_ramdisk
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)
RELINK_INIT := $(TARGET_RECOVERY_ROOT_OUT)/system/bin/init
LOCAL_POST_INSTALL_CMD += $(RELINK) $(TARGET_RECOVERY_ROOT_OUT)/ $(RELINK_INIT) && \
    cp $(TARGET_RECOVERY_ROOT_OUT)/system/bin/ueventd $(TARGET_RECOVERY_ROOT_OUT)/sbin/ && \
    ln -sf /init $(TARGET_RECOVERY_ROOT_OUT)/sbin/init && \
    ln -sf /init $(TARGET_RECOVERY_ROOT_OUT)/system/bin/init && \
    mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/system/etc/selinux/ && \
    cp $(TARGET_ROOT_OUT)/../system/etc/selinux/plat_service_contexts $(TARGET_RECOVERY_ROOT_OUT)/system/etc/selinux/plat_service_contexts && \
    cp $(TARGET_ROOT_OUT)/../system/etc/selinux/plat_hwservice_contexts $(TARGET_RECOVERY_ROOT_OUT)/system/etc/selinux/plat_hwservice_contexts && \
    cp $(TARGET_ROOT_OUT)/../vendor/etc/selinux/vndservice_contexts $(TARGET_RECOVERY_ROOT_OUT)/system/etc/selinux/vndservice_contexts && \
    cp $(TARGET_ROOT_OUT)/../vendor/etc/selinux/vendor_hwservice_contexts $(TARGET_RECOVERY_ROOT_OUT)/system/etc/selinux/vendor_hwservice_contexts
LOCAL_REQUIRED_MODULES := init_second_stage.recovery reboot.recovery plat_service_contexts plat_hardware_contexts vndservice_contexts
include $(BUILD_PHONY_PACKAGE)

#mke2fs.conf
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 26; echo $$?),0)
    include $(CLEAR_VARS)
    LOCAL_MODULE := mke2fs.conf
    LOCAL_MODULE_TAGS := optional
    LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
    LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/etc
    LOCAL_SRC_FILES := $(LOCAL_MODULE)
    include $(BUILD_PREBUILT)
endif

ifeq ($(BOARD_HAS_NO_REAL_SDCARD),)
	ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
	    #prebuilt, static sgdisk
	    include $(CLEAR_VARS)
	    LOCAL_MODULE := sgdisk_static
	    LOCAL_MODULE_STEM := sgdisk
	    LOCAL_MODULE_TAGS := optional
	    LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
	    LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
	    LOCAL_SRC_FILES := $(LOCAL_MODULE)
	    include $(BUILD_PREBUILT)
	endif
	#parted
	#include $(CLEAR_VARS)
	#LOCAL_MODULE := parted
	#LOCAL_MODULE_TAGS := optional
	#LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
	#LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
	#LOCAL_SRC_FILES := $(LOCAL_MODULE)
	#include $(BUILD_PREBUILT)
endif

# copy license file for OpenAES
ifneq ($(TW_EXCLUDE_ENCRYPTED_BACKUPS), true)
	include $(CLEAR_VARS)
	LOCAL_MODULE := openaes_license
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/license/openaes
	LOCAL_SRC_FILES := ../openaes/LICENSE
	include $(BUILD_PREBUILT)
endif

ifeq ($(TW_INCLUDE_DUMLOCK), true)
	#htcdumlock for /system for dumlock
	include $(CLEAR_VARS)
	LOCAL_MODULE := htcdumlocksys
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)$(TWHTCD_PATH)
	LOCAL_SRC_FILES := $(LOCAL_MODULE)
	include $(BUILD_PREBUILT)

	#flash_image for /system for dumlock
	include $(CLEAR_VARS)
	LOCAL_MODULE := flash_imagesys
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)$(TWHTCD_PATH)
	LOCAL_SRC_FILES := $(LOCAL_MODULE)
	include $(BUILD_PREBUILT)

	#dump_image for /system for dumlock
	include $(CLEAR_VARS)
	LOCAL_MODULE := dump_imagesys
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)$(TWHTCD_PATH)
	LOCAL_SRC_FILES := $(LOCAL_MODULE)
	include $(BUILD_PREBUILT)

	#libbmlutils for /system for dumlock
	include $(CLEAR_VARS)
	LOCAL_MODULE := libbmlutils.so
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)$(TWHTCD_PATH)
	LOCAL_SRC_FILES := $(LOCAL_MODULE)
	include $(BUILD_PREBUILT)

	#libflashutils for /system for dumlock
	include $(CLEAR_VARS)
	LOCAL_MODULE := libflashutils.so
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)$(TWHTCD_PATH)
	LOCAL_SRC_FILES := $(LOCAL_MODULE)
	include $(BUILD_PREBUILT)

	#libmmcutils for /system for dumlock
	include $(CLEAR_VARS)
	LOCAL_MODULE := libmmcutils.so
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)$(TWHTCD_PATH)
	LOCAL_SRC_FILES := $(LOCAL_MODULE)
	include $(BUILD_PREBUILT)

	#libmtdutils for /system for dumlock
	include $(CLEAR_VARS)
	LOCAL_MODULE := libmtdutils.so
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)$(TWHTCD_PATH)
	LOCAL_SRC_FILES := $(LOCAL_MODULE)
	include $(BUILD_PREBUILT)

	#HTCDumlock.apk
	include $(CLEAR_VARS)
	LOCAL_MODULE := HTCDumlock.apk
	LOCAL_MODULE_TAGS := optional
	LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)$(TWHTCD_PATH)
	LOCAL_SRC_FILES := $(LOCAL_MODULE)
	include $(BUILD_PREBUILT)
endif

ifeq ($(TW_USE_TOOLBOX), true)
   include $(CLEAR_VARS)
   LOCAL_MODULE := mkshrc_twrp
   LOCAL_MODULE_STEM := mkshrc
   LOCAL_MODULE_TAGS := optional
   LOCAL_MODULE_CLASS := ETC
   LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/etc
   LOCAL_SRC_FILES := $(LOCAL_MODULE)
   include $(BUILD_PREBUILT)
endif

#TWRP App "placeholder"
include $(CLEAR_VARS)
LOCAL_MODULE := me.twrp.twrpapp.apk
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)

#TWRP App permissions for Android 9+
include $(CLEAR_VARS)
LOCAL_MODULE := privapp-permissions-twrpapp.xml
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)

ifeq ($(TW_INCLUDE_CRYPTO), true)
    ifneq ($(TW_CRYPTO_USE_SYSTEM_VOLD),)
        ifneq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
            # Prebuilt vdc_pie for pre-Pie SDK Platforms
            include $(CLEAR_VARS)
            LOCAL_MODULE := vdc_pie
            LOCAL_MODULE_TAGS := optional
            LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
            LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
            LOCAL_SRC_FILES := vdc_pie-$(TARGET_ARCH)
            include $(BUILD_PREBUILT)
        endif
    endif
endif

ifneq (,$(filter $(TW_INCLUDE_REPACKTOOLS) $(TW_INCLUDE_RESETPROP) $(TW_INCLUDE_LIBRESETPROP), true))
    ifeq ($(wildcard external/magisk-prebuilt/Android.mk),)
        $(warning Magisk prebuilt tools not found!)
        $(warning Please place https://github.com/TeamWin/external_magisk-prebuilt)
        $(warning into external/magisk-prebuilt)
        $(error magiskboot prebuilts not present; exiting)
    endif
endif
