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

package com.example.android.systemupdatersample.updates;

import android.os.UpdateEngine;

import com.example.android.systemupdatersample.PayloadSpec;
import com.example.android.systemupdatersample.UpdateConfig;
import com.example.android.systemupdatersample.util.PayloadSpecs;

/**
 * Applies A/B (seamless) non-streaming update.
 */
public class AbNonStreamingUpdate {

    private final UpdateEngine mUpdateEngine;
    private final UpdateConfig mUpdateConfig;

    public AbNonStreamingUpdate(UpdateEngine updateEngine, UpdateConfig config) {
        this.mUpdateEngine = updateEngine;
        this.mUpdateConfig = config;
    }

    /**
     * Start applying the update. This method doesn't wait until end of the update.
     * {@code update_engine} works asynchronously.
     */
    public void execute() throws Exception {
        PayloadSpec payload = PayloadSpecs.forNonStreaming(mUpdateConfig.getUpdatePackageFile());

        mUpdateEngine.applyPayload(
                payload.getUrl(),
                payload.getOffset(),
                payload.getSize(),
                payload.getProperties().toArray(new String[0]));
    }

}
