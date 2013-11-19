# Auto-generated - DO NOT EDIT!
# To regenerate, edit scrypt.config, then run:
#     ./import_scrypt.sh import /path/to/scrypt-1.1.6.tar.gz
#
# Before including this file, the local Android.mk must define the following
# variables:
#
#    local_c_flags
#    local_c_includes
#    local_additional_dependencies
#
# This script will define the following variables:
#
#    target_c_flags
#    target_c_includes
#    target_src_files
#
#    host_c_flags
#    host_c_includes
#    host_src_files
#

# Ensure these are empty.
unknown_arch_c_flags :=
unknown_arch_src_files :=
unknown_arch_exclude_files :=


common_c_flags :=

common_src_files := \
  lib/crypto/crypto_scrypt-ref.c \

common_c_includes := \
  lib/crypto \
  lib/util \

arm_c_flags :=

arm_src_files :=

arm_exclude_files :=

arm_neon_c_flags :=

arm_neon_src_files := \
  lib/crypto/crypto_scrypt-neon.c \

arm_neon_exclude_files := \
  lib/crypto/crypto_scrypt-ref.c \

x86_c_flags :=

x86_src_files := \
  lib/crypto/crypto_scrypt-sse.c \

x86_exclude_files := \
  lib/crypto/crypto_scrypt-ref.c \

x86_64_c_flags :=

x86_64_src_files := \
  lib/crypto/crypto_scrypt-sse.c \

x86_64_exclude_files := \
  lib/crypto/crypto_scrypt-ref.c \

mips_c_flags :=

mips_src_files :=

mips_exclude_files :=

target_arch := $(TARGET_ARCH)
ifeq ($(target_arch)-$(TARGET_HAS_BIGENDIAN),mips-true)
target_arch := unknown_arch
endif

target_c_flags    := $(common_c_flags) $($(target_arch)_c_flags) $(local_c_flags)
target_c_includes := $(addprefix bootable/recovery/crypto/scrypt/,$(common_c_includes)) $(local_c_includes)
target_src_files  := $(common_src_files) $($(target_arch)_src_files)
target_src_files  := $(filter-out $($(target_arch)_exclude_files), $(target_src_files))

# Hacks for ARM NEON support
ifeq ($(target_arch),arm)
ifeq ($(ARCH_ARM_HAVE_NEON),true)
target_c_flags   += $(arm_neon_c_flags)
target_src_files += $(arm_neon_src_files)
target_src_files := $(filter-out $(arm_neon_exclude_files), $(target_src_files))
endif
endif

ifeq ($(HOST_OS)-$(HOST_ARCH),linux-x86)
host_arch := x86
else
host_arch := unknown_arch
endif

host_c_flags    := $(common_c_flags) $($(host_arch)_c_flags) $(local_c_flags)
host_c_includes := $(addprefix bootable/recovery/crypto/scrypt/,$(common_c_includes)) $(local_c_includes)
host_src_files  := $(common_src_files) $($(host_arch)_src_files)
host_src_files  := $(filter-out $($(host_arch)_exclude_files), $(host_src_files))

local_additional_dependencies += $(LOCAL_PATH)/Scrypt-config.mk

