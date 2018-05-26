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

package com.example.android.systemupdatersample;

import android.content.Context;
import android.os.UpdateEngine;
import android.os.UpdateEngineCallback;
import android.util.Log;

import com.example.android.systemupdatersample.services.PrepareStreamingService;
import com.example.android.systemupdatersample.util.PayloadSpecs;
import com.example.android.systemupdatersample.util.UpdateEngineErrorCodes;
import com.example.android.systemupdatersample.util.UpdateEngineProperties;
import com.example.android.systemupdatersample.util.UpdaterStates;
import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableList;
import com.google.common.util.concurrent.AtomicDouble;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.DoubleConsumer;
import java.util.function.IntConsumer;

/**
 * Manages the update flow. It has its own state (in memory), separate from
 * {@link UpdateEngine}'s state. Asynchronously interacts with the {@link UpdateEngine}.
 */
public class UpdateManager {

    private static final String TAG = "UpdateManager";

    /** HTTP Header: User-Agent; it will be sent to the server when streaming the payload. */
    private static final String HTTP_USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
            + "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/60.0.3112.113 Safari/537.36";

    private final UpdateEngine mUpdateEngine;
    private final PayloadSpecs mPayloadSpecs;

    private AtomicInteger mUpdateEngineStatus =
            new AtomicInteger(UpdateEngine.UpdateStatusConstants.IDLE);
    private AtomicInteger mEngineErrorCode = new AtomicInteger(UpdateEngineErrorCodes.UNKNOWN);
    private AtomicDouble mProgress = new AtomicDouble(0);
    private AtomicInteger mState = new AtomicInteger(UpdaterStates.IDLE);

    private AtomicBoolean mManualSwitchSlotRequired = new AtomicBoolean(true);

    private UpdateData mLastUpdateData = null;

    private IntConsumer mOnStateChangeCallback = null;
    private IntConsumer mOnEngineStatusUpdateCallback = null;
    private DoubleConsumer mOnProgressUpdateCallback = null;
    private IntConsumer mOnEngineCompleteCallback = null;

    private final Object mLock = new Object();

    private final UpdateManager.UpdateEngineCallbackImpl
            mUpdateEngineCallback = new UpdateManager.UpdateEngineCallbackImpl();

    public UpdateManager(UpdateEngine updateEngine, PayloadSpecs payloadSpecs) {
        this.mUpdateEngine = updateEngine;
        this.mPayloadSpecs = payloadSpecs;
    }

    /**
     * Binds to {@link UpdateEngine}.
     */
    public void bind() {
        this.mUpdateEngine.bind(mUpdateEngineCallback);
    }

    /**
     * Unbinds from {@link UpdateEngine}.
     */
    public void unbind() {
        this.mUpdateEngine.unbind();
    }

    /**
     * @return a number from {@code 0.0} to {@code 1.0}.
     */
    public float getProgress() {
        return (float) this.mProgress.get();
    }

    /**
     * Returns true if manual switching slot is required. Value depends on
     * the update config {@code ab_config.force_switch_slot}.
     */
    public boolean isManualSwitchSlotRequired() {
        return mManualSwitchSlotRequired.get();
    }

    /**
     * Sets SystemUpdaterSample app state change callback. Value of {@code state} will be one
     * of the values from {@link UpdaterStates}.
     *
     * @param onStateChangeCallback a callback with parameter {@code state}.
     */
    public void setOnStateChangeCallback(IntConsumer onStateChangeCallback) {
        synchronized (mLock) {
            this.mOnStateChangeCallback = onStateChangeCallback;
        }
    }

    private Optional<IntConsumer> getOnStateChangeCallback() {
        synchronized (mLock) {
            return mOnStateChangeCallback == null
                    ? Optional.empty()
                    : Optional.of(mOnStateChangeCallback);
        }
    }

