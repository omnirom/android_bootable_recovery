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

package com.example.android.systemupdatersample.services;

import static com.example.android.systemupdatersample.util.PackageFiles.OTA_PACKAGE_DIR;
import static com.example.android.systemupdatersample.util.PackageFiles.PAYLOAD_BINARY_FILE_NAME;
import static com.example.android.systemupdatersample.util.PackageFiles.PAYLOAD_PROPERTIES_FILE_NAME;

import android.app.IntentService;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.ResultReceiver;
import android.util.Log;

import com.example.android.systemupdatersample.PayloadSpec;
import com.example.android.systemupdatersample.UpdateConfig;
import com.example.android.systemupdatersample.util.FileDownloader;
import com.example.android.systemupdatersample.util.PackageFiles;
import com.example.android.systemupdatersample.util.PayloadSpecs;
import com.example.android.systemupdatersample.util.UpdateConfigs;
import com.google.common.collect.ImmutableSet;

import java.io.IOException;
import java.nio.file.Paths;
import java.util.Optional;

/**
 * This IntentService will download/extract the necessary files from the package zip
 * without downloading the whole package. And it constructs {@link PayloadSpec}.
 * All this work required to install streaming A/B updates.
 *
 * PrepareStreamingService runs on it's own thread. It will notify activity
 * using interface {@link UpdateResultCallback} when update is ready to install.
 */
public class PrepareStreamingService extends IntentService {

    /**
     * UpdateResultCallback result codes.
     */
    public static final int RESULT_CODE_SUCCESS = 0;
    public static final int RESULT_CODE_ERROR = 1;

    /**
     * This interface is used to send results from {@link PrepareStreamingService} to
     * {@code MainActivity}.
     */
    public interface UpdateResultCallback {

        /**
         * Invoked when files are downloaded and payload spec is constructed.
         *
         * @param resultCode result code, values are defined in {@link PrepareStreamingService}
         * @param payloadSpec prepared payload spec for streaming update
         */
        void onReceiveResult(int resultCode, PayloadSpec payloadSpec);
    }

    /**
     * Starts PrepareStreamingService.
     *
     * @param context application context
     * @param config update config
     * @param resultCallback callback that will be called when the update is ready to be installed
     */
    public static void startService(Context context,
            UpdateConfig config,
            UpdateResultCallback resultCallback) {
        Log.d(TAG, "Starting PrepareStreamingService");
        ResultReceiver receiver = new CallbackResultReceiver(new Handler(), resultCallback);
        Intent intent = new Intent(context, PrepareStreamingService.class);
        intent.putExtra(EXTRA_PARAM_CONFIG, config);
        intent.putExtra(EXTRA_PARAM_RESULT_RECEIVER, receiver);
        context.startService(intent);
    }

    public PrepareStreamingService() {
        super(TAG);
    }

    private static final String TAG = "PrepareStreamingService";

    /**
     * Extra params that will be sent from Activity to IntentService.
     */
    private static final String EXTRA_PARAM_CONFIG = "config";
    private static final String EXTRA_PARAM_RESULT_RECEIVER = "result-receiver";

    /**
     * The files that should be downloaded before streaming.
     */
    private static final ImmutableSet<String> PRE_STREAMING_FILES_SET =
            ImmutableSet.of(
                PackageFiles.CARE_MAP_FILE_NAME,
                PackageFiles.COMPATIBILITY_ZIP_FILE_NAME,
                PackageFiles.METADATA_FILE_NAME,
                PackageFiles.PAYLOAD_PROPERTIES_FILE_NAME
            );

