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

import com.example.android.systemupdatersample.PayloadSpec;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Enumeration;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** The helper class that creates {@link PayloadSpec}. */
public class PayloadSpecs {

    public PayloadSpecs() {}

    /**
     * The payload PAYLOAD_ENTRY is stored in the zip package to comply with the Android OTA package
     * format. We want to find out the offset of the entry, so that we can pass it over to the A/B
     * updater without making an extra copy of the payload.
     *
     * <p>According to Android docs, the entries are listed in the order in which they appear in the
     * zip file. So we enumerate the entries to identify the offset of the payload file.
     * http://developer.android.com/reference/java/util/zip/ZipFile.html#entries()
     */
    public PayloadSpec forNonStreaming(File packageFile) throws IOException {
        boolean payloadFound = false;
        long payloadOffset = 0;
        long payloadSize = 0;

        List<String> properties = new ArrayList<>();
        try (ZipFile zip = new ZipFile(packageFile)) {
            Enumeration<? extends ZipEntry> entries = zip.entries();
            long offset = 0;
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                String name = entry.getName();
                // Zip local file header has 30 bytes + filename + sizeof extra field.
                // https://en.wikipedia.org/wiki/Zip_(file_format)
                long extraSize = entry.getExtra() == null ? 0 : entry.getExtra().length;
                offset += 30 + name.length() + extraSize;

                if (entry.isDirectory()) {
                    continue;
                }

                long length = entry.getCompressedSize();
                if (PackageFiles.PAYLOAD_BINARY_FILE_NAME.equals(name)) {
                    if (entry.getMethod() != ZipEntry.STORED) {
                        throw new IOException("Invalid compression method.");
                    }
                    payloadFound = true;
                    payloadOffset = offset;
                    payloadSize = length;
                } else if (PackageFiles.PAYLOAD_PROPERTIES_FILE_NAME.equals(name)) {
                    InputStream inputStream = zip.getInputStream(entry);
                    if (inputStream != null) {
                        BufferedReader br = new BufferedReader(new InputStreamReader(inputStream));
                        String line;
                        while ((line = br.readLine()) != null) {
                            properties.add(line);
                        }
                    }
                }
                offset += length;
            }
        }

        if (!payloadFound) {
            throw new IOException("Failed to find payload entry in the given package.");
        }
        return PayloadSpec.newBuilder()
                        .url("file://" + packageFile.getAbsolutePath())
                        .offset(payloadOffset)
                        .size(payloadSize)
                        .properties(properties)
                        .build();
    }

    /**
     * Creates a {@link PayloadSpec} for streaming update.
     */
    public PayloadSpec forStreaming(String updateUrl,
                                           long offset,
                                           long size,
                                           File propertiesFile) throws IOException {
        return PayloadSpec.newBuilder()
                .url(updateUrl)
                .offset(offset)
                .size(size)
                .properties(Files.readAllLines(propertiesFile.toPath()))
                .build();
    }

    /**
     * Converts an {@link PayloadSpec} to a string.
     */
    public String specToString(PayloadSpec payloadSpec) {
        return "<PayloadSpec url=" + payloadSpec.getUrl()
                + ", offset=" + payloadSpec.getOffset()
                + ", size=" + payloadSpec.getSize()
                + ", properties=" + Arrays.toString(
                        payloadSpec.getProperties().toArray(new String[0]))
                + ">";
    }

}