    /**
     * Sets update engine status update callback. Value of {@code status} will
     * be one of the values from {@link UpdateEngine.UpdateStatusConstants}.
     *
     * @param onStatusUpdateCallback a callback with parameter {@code status}.
     */
    public void setOnEngineStatusUpdateCallback(IntConsumer onStatusUpdateCallback) {
        synchronized (mLock) {
            this.mOnEngineStatusUpdateCallback = onStatusUpdateCallback;
        }
    }

    private Optional<IntConsumer> getOnEngineStatusUpdateCallback() {
        synchronized (mLock) {
            return mOnEngineStatusUpdateCallback == null
                    ? Optional.empty()
                    : Optional.of(mOnEngineStatusUpdateCallback);
        }
    }

    /**
     * Sets update engine payload application complete callback. Value of {@code errorCode} will
     * be one of the values from {@link UpdateEngine.ErrorCodeConstants}.
     *
     * @param onComplete a callback with parameter {@code errorCode}.
     */
    public void setOnEngineCompleteCallback(IntConsumer onComplete) {
        synchronized (mLock) {
            this.mOnEngineCompleteCallback = onComplete;
        }
    }

    private Optional<IntConsumer> getOnEngineCompleteCallback() {
        synchronized (mLock) {
            return mOnEngineCompleteCallback == null
                    ? Optional.empty()
                    : Optional.of(mOnEngineCompleteCallback);
        }
    }

    /**
     * Sets progress update callback. Progress is a number from {@code 0.0} to {@code 1.0}.
     *
     * @param onProgressCallback a callback with parameter {@code progress}.
     */
    public void setOnProgressUpdateCallback(DoubleConsumer onProgressCallback) {
        synchronized (mLock) {
            this.mOnProgressUpdateCallback = onProgressCallback;
        }
    }

    private Optional<DoubleConsumer> getOnProgressUpdateCallback() {
        synchronized (mLock) {
            return mOnProgressUpdateCallback == null
                    ? Optional.empty()
                    : Optional.of(mOnProgressUpdateCallback);
        }
    }

    /**
     * Updates {@link this.mState} and if state is changed,
     * it also notifies {@link this.mOnStateChangeCallback}.
     */
    private void setUpdaterState(int updaterState) {
        int previousState = mState.get();
        mState.set(updaterState);
        if (previousState != updaterState) {
            getOnStateChangeCallback().ifPresent(callback -> callback.accept(updaterState));
        }
    }

    /**
     * Requests update engine to stop any ongoing update. If an update has been applied,
     * leave it as is.
     *
     * <p>Sometimes it's possible that the
     * update engine would throw an error when the method is called, and the only way to
     * handle it is to catch the exception.</p>
     */
    public void cancelRunningUpdate() {
        try {
            mUpdateEngine.cancel();
            setUpdaterState(UpdaterStates.IDLE);
        } catch (Exception e) {
            Log.w(TAG, "UpdateEngine failed to stop the ongoing update", e);
        }
    }

    /**
     * Resets update engine to IDLE state. If an update has been applied it reverts it.
     *
     * <p>Sometimes it's possible that the
     * update engine would throw an error when the method is called, and the only way to
     * handle it is to catch the exception.</p>
     */
    public void resetUpdate() {
        try {
            mUpdateEngine.resetStatus();
            setUpdaterState(UpdaterStates.IDLE);
        } catch (Exception e) {
            Log.w(TAG, "UpdateEngine failed to reset the update", e);
        }
    }

    /**
     * Applies the given update.
     *
     * <p>UpdateEngine works asynchronously. This method doesn't wait until
     * end of the update.</p>
     */
    public void applyUpdate(Context context, UpdateConfig config) {
        mEngineErrorCode.set(UpdateEngineErrorCodes.UNKNOWN);
        setUpdaterState(UpdaterStates.RUNNING);

        synchronized (mLock) {
            // Cleaning up previous update data.
            mLastUpdateData = null;
        }

        if (!config.getAbConfig().getForceSwitchSlot()) {
            mManualSwitchSlotRequired.set(true);
        } else {
            mManualSwitchSlotRequired.set(false);
        }

        if (config.getInstallType() == UpdateConfig.AB_INSTALL_TYPE_NON_STREAMING) {
            applyAbNonStreamingUpdate(config);
        } else {
            applyAbStreamingUpdate(context, config);
        }
    }