    @Override
    protected void onHandleIntent(Intent intent) {
        Log.d(TAG, "On handle intent is called");
        UpdateConfig config = intent.getParcelableExtra(EXTRA_PARAM_CONFIG);
        ResultReceiver resultReceiver = intent.getParcelableExtra(EXTRA_PARAM_RESULT_RECEIVER);

        try {
            downloadPreStreamingFiles(config, OTA_PACKAGE_DIR);
        } catch (IOException e) {
            Log.e(TAG, "Failed to download pre-streaming files", e);
            resultReceiver.send(RESULT_CODE_ERROR, null);
            return;
        }

        Optional<UpdateConfig.PackageFile> payloadBinary =
                UpdateConfigs.getPropertyFile(PAYLOAD_BINARY_FILE_NAME, config);

        if (!payloadBinary.isPresent()) {
            Log.e(TAG, "Failed to find " + PAYLOAD_BINARY_FILE_NAME + " in config");
            resultReceiver.send(RESULT_CODE_ERROR, null);
            return;
        }

        Optional<UpdateConfig.PackageFile> properties =
                UpdateConfigs.getPropertyFile(PAYLOAD_PROPERTIES_FILE_NAME, config);

        if (!properties.isPresent()) {
            Log.e(TAG, "Failed to find " + PAYLOAD_PROPERTIES_FILE_NAME + " in config");
            resultReceiver.send(RESULT_CODE_ERROR, null);
            return;
        }

        PayloadSpec spec;
        try {
            spec = PayloadSpecs.forStreaming(config.getUrl(),
                    payloadBinary.get().getOffset(),
                    payloadBinary.get().getSize(),
                    Paths.get(OTA_PACKAGE_DIR, properties.get().getFilename()).toFile()
            );
        } catch (IOException e) {
            Log.e(TAG, "PayloadSpecs failed to create PayloadSpec for streaming", e);
            resultReceiver.send(RESULT_CODE_ERROR, null);
            return;
        }

        resultReceiver.send(RESULT_CODE_SUCCESS, CallbackResultReceiver.createBundle(spec));
    }

    /**
     * Downloads files defined in {@link UpdateConfig#getStreamingMetadata()}
     * and exists in {@code PRE_STREAMING_FILES_SET}, and put them
     * in directory {@code dir}.
     * @throws IOException when can't download a file
     */
    private static void downloadPreStreamingFiles(UpdateConfig config, String dir)
            throws IOException {
        Log.d(TAG, "Downloading files to " + dir);
        for (UpdateConfig.PackageFile file : config.getStreamingMetadata().getPropertyFiles()) {
            if (PRE_STREAMING_FILES_SET.contains(file.getFilename())) {
                Log.d(TAG, "Downloading file " + file.getFilename());
                FileDownloader downloader = new FileDownloader(
                        config.getUrl(),
                        file.getOffset(),
                        file.getSize(),
                        Paths.get(dir, file.getFilename()).toFile());
                downloader.download();
            }
        }
    }

    /**
     * Used by {@link PrepareStreamingService} to pass {@link PayloadSpec}
     * to {@link UpdateResultCallback#onReceiveResult}.
     */
    private static class CallbackResultReceiver extends ResultReceiver {

        static Bundle createBundle(PayloadSpec payloadSpec) {
            Bundle b = new Bundle();
            b.putSerializable(BUNDLE_PARAM_PAYLOAD_SPEC, payloadSpec);
            return b;
        }

        private static final String BUNDLE_PARAM_PAYLOAD_SPEC = "payload-spec";

        private UpdateResultCallback mUpdateResultCallback;

        CallbackResultReceiver(Handler handler, UpdateResultCallback updateResultCallback) {
            super(handler);
            this.mUpdateResultCallback = updateResultCallback;
        }

        @Override
        protected void onReceiveResult(int resultCode, Bundle resultData) {
            PayloadSpec payloadSpec = null;
            if (resultCode == RESULT_CODE_SUCCESS) {
                payloadSpec = (PayloadSpec) resultData.getSerializable(BUNDLE_PARAM_PAYLOAD_SPEC);
            }
            mUpdateResultCallback.onReceiveResult(resultCode, payloadSpec);
        }
    }

}
