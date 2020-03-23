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

import static org.junit.Assert.assertEquals;

import android.content.Context;

import androidx.test.InstrumentationRegistry;
import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import com.example.android.systemupdatersample.tests.R;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.junit.runner.RunWith;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Paths;

/**
 * Tests for {@link FileDownloader}
 */
@RunWith(AndroidJUnit4.class)
@SmallTest
public class FileDownloaderTest {

    @Rule
    public final ExpectedException thrown = ExpectedException.none();

    private Context mTestContext;
    private Context mTargetContext;

    @Before
    public void setUp() {
        mTestContext = InstrumentationRegistry.getContext();
        mTargetContext = InstrumentationRegistry.getTargetContext();
    }

    @Test
    public void download_downloadsChunkOfZip() throws Exception {
        // Prepare the target file
        File packageFile = Paths
                .get(mTargetContext.getCacheDir().getAbsolutePath(), "ota.zip")
                .toFile();
        Files.deleteIfExists(packageFile.toPath());
        Files.copy(mTestContext.getResources().openRawResource(R.raw.ota_002_package),
                packageFile.toPath());
        String url = "file://" + packageFile.getAbsolutePath();
        // prepare where to download
        File outFile = Paths
                .get(mTargetContext.getCacheDir().getAbsolutePath(), "care_map.txt")
                .toFile();
        Files.deleteIfExists(outFile.toPath());
        // download a chunk of ota.zip
        FileDownloader downloader = new FileDownloader(url, 1674, 12, outFile);
        downloader.download();
        String downloadedContent = String.join("\n", Files.readAllLines(outFile.toPath()));
        // archive contains text files with uppercase filenames
        assertEquals("CARE_MAP-TXT", downloadedContent);
    }

}
