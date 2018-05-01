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
import static com.example.android.systemupdatersample.util.PackageFiles.PAYLOAD_PROPERTIES_FILE_NAME;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;

import android.content.Context;
import android.support.test.InstrumentationRegistry;
import android.support.test.filters.SmallTest;
import android.support.test.runner.AndroidJUnit4;

import com.example.android.systemupdatersample.PayloadSpec;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.junit.runner.RunWith;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * Tests if PayloadSpecs parses update package zip file correctly.
 */
@RunWith(AndroidJUnit4.class)
@SmallTest
public class PayloadSpecsTest {

    private static final String PROPERTIES_CONTENTS = "k1=val1\nkey2=val2";
    private static final String PAYLOAD_CONTENTS    = "hello\nworld";
    private static final int PAYLOAD_SIZE           = PAYLOAD_CONTENTS.length();

    private File mTestDir;

    private Context mContext;

    @Rule
    public final ExpectedException thrown = ExpectedException.none();

    @Before
    public void setUp() {
        mContext = InstrumentationRegistry.getTargetContext();

        mTestDir = mContext.getFilesDir();
    }

    @Test
    public void forNonStreaming_works() throws Exception {
        File packageFile = createMockZipFile();
        PayloadSpec spec = PayloadSpecs.forNonStreaming(packageFile);

        assertEquals("correct url", "file://" + packageFile.getAbsolutePath(), spec.getUrl());
        assertEquals("correct payload offset",
                30 + PAYLOAD_BINARY_FILE_NAME.length(), spec.getOffset());
        assertEquals("correct payload size", PAYLOAD_SIZE, spec.getSize());
        assertArrayEquals("correct properties",
                new String[]{"k1=val1", "key2=val2"}, spec.getProperties().toArray(new String[0]));
    }

    @Test
    public void forNonStreaming_IOException() throws Exception {
        thrown.expect(IOException.class);
        PayloadSpecs.forNonStreaming(new File("/fake/news.zip"));
    }

    /**
     * Creates package zip file that contains payload.bin and payload_properties.txt
     */
    private File createMockZipFile() throws IOException {
        File testFile = new File(mTestDir, "test.zip");
        try (ZipOutputStream zos = new ZipOutputStream(new FileOutputStream(testFile))) {
            // Add payload.bin entry.
            ZipEntry entry = new ZipEntry(PAYLOAD_BINARY_FILE_NAME);
            entry.setMethod(ZipEntry.STORED);
            entry.setCompressedSize(PAYLOAD_SIZE);
            entry.setSize(PAYLOAD_SIZE);
            CRC32 crc = new CRC32();
            crc.update(PAYLOAD_CONTENTS.getBytes(StandardCharsets.UTF_8));
            entry.setCrc(crc.getValue());
            zos.putNextEntry(entry);
            zos.write(PAYLOAD_CONTENTS.getBytes(StandardCharsets.UTF_8));
            zos.closeEntry();

            // Add payload properties entry.
            ZipEntry propertiesEntry = new ZipEntry(PAYLOAD_PROPERTIES_FILE_NAME);
            zos.putNextEntry(propertiesEntry);
            zos.write(PROPERTIES_CONTENTS.getBytes(StandardCharsets.UTF_8));
            zos.closeEntry();
        }
        return testFile;
    }

}
