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

import android.os.UpdateEngine;

import java.io.Serializable;
import java.util.List;

/**
 * Payload that will be given to {@link UpdateEngine#applyPayload)}.
 */
public class PayloadSpec implements Serializable {

    private static final long serialVersionUID = 41043L;

    /**
     * Creates a payload spec {@link Builder}
     */
    public static Builder newBuilder() {
        return new Builder();
    }

    private String mUrl;
    private long mOffset;
    private long mSize;
    private List<String> mProperties;

    public PayloadSpec(Builder b) {
        this.mUrl = b.mUrl;
        this.mOffset = b.mOffset;
        this.mSize = b.mSize;
        this.mProperties = b.mProperties;
    }

    public String getUrl() {
        return mUrl;
    }

    public long getOffset() {
        return mOffset;
    }

    public long getSize() {
        return mSize;
    }

    public List<String> getProperties() {
        return mProperties;
    }

    /**
     * payload spec builder.
     *
     * <p>Usage:</p>
     *
     * {@code
     *   PayloadSpec spec = PayloadSpec.newBuilder()
     *     .url("url")
     *     .build();
     * }
     */
    public static class Builder {
        private String mUrl;
        private long mOffset;
        private long mSize;
        private List<String> mProperties;

        public Builder() {
        }

        /**
         * set url
         */
        public Builder url(String url) {
            this.mUrl = url;
            return this;
        }

        /**
         * set offset
         */
        public Builder offset(long offset) {
            this.mOffset = offset;
            return this;
        }

        /**
         * set size
         */
        public Builder size(long size) {
            this.mSize = size;
            return this;
        }

        /**
         * set properties
         */
        public Builder properties(List<String> properties) {
            this.mProperties = properties;
            return this;
        }

        /**
         * build {@link PayloadSpec}
         */
        public PayloadSpec build() {
            return new PayloadSpec(this);
        }
    }
}
