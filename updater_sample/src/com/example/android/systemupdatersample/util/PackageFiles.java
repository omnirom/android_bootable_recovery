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

/** Utility class for an OTA package. */
public final class PackageFiles {

    /**
     * Directory used to perform updates.
     */
    public static final String OTA_PACKAGE_DIR = "/data/ota_package";

    /**
     * update payload, it will be passed to {@code UpdateEngine#applyPayload}.
     */
    public static final String PAYLOAD_BINARY_FILE_NAME = "payload.bin";

    /**
     * Currently, when calling {@code UpdateEngine#applyPayload} to perform actions
     * that don't require network access (e.g. change slot), update_engine still
     * talks to the server to download/verify file.
     * {@code update_engine} might throw error when rebooting if {@code UpdateEngine#applyPayload}
     * is not supplied right headers and tokens.
     * This behavior might change in future android versions.
     *
     * To avoid extra network request in {@code update_engine}, this file has to be
     * downloaded and put in {@code OTA_PACKAGE_DIR}.
     */
    public static final String PAYLOAD_METADATA_FILE_NAME = "payload_metadata.bin";

    public static final String PAYLOAD_PROPERTIES_FILE_NAME = "payload_properties.txt";

    /** The zip entry in an A/B OTA package, which will be used by update_verifier. */
    public static final String CARE_MAP_FILE_NAME = "care_map.txt";

    public static final String METADATA_FILE_NAME = "metadata";

    /**
     * The zip file that claims the compatibility of the update package to check against the Android
     * framework to ensure that the package can be installed on the device.
     */
    public static final String COMPATIBILITY_ZIP_FILE_NAME = "compatibility.zip";

    private PackageFiles() {}
}
