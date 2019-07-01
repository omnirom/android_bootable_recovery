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

import static com.example.android.systemupdatersample.util.PackageFiles.PAYLOAD_BINARY_FILE_NAME;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;

import android.content.Context;

import androidx.test.InstrumentationRegistry;
import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import com.example.android.systemupdatersample.PayloadSpec;
import com.example.android.systemupdatersample.tests.R;
import com.google.common.base.Charsets;
import com.google.common.io.Files;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.junit.runner.RunWith;

import java.io.File;
import java.io.IOException;
import java.nio.file.Paths;

/**
 * Tests if PayloadSpecs parses update package zip file correctly.
 */
@RunWith(AndroidJUnit4.class)
@SmallTest
public class PayloadSpecsTest {

    private static final String PROPERTIES_CONTENTS = "k1=val1\nkey2=val2";

    private File mTestDir;

    private Context mTargetContext;
    private Context mTestContext;

    private PayloadSpecs mPayloadSpecs;

    @Rule
    public final ExpectedException thrown = ExpectedException.none();

    @Before
    public void setUp() {
        mTargetContext = InstrumentationRegistry.getTargetContext();
        mTestContext = InstrumentationRegistry.getContext();

        mTestDir = mTargetContext.getCacheDir();
        mPayloadSpecs = new PayloadSpecs();
    }

    @Test
    public void forNonStreaming_works() throws Exception {
        // Prepare the target file
        File packageFile = Paths
                .get(mTargetContext.getCacheDir().getAbsolutePath(), "ota.zip")
                .toFile();
        java.nio.file.Files.deleteIfExists(packageFile.toPath());
        java.nio.file.Files.copy(mTestContext.getResources().openRawResource(R.raw.ota_002_package),
                packageFile.toPath());
        PayloadSpec spec = mPayloadSpecs.forNonStreaming(packageFile);

        assertEquals("correct url", "file://" + packageFile.getAbsolutePath(), spec.getUrl());
        assertEquals("correct payload offset",
                30 + PAYLOAD_BINARY_FILE_NAME.length(), spec.getOffset());
        assertEquals("correct payload size", 1392, spec.getSize());
        assertEquals(4, spec.getProperties().size());
        assertEquals(
                "FILE_HASH=sEAK/NMbU7GGe01xt55FsPafIPk8IYyBOAd6SiDpiMs=",
                spec.getProperties().get(0));
    }

    @Test
    public void forNonStreaming_IOException() throws Exception {
        thrown.expect(IOException.class);
        mPayloadSpecs.forNonStreaming(new File("/fake/news.zip"));
    }

    @Test
    public void forStreaming_works() throws Exception {
        String url = "http://a.com/b.zip";
        long offset = 45;
        long size = 200;
        File propertiesFile = createMockPropertiesFile();

        PayloadSpec spec = mPayloadSpecs.forStreaming(url, offset, size, propertiesFile);
        assertEquals("same url", url, spec.getUrl());
        assertEquals("same offset", offset, spec.getOffset());
        assertEquals("same size", size, spec.getSize());
        assertArrayEquals("correct properties",
                new String[]{"k1=val1", "key2=val2"}, spec.getProperties().toArray(new String[0]));
    }

    private File createMockPropertiesFile() throws IOException {
        File propertiesFile = new File(mTestDir, PackageFiles.PAYLOAD_PROPERTIES_FILE_NAME);
        Files.asCharSink(propertiesFile, Charsets.UTF_8).write(PROPERTIES_CONTENTS);
        return propertiesFile;
    }

}
