/*
 * Copyright (C) 2018 The Android Open Source Project
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

package com.example.android.systemupdatersample.util;

/**
 * Utility class for properties that will be passed to {@code UpdateEngine#applyPayload}.
 */
public final class UpdateEngineProperties {

    /**
     * The property indicating that the update engine should not switch slot
     * when the device reboots.
     */
    public static final String PROPERTY_DISABLE_SWITCH_SLOT_ON_REBOOT = "SWITCH_SLOT_ON_REBOOT=0";

    /**
     * The property to skip post-installation.
     * https://source.android.com/devices/tech/ota/ab/#post-installation
     */
    public static final String PROPERTY_SKIP_POST_INSTALL = "RUN_POST_INSTALL=0";

    private UpdateEngineProperties() {}
}
