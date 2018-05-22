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

package com.example.android.systemupdatersample.ui;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.os.UpdateEngine;
import android.os.UpdateEngineCallback;
import android.util.Log;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ProgressBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import com.example.android.systemupdatersample.PayloadSpec;
import com.example.android.systemupdatersample.R;
import com.example.android.systemupdatersample.UpdateConfig;
import com.example.android.systemupdatersample.services.PrepareStreamingService;
import com.example.android.systemupdatersample.util.PayloadSpecs;
import com.example.android.systemupdatersample.util.UpdateConfigs;
import com.example.android.systemupdatersample.util.UpdateEngineErrorCodes;
import com.example.android.systemupdatersample.util.UpdateEngineProperties;
import com.example.android.systemupdatersample.util.UpdateEngineStatuses;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * UI for SystemUpdaterSample app.
 */
public class MainActivity extends Activity {

    private static final String TAG = "MainActivity";

    /** HTTP Header: User-Agent; it will be sent to the server when streaming the payload. */
    private static final String HTTP_USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
            + "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/60.0.3112.113 Safari/537.36";

    private TextView mTextViewBuild;
    private Spinner mSpinnerConfigs;
    private TextView mTextViewConfigsDirHint;
    private Button mButtonReload;
    private Button mButtonApplyConfig;
    private Button mButtonStop;
    private Button mButtonReset;
    private ProgressBar mProgressBar;
    private TextView mTextViewStatus;
    private TextView mTextViewCompletion;
    private TextView mTextViewUpdateInfo;
    private Button mButtonSwitchSlot;

    private List<UpdateConfig> mConfigs;
    private AtomicInteger mUpdateEngineStatus =
            new AtomicInteger(UpdateEngine.UpdateStatusConstants.IDLE);
    private PayloadSpec mLastPayloadSpec;
    private AtomicBoolean mManualSwitchSlotRequired = new AtomicBoolean(true);
    private final PayloadSpecs mPayloadSpecs = new PayloadSpecs();

    /**
     * Listen to {@code update_engine} events.
     */
    private UpdateEngineCallbackImpl mUpdateEngineCallback = new UpdateEngineCallbackImpl();

    private final UpdateEngine mUpdateEngine = new UpdateEngine();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        this.mTextViewBuild = findViewById(R.id.textViewBuild);
        this.mSpinnerConfigs = findViewById(R.id.spinnerConfigs);
        this.mTextViewConfigsDirHint = findViewById(R.id.textViewConfigsDirHint);
        this.mButtonReload = findViewById(R.id.buttonReload);
        this.mButtonApplyConfig = findViewById(R.id.buttonApplyConfig);
        this.mButtonStop = findViewById(R.id.buttonStop);
        this.mButtonReset = findViewById(R.id.buttonReset);
        this.mProgressBar = findViewById(R.id.progressBar);
        this.mTextViewStatus = findViewById(R.id.textViewStatus);
        this.mTextViewCompletion = findViewById(R.id.textViewCompletion);
        this.mTextViewUpdateInfo = findViewById(R.id.textViewUpdateInfo);
        this.mButtonSwitchSlot = findViewById(R.id.buttonSwitchSlot);

        this.mTextViewConfigsDirHint.setText(UpdateConfigs.getConfigsRoot(this));

        uiReset();
        loadUpdateConfigs();

