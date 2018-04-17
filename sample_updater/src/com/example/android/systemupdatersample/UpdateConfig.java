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

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.Serializable;

/**
 * UpdateConfig describes an update. It will be parsed from JSON, which is intended to
 * be sent from server to the update app, but in this sample app it will be stored on the device.
 */
public class UpdateConfig implements Parcelable {

    public static final int TYPE_NON_STREAMING = 0;
    public static final int TYPE_STREAMING     = 1;

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
        if (TYPE_NON_STREAMING_JSON.equals(o.getString("type"))) {
            c.mInstallType = TYPE_NON_STREAMING;
        } else if (TYPE_STREAMING_JSON.equals(o.getString("type"))) {
            c.mInstallType = TYPE_STREAMING;
        } else {
            throw new JSONException("Invalid type, expected either "
                    + "NON_STREAMING or STREAMING, got " + o.getString("type"));
        }
        if (o.has("metadata")) {
            c.mMetadata = new Metadata(
                    o.getJSONObject("metadata").getInt("offset"),
                    o.getJSONObject("metadata").getInt("size"));
        }
        c.mRawJson = json;
        return c;
    }

    /**
     * these strings are represent types in JSON config files
     */
    private static final String TYPE_NON_STREAMING_JSON  = "NON_STREAMING";
    private static final String TYPE_STREAMING_JSON      = "STREAMING";

    /** name will be visible on UI */
    private String mName;

    /** update zip file URI, can be https:// or file:// */
    private String mUrl;

    /** non-streaming (first saves locally) OR streaming (on the fly) */
    private int mInstallType;

    /** metadata is required only for streaming update */
    private Metadata mMetadata;

    private String mRawJson;

    protected UpdateConfig() {
    }

    protected UpdateConfig(Parcel in) {
        this.mName = in.readString();
        this.mUrl = in.readString();
        this.mInstallType = in.readInt();
        this.mMetadata = (Metadata) in.readSerializable();
        this.mRawJson = in.readString();
    }

    public UpdateConfig(String name, String url, int installType) {
        this.mName = name;
        this.mUrl = url;
        this.mInstallType = installType;
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
        return mInstallType;
    }

    /**
     * "url" must be the file located on the device.
     *
     * @return File object for given url
     */
    public File getUpdatePackageFile() {
        if (mInstallType != TYPE_NON_STREAMING) {
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
        dest.writeInt(mInstallType);
        dest.writeSerializable(mMetadata);
        dest.writeString(mRawJson);
    }

    /**
     * Metadata for STREAMING update
     */
    public static class Metadata implements Serializable {

        private static final long serialVersionUID = 31042L;

        /** defines beginning of update data in archive */
        private long mOffset;

        /** size of the update data in archive */
        private long mSize;

        public Metadata(long offset, long size) {
            this.mOffset = offset;
            this.mSize = size;
        }

        public long getOffset() {
            return mOffset;
        }

        public long getSize() {
            return mSize;
        }
    }

}
