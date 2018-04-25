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

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertSame;

import android.support.test.filters.SmallTest;
import android.support.test.runner.AndroidJUnit4;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.junit.runner.RunWith;

/**
 * Tests for {@link UpdateConfig}
 */
@RunWith(AndroidJUnit4.class)
@SmallTest
public class UpdateConfigTest {

    private static final String JSON_NON_STREAMING =
            "{\"name\": \"vip update\", \"url\": \"file:///builds/a.zip\", "
            + " \"type\": \"NON_STREAMING\"}";

    private static final String JSON_STREAMING =
            "{\"name\": \"vip update 2\", \"url\": \"http://foo.bar/a.zip\", "
            + "\"type\": \"STREAMING\"}";

    @Rule
    public final ExpectedException thrown = ExpectedException.none();

    @Test
    public void fromJson_parsesJsonConfigWithoutMetadata() throws Exception {
        UpdateConfig config = UpdateConfig.fromJson(JSON_NON_STREAMING);
        assertEquals("name is parsed", "vip update", config.getName());
        assertEquals("stores raw json", JSON_NON_STREAMING, config.getRawJson());
        assertSame("type is parsed", UpdateConfig.TYPE_NON_STREAMING, config.getInstallType());
        assertEquals("url is parsed", "file:///builds/a.zip", config.getUrl());
    }

    @Test
    public void getUpdatePackageFile_throwsErrorIfStreaming() throws Exception {
        UpdateConfig config = UpdateConfig.fromJson(JSON_STREAMING);
        thrown.expect(RuntimeException.class);
        config.getUpdatePackageFile();
    }

    @Test
    public void getUpdatePackageFile_throwsErrorIfNotAFile() throws Exception {
        String json = "{\"name\": \"upd\", \"url\": \"http://foo.bar\","
                + " \"type\": \"NON_STREAMING\"}";
        UpdateConfig config = UpdateConfig.fromJson(json);
        thrown.expect(RuntimeException.class);
        config.getUpdatePackageFile();
    }

    @Test
    public void getUpdatePackageFile_works() throws Exception {
        UpdateConfig c = UpdateConfig.fromJson(JSON_NON_STREAMING);
        assertEquals("correct path", "/builds/a.zip", c.getUpdatePackageFile().getAbsolutePath());
    }

}
