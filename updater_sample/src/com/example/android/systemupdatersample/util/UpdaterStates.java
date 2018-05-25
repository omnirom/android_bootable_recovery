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
 * SystemUpdaterSample app state.
 */
public class UpdaterStates {

    public static final int IDLE = 0;
    public static final int ERROR = 1;
    public static final int RUNNING = 2;
    public static final int PAUSED = 3;
    public static final int FINISHED = 4;

    private static final SparseArray<String> STATE_MAP = new SparseArray<>();

    static {
        STATE_MAP.put(0, "IDLE");
        STATE_MAP.put(1, "ERROR");
        STATE_MAP.put(2, "RUNNING");
        STATE_MAP.put(3, "PAUSED");
        STATE_MAP.put(4, "FINISHED");
    }

    /**
     * converts status code to status name
     */
    public static String getStateText(int state) {
        return STATE_MAP.get(state);
    }

    private UpdaterStates() {}
}
