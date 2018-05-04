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

import android.util.SparseArray;

/**
 * Helper class to work with update_engine's error codes.
 * Many error codes are defined in  {@code UpdateEngine.UpdateStatusConstants},
 * but you can find more in system/update_engine/common/error_code.h.
 */
public final class UpdateEngineStatuses {

    private static final SparseArray<String> STATUS_MAP = new SparseArray<>();

    static {
        STATUS_MAP.put(0, "IDLE");
        STATUS_MAP.put(1, "CHECKING_FOR_UPDATE");
        STATUS_MAP.put(2, "UPDATE_AVAILABLE");
        STATUS_MAP.put(3, "DOWNLOADING");
        STATUS_MAP.put(4, "VERIFYING");
        STATUS_MAP.put(5, "FINALIZING");
        STATUS_MAP.put(6, "UPDATED_NEED_REBOOT");
        STATUS_MAP.put(7, "REPORTING_ERROR_EVENT");
        STATUS_MAP.put(8, "ATTEMPTING_ROLLBACK");
        STATUS_MAP.put(9, "DISABLED");
    }

    /**
     * converts status code to status name
     */
    public static String getStatusText(int status) {
        return STATUS_MAP.get(status);
    }

    private UpdateEngineStatuses() {}
}