        this.mUpdateEngine.bind(mUpdateEngineCallback);
    }

    @Override
    protected void onDestroy() {
        this.mUpdateEngine.unbind();
        super.onDestroy();
    }

    /**
     * reload button is clicked
     */
    public void onReloadClick(View view) {
        loadUpdateConfigs();
    }

    /**
     * view config button is clicked
     */
    public void onViewConfigClick(View view) {
        UpdateConfig config = mConfigs.get(mSpinnerConfigs.getSelectedItemPosition());
        new AlertDialog.Builder(this)
                .setTitle(config.getName())
                .setMessage(config.getRawJson())
                .setPositiveButton(R.string.close, (dialog, id) -> dialog.dismiss())
                .show();
    }

    /**
     * apply config button is clicked
     */
    public void onApplyConfigClick(View view) {
        new AlertDialog.Builder(this)
                .setTitle("Apply Update")
                .setMessage("Do you really want to apply this update?")
                .setIcon(android.R.drawable.ic_dialog_alert)
                .setPositiveButton(android.R.string.ok, (dialog, whichButton) -> {
                    uiSetUpdating();
                    applyUpdate(getSelectedConfig());
                })
                .setNegativeButton(android.R.string.cancel, null)
                .show();
    }

    /**
     * stop button clicked
     */
    public void onStopClick(View view) {
        new AlertDialog.Builder(this)
                .setTitle("Stop Update")
                .setMessage("Do you really want to cancel running update?")
                .setIcon(android.R.drawable.ic_dialog_alert)
                .setPositiveButton(android.R.string.ok, (dialog, whichButton) -> {
                    stopRunningUpdate();
                })
                .setNegativeButton(android.R.string.cancel, null).show();
    }

    /**
     * reset button clicked
     */
    public void onResetClick(View view) {
        new AlertDialog.Builder(this)
                .setTitle("Reset Update")
                .setMessage("Do you really want to cancel running update"
                        + " and restore old version?")
                .setIcon(android.R.drawable.ic_dialog_alert)
                .setPositiveButton(android.R.string.ok, (dialog, whichButton) -> {
                    resetUpdate();
                })
                .setNegativeButton(android.R.string.cancel, null).show();
    }

    /**
     * switch slot button clicked
     */
    public void onSwitchSlotClick(View view) {
        setSwitchSlotOnReboot();
    }

    /**
     * Invoked when anything changes. The value of {@code status} will
     * be one of the values from {@link UpdateEngine.UpdateStatusConstants},
     * and {@code percent} will be from {@code 0.0} to {@code 1.0}.
     */
    private void onStatusUpdate(int status, float percent) {
        mProgressBar.setProgress((int) (100 * percent));
        if (mUpdateEngineStatus.get() != status) {
            mUpdateEngineStatus.set(status);
            runOnUiThread(() -> {
                Log.e("UpdateEngine", "StatusUpdate - status="
                        + UpdateEngineStatuses.getStatusText(status)
                        + "/" + status);
                Toast.makeText(this, "Update Status changed", Toast.LENGTH_LONG)
                        .show();
                if (status == UpdateEngine.UpdateStatusConstants.IDLE) {
                    Log.d(TAG, "status changed, resetting ui");
                    uiReset();
                } else {
                    Log.d(TAG, "status changed, setting ui to updating mode");
                    uiSetUpdating();
                }
                setUiStatus(status);
            });
        }
    }

    /**
     * Invoked when the payload has been applied, whether successfully or
     * unsuccessfully. The value of {@code errorCode} will be one of the
     * values from {@link UpdateEngine.ErrorCodeConstants}.
     */
    private void onPayloadApplicationComplete(int errorCode) {
        final String state = UpdateEngineErrorCodes.isUpdateSucceeded(errorCode)
                ? "SUCCESS"
                : "FAILURE";
        runOnUiThread(() -> {
            Log.i("UpdateEngine",
                    "Completed - errorCode="
                    + UpdateEngineErrorCodes.getCodeName(errorCode) + "/" + errorCode
                    + " " + state);
            Toast.makeText(this, "Update completed", Toast.LENGTH_LONG).show();
            setUiCompletion(errorCode);
            if (errorCode == UpdateEngineErrorCodes.UPDATED_BUT_NOT_ACTIVE) {
                // if update was successfully applied.
                if (mManualSwitchSlotRequired.get()) {
                    // Show "Switch Slot" button.
                    uiShowSwitchSlotInfo();
                }
            }
        });
    }

    /** resets ui */
    private void uiReset() {
        mTextViewBuild.setText(Build.DISPLAY);
        mSpinnerConfigs.setEnabled(true);
        mButtonReload.setEnabled(true);
        mButtonApplyConfig.setEnabled(true);
        mButtonStop.setEnabled(false);
        mButtonReset.setEnabled(false);
        mProgressBar.setProgress(0);
        mProgressBar.setEnabled(false);
        mProgressBar.setVisibility(ProgressBar.INVISIBLE);
        mTextViewStatus.setText(R.string.unknown);
        mTextViewCompletion.setText(R.string.unknown);
        uiHideSwitchSlotInfo();
    }

    /** sets ui updating mode */
    private void uiSetUpdating() {
        mTextViewBuild.setText(Build.DISPLAY);
        mSpinnerConfigs.setEnabled(false);
        mButtonReload.setEnabled(false);
        mButtonApplyConfig.setEnabled(false);
        mButtonStop.setEnabled(true);
        mProgressBar.setEnabled(true);
        mButtonReset.setEnabled(true);
        mProgressBar.setVisibility(ProgressBar.VISIBLE);
    }

    private void uiShowSwitchSlotInfo() {
        mButtonSwitchSlot.setEnabled(true);
        mTextViewUpdateInfo.setTextColor(Color.parseColor("#777777"));
    }

    private void uiHideSwitchSlotInfo() {
        mTextViewUpdateInfo.setTextColor(Color.parseColor("#AAAAAA"));
        mButtonSwitchSlot.setEnabled(false);
    }

    /**
     * loads json configurations from configs dir that is defined in {@link UpdateConfigs}.
     */
    private void loadUpdateConfigs() {
        mConfigs = UpdateConfigs.getUpdateConfigs(this);
        loadConfigsToSpinner(mConfigs);
    }

    /**
     * @param status update engine status code
     */
    private void setUiStatus(int status) {
        String statusText = UpdateEngineStatuses.getStatusText(status);
        mTextViewStatus.setText(statusText + "/" + status);
    }

    /**
     * @param errorCode update engine error code
     */
    private void setUiCompletion(int errorCode) {
        final String state = UpdateEngineErrorCodes.isUpdateSucceeded(errorCode)
                ? "SUCCESS"
                : "FAILURE";
        String errorText = UpdateEngineErrorCodes.getCodeName(errorCode);
        mTextViewCompletion.setText(state + " " + errorText + "/" + errorCode);
    }

    private void loadConfigsToSpinner(List<UpdateConfig> configs) {
        String[] spinnerArray = UpdateConfigs.configsToNames(configs);
        ArrayAdapter<String> spinnerArrayAdapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_item,
                spinnerArray);
        spinnerArrayAdapter.setDropDownViewResource(android.R.layout
                .simple_spinner_dropdown_item);
        mSpinnerConfigs.setAdapter(spinnerArrayAdapter);
    }

    private UpdateConfig getSelectedConfig() {
        return mConfigs.get(mSpinnerConfigs.getSelectedItemPosition());
    }

    /**
     * Applies the given update
     */
    private void applyUpdate(final UpdateConfig config) {
        List<String> extraProperties = new ArrayList<>();

        if (!config.getAbConfig().getForceSwitchSlot()) {
            // Disable switch slot on reboot, which is enabled by default.
            // User will enable it manually by clicking "Switch Slot" button on the screen.
            extraProperties.add(UpdateEngineProperties.PROPERTY_DISABLE_SWITCH_SLOT_ON_REBOOT);
            mManualSwitchSlotRequired.set(true);
        } else {
            mManualSwitchSlotRequired.set(false);
        }

        if (config.getInstallType() == UpdateConfig.AB_INSTALL_TYPE_NON_STREAMING) {
            PayloadSpec payload;
            try {
                payload = mPayloadSpecs.forNonStreaming(config.getUpdatePackageFile());
            } catch (IOException e) {
                Log.e(TAG, "Error creating payload spec", e);
                Toast.makeText(this, "Error creating payload spec", Toast.LENGTH_LONG)
                        .show();
                return;
            }
            updateEngineApplyPayload(payload, extraProperties);
        } else {
            Log.d(TAG, "Starting PrepareStreamingService");
            PrepareStreamingService.startService(this, config, (code, payloadSpec) -> {
                if (code == PrepareStreamingService.RESULT_CODE_SUCCESS) {
                    extraProperties.add("USER_AGENT=" + HTTP_USER_AGENT);
                    config.getStreamingMetadata()
                            .getAuthorization()
                            .ifPresent(s -> extraProperties.add("AUTHORIZATION=" + s));
                    updateEngineApplyPayload(payloadSpec, extraProperties);
                } else {
                    Log.e(TAG, "PrepareStreamingService failed, result code is " + code);
                    Toast.makeText(
                            MainActivity.this,
                            "PrepareStreamingService failed, result code is " + code,
                            Toast.LENGTH_LONG).show();
                }
            });
        }
    }

    /**
     * Applies given payload.
     *
     * UpdateEngine works asynchronously. This method doesn't wait until
     * end of the update.
     *
     * @param payloadSpec contains url, offset and size to {@code PAYLOAD_BINARY_FILE_NAME}
     * @param extraProperties additional properties to pass to {@link UpdateEngine#applyPayload}
     */
    private void updateEngineApplyPayload(PayloadSpec payloadSpec, List<String> extraProperties) {
        mLastPayloadSpec = payloadSpec;

        ArrayList<String> properties = new ArrayList<>(payloadSpec.getProperties());
        if (extraProperties != null) {
            properties.addAll(extraProperties);
        }
        try {
            mUpdateEngine.applyPayload(
                    payloadSpec.getUrl(),
                    payloadSpec.getOffset(),
                    payloadSpec.getSize(),
                    properties.toArray(new String[0]));
        } catch (Exception e) {
            Log.e(TAG, "UpdateEngine failed to apply the update", e);
            Toast.makeText(
                    this,
                    "UpdateEngine failed to apply the update",
                    Toast.LENGTH_LONG).show();
        }
    }

    /**
     * Sets the new slot that has the updated partitions as the active slot,
     * which device will boot into next time.
     * This method is only supposed to be called after the payload is applied.
     *
     * Invoking {@link UpdateEngine#applyPayload} with the same payload url, offset, size
     * and payload metadata headers doesn't trigger new update. It can be used to just switch
     * active A/B slot.
     *
     * {@link UpdateEngine#applyPayload} might take several seconds to finish, and it will
     * invoke callbacks {@link this#onStatusUpdate} and {@link this#onPayloadApplicationComplete)}.
     */
    private void setSwitchSlotOnReboot() {
        Log.d(TAG, "setSwitchSlotOnReboot invoked");
        List<String> extraProperties = new ArrayList<>();
        // PROPERTY_SKIP_POST_INSTALL should be passed on to skip post-installation hooks.
        extraProperties.add(UpdateEngineProperties.PROPERTY_SKIP_POST_INSTALL);
        // It sets property SWITCH_SLOT_ON_REBOOT=1 by default.
        // HTTP headers are not required, UpdateEngine is not expected to stream payload.
        updateEngineApplyPayload(mLastPayloadSpec, extraProperties);
        uiHideSwitchSlotInfo();
    }

    /**
     * Requests update engine to stop any ongoing update. If an update has been applied,
     * leave it as is.
     */
    private void stopRunningUpdate() {
        try {
            mUpdateEngine.cancel();
        } catch (Exception e) {
            Log.w(TAG, "UpdateEngine failed to stop the ongoing update", e);
        }
    }

    /**
     * Resets update engine to IDLE state. Requests to cancel any onging update, or to revert if an
     * update has been applied.
     */
    private void resetUpdate() {
        try {
            mUpdateEngine.resetStatus();
        } catch (Exception e) {
            Log.w(TAG, "UpdateEngine failed to reset the update", e);
        }
    }

    /**
     * Helper class to delegate {@code update_engine} callbacks to MainActivity
     */
    class UpdateEngineCallbackImpl extends UpdateEngineCallback {
        @Override
        public void onStatusUpdate(int status, float percent) {
            MainActivity.this.onStatusUpdate(status, percent);
        }

        @Override
        public void onPayloadApplicationComplete(int errorCode) {
            MainActivity.this.onPayloadApplicationComplete(errorCode);
        }
    }

}
