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

package com.android.update.ui;

import android.app.Activity;
import android.os.UpdateEngine;
import android.os.UpdateEngineCallback;

/** Main update activity. */
public class SystemUpdateActivity extends Activity {

  private UpdateEngine updateEngine;
  private UpdateEngineCallbackImpl updateEngineCallbackImpl = new UpdateEngineCallbackImpl(this);

  @Override
  public void onResume() {
    super.onResume();
    updateEngine = new UpdateEngine();
    updateEngine.bind(updateEngineCallbackImpl);
  }

  @Override
  public void onPause() {
    updateEngine.unbind();
    super.onPause();
  }

  void onStatusUpdate(int i, float v) {
    // Handle update engine status update
  }

  void onPayloadApplicationComplete(int i) {
    // Handle apply payload completion
  }

  private static class UpdateEngineCallbackImpl extends UpdateEngineCallback {

    private final SystemUpdateActivity activity;

    public UpdateEngineCallbackImpl(SystemUpdateActivity activity) {
      this.activity = activity;
    }

    @Override
    public void onStatusUpdate(int i, float v) {
      activity.onStatusUpdate(i, v);
    }

    @Override
    public void onPayloadApplicationComplete(int i) {
      activity.onPayloadApplicationComplete(i);
    }
  }
}
