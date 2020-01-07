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

#pragma once

#include "recovery_ui/device.h"

bool FinishPendingSnapshotMerges(Device* device);

/*
 * This function tries to create the snapshotted devices in the case a Virtual
 * A/B device is updating.
 * The function returns false in case of critical failure that would prevent
 * the further mountings of devices, or true in case of success, if either the
 * devices were created or there was no need to.
 */
bool CreateSnapshotPartitions();
