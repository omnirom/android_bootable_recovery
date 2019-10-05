# Copyright (C) 2007 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# If you don't need to do a full clean build but would like to touch
# a file or delete some intermediate files, add a clean step to the end
# of the list.  These steps will only be run once, if they haven't been
# run before.
#
# E.g.:
#     $(call add-clean-step, touch -c external/sqlite/sqlite3.h)
#     $(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/STATIC_LIBRARIES/libz_intermediates)
#
# Always use "touch -c" and "rm -f" or "rm -rf" to gracefully deal with
# files that are missing or have been moved.
#
# Use $(PRODUCT_OUT) to get to the "out/target/product/blah/" directory.
# Use $(OUT_DIR) to refer to the "out" directory.
#
# If you need to re-do something that's already mentioned, just copy
# the command and add it to the bottom of the list.  E.g., if a change
# that you made last week required touching a file and a change you
# made today requires touching the same file, just copy the old
# touch step and add it to the end of the list.
#
# ************************************************
# NEWER CLEAN STEPS MUST BE AT THE END OF THE LIST
# ************************************************

# For example:
#$(call add-clean-step, rm -rf $(OUT_DIR)/target/common/obj/APPS/AndroidTests_intermediates)
#$(call add-clean-step, rm -rf $(OUT_DIR)/target/common/obj/JAVA_LIBRARIES/core_intermediates)
#$(call add-clean-step, find $(OUT_DIR) -type f -name "IGTalkSession*" -print0 | xargs -0 rm -f)
#$(call add-clean-step, rm -rf $(PRODUCT_OUT)/data/*)

$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/EXECUTABLES/recovery_intermediates)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/STATIC_LIBRARIES/libminui_intermediates/import_includes)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/recovery/root/sbin)

$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/SHARED_LIBRARIES/libinstall.recovery_intermediates)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/recovery/root/system/lib64/libinstall.so)

$(call add-clean-step, rm -rf $(PRODUCT_OUT)/data/nativetest/recovery_component_test)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/data/nativetest64/recovery_component_test)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/testcases/recovery_component_test)

$(call add-clean-step, find $(OUT_DIR) -type f -name "SystemUpdaterSample*" -print0 | xargs -0 rm -f)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/system/priv-app/SystemUpdaterSample)

$(call add-clean-step, rm -rf $(PRODUCT_OUT)/system/lib*/libbrotli.so)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/system/lib*/libbz.so)

# Move recovery resources from /system to /vendor.
$(call add-clean-step, rm -f $(PRODUCT_OUT)/system/bin/applypatch)
$(call add-clean-step, rm -r $(PRODUCT_OUT)/symbols/system/bin/applypatch)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/PACKAGING/target_files_intermediates/*-target_files-*/SYSTEM/bin/applypatch)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/PACKAGING/target_files_intermediates/*-target_files-*/SYSTEM/bin/install-recovery.sh)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/PACKAGING/target_files_intermediates/*-target_files-*/SYSTEM/etc/recovery-resource.dat)
$(call add-clean-step, rm -rf $(PRODUCT_OUT)/obj/PACKAGING/target_files_intermediates/*-target_files-*/SYSTEM/recovery-from-boot.p)

# ************************************************
# NEWER CLEAN STEPS MUST BE AT THE END OF THE LIST
# ************************************************
