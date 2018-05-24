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
import android.util.Log;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ProgressBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import com.example.android.systemupdatersample.R;
import com.example.android.systemupdatersample.UpdateConfig;
import com.example.android.systemupdatersample.UpdateManager;
import com.example.android.systemupdatersample.util.PayloadSpecs;
import com.example.android.systemupdatersample.util.UpdateConfigs;
import com.example.android.systemupdatersample.util.UpdateEngineErrorCodes;
import com.example.android.systemupdatersample.util.UpdateEngineStatuses;

import java.util.List;

/**
 * UI for SystemUpdaterSample app.
 */
public class MainActivity extends Activity {

    private static final String TAG = "MainActivity";

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

    private final UpdateManager mUpdateManager =
            new UpdateManager(new UpdateEngine(), new PayloadSpecs());

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

        this.mUpdateManager.setOnEngineStatusUpdateCallback(this::onStatusUpdate);
        this.mUpdateManager.setOnProgressUpdateCallback(this::onProgressUpdate);
        this.mUpdateManager.setOnEngineCompleteCallback(this::onPayloadApplicationComplete);
    }

    @Override
    protected void onDestroy() {
        this.mUpdateManager.setOnEngineStatusUpdateCallback(null);
        this.mUpdateManager.setOnProgressUpdateCallback(null);
        this.mUpdateManager.setOnEngineCompleteCallback(null);
        super.onDestroy();
    }

    @Override
    protected void onResume() {
        super.onResume();
        this.mUpdateManager.bind();
    }

    @Override
    protected void onPause() {
        this.mUpdateManager.unbind();
        super.onPause();
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
                    mUpdateManager.applyUpdate(this, getSelectedConfig());
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
                    mUpdateManager.cancelRunningUpdate();
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
                    mUpdateManager.resetUpdate();
                })
                .setNegativeButton(android.R.string.cancel, null).show();
    }

    /**
     * switch slot button clicked
     */
    public void onSwitchSlotClick(View view) {
        mUpdateManager.setSwitchSlotOnReboot();
    }

    /**
     * Invoked when anything changes. The value of {@code status} will
     * be one of the values from {@link UpdateEngine.UpdateStatusConstants},
     * and {@code percent} will be from {@code 0.0} to {@code 1.0}.
     */
    private void onStatusUpdate(int status) {
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

    private void onProgressUpdate(double progress) {
        mProgressBar.setProgress((int) (100 * progress));
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
                if (mUpdateManager.manualSwitchSlotRequired()) {
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

}