    private void applyAbNonStreamingUpdate(UpdateConfig config) {
        UpdateData.Builder builder = UpdateData.builder()
                .setExtraProperties(prepareExtraProperties(config));

        try {
            builder.setPayload(mPayloadSpecs.forNonStreaming(config.getUpdatePackageFile()));
        } catch (IOException e) {
            Log.e(TAG, "Error creating payload spec", e);
            setUpdaterState(UpdaterStates.ERROR);
            return;
        }
        updateEngineApplyPayload(builder.build());
    }

    private void applyAbStreamingUpdate(Context context, UpdateConfig config) {
        UpdateData.Builder builder = UpdateData.builder()
                .setExtraProperties(prepareExtraProperties(config));

        Log.d(TAG, "Starting PrepareStreamingService");
        PrepareStreamingService.startService(context, config, (code, payloadSpec) -> {
            if (code == PrepareStreamingService.RESULT_CODE_SUCCESS) {
                builder.setPayload(payloadSpec);
                builder.addExtraProperty("USER_AGENT=" + HTTP_USER_AGENT);
                config.getStreamingMetadata()
                        .getAuthorization()
                        .ifPresent(s -> builder.addExtraProperty("AUTHORIZATION=" + s));
                updateEngineApplyPayload(builder.build());
            } else {
                Log.e(TAG, "PrepareStreamingService failed, result code is " + code);
                setUpdaterState(UpdaterStates.ERROR);
            }
        });
    }

    private List<String> prepareExtraProperties(UpdateConfig config) {
        List<String> extraProperties = new ArrayList<>();

        if (!config.getAbConfig().getForceSwitchSlot()) {
            // Disable switch slot on reboot, which is enabled by default.
            // User will enable it manually by clicking "Switch Slot" button on the screen.
            extraProperties.add(UpdateEngineProperties.PROPERTY_DISABLE_SWITCH_SLOT_ON_REBOOT);
        }
        return extraProperties;
    }

    /**
     * Applies given payload.
     *
     * <p>UpdateEngine works asynchronously. This method doesn't wait until
     * end of the update.</p>
     *
     * <p>It's possible that the update engine throws a generic error, such as upon seeing invalid
     * payload properties (which come from OTA packages), or failing to set up the network
     * with the given id.</p>
     */
    private void updateEngineApplyPayload(UpdateData update) {
        synchronized (mLock) {
            mLastUpdateData = update;
        }

        ArrayList<String> properties = new ArrayList<>(update.getPayload().getProperties());
        properties.addAll(update.getExtraProperties());

        try {
            mUpdateEngine.applyPayload(
                    update.getPayload().getUrl(),
                    update.getPayload().getOffset(),
                    update.getPayload().getSize(),
                    properties.toArray(new String[0]));
        } catch (Exception e) {
            Log.e(TAG, "UpdateEngine failed to apply the update", e);
            setUpdaterState(UpdaterStates.ERROR);
        }
    }

