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

import static org.junit.Assert.assertArrayEquals;

import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import com.example.android.systemupdatersample.UpdateConfig;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.junit.runner.RunWith;

import java.util.Arrays;
import java.util.List;

/**
 * Tests for {@link UpdateConfigs}
 */
@RunWith(AndroidJUnit4.class)
@SmallTest
public class UpdateConfigsTest {

    @Rule
    public final ExpectedException thrown = ExpectedException.none();

    @Test
    public void configsToNames_extractsNames() {
        List<UpdateConfig> configs = Arrays.asList(
                new UpdateConfig("blah", "http://", UpdateConfig.AB_INSTALL_TYPE_NON_STREAMING),
                new UpdateConfig("blah 2", "http://", UpdateConfig.AB_INSTALL_TYPE_STREAMING)
        );
        String[] names = UpdateConfigs.configsToNames(configs);
        assertArrayEquals(new String[] {"blah", "blah 2"}, names);
    }
}
