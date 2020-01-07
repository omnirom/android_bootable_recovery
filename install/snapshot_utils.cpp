
/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <libsnapshot/snapshot.h>

#include "recovery_ui/device.h"
#include "recovery_ui/ui.h"
#include "recovery_utils/roots.h"

using android::snapshot::CreateResult;
using android::snapshot::SnapshotManager;

bool FinishPendingSnapshotMerges(Device* device) {
  if (!android::base::GetBoolProperty("ro.virtual_ab.enabled", false)) {
    return true;
  }

  RecoveryUI* ui = device->GetUI();
  auto sm = SnapshotManager::NewForFirstStageMount();
  if (!sm) {
    ui->Print("Could not create SnapshotManager.\n");
    return false;
  }

  auto callback = [&]() -> void {
    double progress;
    sm->GetUpdateState(&progress);
    ui->Print("Waiting for merge to complete: %.2f\n", progress);
  };
  if (!sm->HandleImminentDataWipe(callback)) {
    ui->Print("Unable to check merge status and/or complete update merge.\n");
    return false;
  }
  return true;
}

bool CreateSnapshotPartitions() {
  if (!android::base::GetBoolProperty("ro.virtual_ab.enabled", false)) {
    // If the device does not support Virtual A/B, there's no need to create
    // snapshot devices.
    return true;
  }

  auto sm = SnapshotManager::NewForFirstStageMount();
  if (!sm) {
    // SnapshotManager could not be created. The device is still in a
    // consistent state and can continue with the mounting of the existing
    // devices, but cannot initialize snapshot devices.
    LOG(WARNING) << "Could not create SnapshotManager";
    return true;
  }

  auto ret = sm->RecoveryCreateSnapshotDevices();
  if (ret == CreateResult::ERROR) {
    return false;
  }
  return true;
}