    private void updateEngineReApplyPayload() {
        UpdateData lastUpdate;
        synchronized (mLock) {
            // mLastPayloadSpec might be empty in some cases.
            // But to make this sample app simple, we will not handle it.
            Preconditions.checkArgument(
                    mLastUpdateData != null,
                    "mLastUpdateData must be present.");
            lastUpdate = mLastUpdateData;
        }
        updateEngineApplyPayload(lastUpdate);
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
    public void setSwitchSlotOnReboot() {
        Log.d(TAG, "setSwitchSlotOnReboot invoked");
        UpdateData.Builder builder;
        synchronized (mLock) {
            // To make sample app simple, we don't handle it.
            Preconditions.checkArgument(
                    mLastUpdateData != null,
                    "mLastUpdateData must be present.");
            builder = mLastUpdateData.toBuilder();
        }
        // PROPERTY_SKIP_POST_INSTALL should be passed on to skip post-installation hooks.
        builder.setExtraProperties(
                Collections.singletonList(UpdateEngineProperties.PROPERTY_SKIP_POST_INSTALL));
        // UpdateEngine sets property SWITCH_SLOT_ON_REBOOT=1 by default.
        // HTTP headers are not required, UpdateEngine is not expected to stream payload.
        updateEngineApplyPayload(builder.build());
    }

    private void onStatusUpdate(int status, float progress) {
        int previousStatus = mUpdateEngineStatus.get();
        mUpdateEngineStatus.set(status);
        mProgress.set(progress);

        getOnProgressUpdateCallback().ifPresent(callback -> callback.accept(progress));

        if (previousStatus != status) {
            getOnEngineStatusUpdateCallback().ifPresent(callback -> callback.accept(status));
        }
    }

    private void onPayloadApplicationComplete(int errorCode) {
        Log.d(TAG, "onPayloadApplicationComplete invoked, errorCode=" + errorCode);
        mEngineErrorCode.set(errorCode);
        if (errorCode == UpdateEngine.ErrorCodeConstants.SUCCESS
                || errorCode == UpdateEngineErrorCodes.UPDATED_BUT_NOT_ACTIVE) {
            setUpdaterState(UpdaterStates.FINISHED);
        } else if (errorCode != UpdateEngineErrorCodes.USER_CANCELLED) {
            setUpdaterState(UpdaterStates.ERROR);
        }

        getOnEngineCompleteCallback()
                .ifPresent(callback -> callback.accept(errorCode));
    }

    /**
     * Helper class to delegate {@code update_engine} callbacks to UpdateManager
     */
    class UpdateEngineCallbackImpl extends UpdateEngineCallback {
        @Override
        public void onStatusUpdate(int status, float percent) {
            UpdateManager.this.onStatusUpdate(status, percent);
        }

        @Override
        public void onPayloadApplicationComplete(int errorCode) {
            UpdateManager.this.onPayloadApplicationComplete(errorCode);
        }
    }

    /**
     *
     * Contains update data - PayloadSpec and extra properties list.
     *
     * <p>{@code mPayload} contains url, offset and size to {@code PAYLOAD_BINARY_FILE_NAME}.
     * {@code mExtraProperties} is a list of additional properties to pass to
     * {@link UpdateEngine#applyPayload}.</p>
     */
    private static class UpdateData {
        private final PayloadSpec mPayload;
        private final ImmutableList<String> mExtraProperties;

        public static Builder builder() {
            return new Builder();
        }

        UpdateData(Builder builder) {
            this.mPayload = builder.mPayload;
            this.mExtraProperties = ImmutableList.copyOf(builder.mExtraProperties);
        }

        public PayloadSpec getPayload() {
            return mPayload;
        }

        public ImmutableList<String> getExtraProperties() {
            return mExtraProperties;
        }

        public Builder toBuilder() {
            return builder()
                    .setPayload(mPayload)
                    .setExtraProperties(mExtraProperties);
        }

        static class Builder {
            private PayloadSpec mPayload;
            private List<String> mExtraProperties;

            public Builder setPayload(PayloadSpec payload) {
                this.mPayload = payload;
                return this;
            }

            public Builder setExtraProperties(List<String> extraProperties) {
                this.mExtraProperties = new ArrayList<>(extraProperties);
                return this;
            }

            public Builder addExtraProperty(String property) {
                if (this.mExtraProperties == null) {
                    this.mExtraProperties = new ArrayList<>();
                }
                this.mExtraProperties.add(property);
                return this;
            }

            public UpdateData build() {
                return new UpdateData(this);
            }
        }
    }

}
