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

import android.os.Parcel;
import android.os.Parcelable;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.Serializable;
import java.util.Optional;

/**
 * An update description. It will be parsed from JSON, which is intended to
 * be sent from server to the update app, but in this sample app it will be stored on the device.
 */
public class UpdateConfig implements Parcelable {

    public static final int AB_INSTALL_TYPE_NON_STREAMING = 0;
    public static final int AB_INSTALL_TYPE_STREAMING = 1;

    public static final Parcelable.Creator<UpdateConfig> CREATOR =
            new Parcelable.Creator<UpdateConfig>() {
                @Override
                public UpdateConfig createFromParcel(Parcel source) {
                    return new UpdateConfig(source);
                }

                @Override
                public UpdateConfig[] newArray(int size) {
                    return new UpdateConfig[size];
                }
            };

    /** parse update config from json */
    public static UpdateConfig fromJson(String json) throws JSONException {
        UpdateConfig c = new UpdateConfig();

        JSONObject o = new JSONObject(json);
        c.mName = o.getString("name");
        c.mUrl = o.getString("url");
        switch (o.getString("ab_install_type")) {
            case AB_INSTALL_TYPE_NON_STREAMING_JSON:
                c.mAbInstallType = AB_INSTALL_TYPE_NON_STREAMING;
                break;
            case AB_INSTALL_TYPE_STREAMING_JSON:
                c.mAbInstallType = AB_INSTALL_TYPE_STREAMING;
                break;
            default:
                throw new JSONException("Invalid type, expected either "
                        + "NON_STREAMING or STREAMING, got " + o.getString("ab_install_type"));
        }
        if (c.mAbInstallType == AB_INSTALL_TYPE_STREAMING) {
            JSONObject meta = o.getJSONObject("ab_streaming_metadata");
            JSONArray propertyFilesJson = meta.getJSONArray("property_files");
            PackageFile[] propertyFiles =
                new PackageFile[propertyFilesJson.length()];
            for (int i = 0; i < propertyFilesJson.length(); i++) {
                JSONObject p = propertyFilesJson.getJSONObject(i);
                propertyFiles[i] = new PackageFile(
                        p.getString("filename"),
                        p.getLong("offset"),
                        p.getLong("size"));
            }
            String authorization = null;
            if (meta.has("authorization")) {
                authorization = meta.getString("authorization");
            }
            c.mAbStreamingMetadata = new StreamingMetadata(
                    propertyFiles,
                    authorization);
        }
        c.mRawJson = json;
        return c;
    }

    /**
     * these strings are represent types in JSON config files
     */
    private static final String AB_INSTALL_TYPE_NON_STREAMING_JSON = "NON_STREAMING";
    private static final String AB_INSTALL_TYPE_STREAMING_JSON = "STREAMING";

    /** name will be visible on UI */
    private String mName;

    /** update zip file URI, can be https:// or file:// */
    private String mUrl;

    /** non-streaming (first saves locally) OR streaming (on the fly) */
    private int mAbInstallType;

    /** metadata is required only for streaming update */
    private StreamingMetadata mAbStreamingMetadata;

    private String mRawJson;

    protected UpdateConfig() {
    }

    protected UpdateConfig(Parcel in) {
        this.mName = in.readString();
        this.mUrl = in.readString();
        this.mAbInstallType = in.readInt();
        this.mAbStreamingMetadata = (StreamingMetadata) in.readSerializable();
        this.mRawJson = in.readString();
    }

    public UpdateConfig(String name, String url, int installType) {
        this.mName = name;
        this.mUrl = url;
        this.mAbInstallType = installType;
    }

    public String getName() {
        return mName;
    }

    public String getUrl() {
        return mUrl;
    }

    public String getRawJson() {
        return mRawJson;
    }

    public int getInstallType() {
        return mAbInstallType;
    }

    public StreamingMetadata getStreamingMetadata() {
        return mAbStreamingMetadata;
    }

    /**
     * @return File object for given url
     */
    public File getUpdatePackageFile() {
        if (mAbInstallType != AB_INSTALL_TYPE_NON_STREAMING) {
            throw new RuntimeException("Expected non-streaming install type");
        }
        if (!mUrl.startsWith("file://")) {
            throw new RuntimeException("url is expected to start with file://");
        }
        return new File(mUrl.substring(7, mUrl.length()));
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeString(mName);
        dest.writeString(mUrl);
        dest.writeInt(mAbInstallType);
        dest.writeSerializable(mAbStreamingMetadata);
        dest.writeString(mRawJson);
    }

    /**
     * Metadata for streaming A/B update.
     */
    public static class StreamingMetadata implements Serializable {

        private static final long serialVersionUID = 31042L;

        /** defines beginning of update data in archive */
        private PackageFile[] mPropertyFiles;

        /** SystemUpdaterSample receives the authorization token from the OTA server, in addition
         * to the package URL. It passes on the info to update_engine, so that the latter can
         * fetch the data from the package server directly with the token. */
        private String mAuthorization;

        public StreamingMetadata(PackageFile[] propertyFiles, String authorization) {
            this.mPropertyFiles = propertyFiles;
            this.mAuthorization = authorization;
        }

        public PackageFile[] getPropertyFiles() {
            return mPropertyFiles;
        }

        public Optional<String> getAuthorization() {
            return mAuthorization == null ? Optional.empty() : Optional.of(mAuthorization);
        }
    }

    /**
     * Description of a file in an OTA package zip file.
     */
    public static class PackageFile implements Serializable {

        private static final long serialVersionUID = 31043L;

        /** filename in an archive */
        private String mFilename;

        /** defines beginning of update data in archive */
        private long mOffset;

        /** size of the update data in archive */
        private long mSize;

        public PackageFile(String filename, long offset, long size) {
            this.mFilename = filename;
            this.mOffset = offset;
            this.mSize = size;
        }

        public String getFilename() {
            return mFilename;
        }

        public long getOffset() {
            return mOffset;
        }

        public long getSize() {
            return mSize;
        }
    }

}
