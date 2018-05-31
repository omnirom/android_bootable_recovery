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

import android.util.SparseArray;

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;

import java.util.concurrent.atomic.AtomicInteger;

/**
 * Controls updater state.
 */
public class UpdaterState {

    public static final int IDLE = 0;
    public static final int ERROR = 1;
    public static final int RUNNING = 2;
    public static final int PAUSED = 3;
    public static final int SLOT_SWITCH_REQUIRED = 4;
    public static final int REBOOT_REQUIRED = 5;

    private static final SparseArray<String> STATE_MAP = new SparseArray<>();

    static {
        STATE_MAP.put(0, "IDLE");
        STATE_MAP.put(1, "ERROR");
        STATE_MAP.put(2, "RUNNING");
        STATE_MAP.put(3, "PAUSED");
        STATE_MAP.put(4, "SLOT_SWITCH_REQUIRED");
        STATE_MAP.put(5, "REBOOT_REQUIRED");
    }

    /**
     * Allowed state transitions. It's a map: key is a state, value is a set of states that
     * are allowed to transition to from key.
     */
    private static final ImmutableMap<Integer, ImmutableSet<Integer>> TRANSITIONS =
            ImmutableMap.<Integer, ImmutableSet<Integer>>builder()
                    .put(IDLE, ImmutableSet.of(IDLE, ERROR, RUNNING))
                    .put(ERROR, ImmutableSet.of(IDLE))
                    .put(RUNNING, ImmutableSet.of(
                            IDLE, ERROR, PAUSED, REBOOT_REQUIRED, SLOT_SWITCH_REQUIRED))
                    .put(PAUSED, ImmutableSet.of(ERROR, RUNNING, IDLE))
                    .put(SLOT_SWITCH_REQUIRED, ImmutableSet.of(ERROR, REBOOT_REQUIRED, IDLE))
                    .put(REBOOT_REQUIRED, ImmutableSet.of(IDLE))
                    .build();

    private AtomicInteger mState;

    public UpdaterState(int state) {
        this.mState = new AtomicInteger(state);
    }

    /**
     * Returns updater state.
     */
    public int get() {
        return mState.get();
    }

    /**
     * Sets the updater state.
     *
     * @throws InvalidTransitionException if transition is not allowed.
     */
    public void set(int newState) throws InvalidTransitionException {
        int oldState = mState.get();
        if (!TRANSITIONS.get(oldState).contains(newState)) {
            throw new InvalidTransitionException(
                    "Can't transition from " + oldState + " to " + newState);
        }
        mState.set(newState);
    }

    /**
     * Converts status code to status name.
     */
    public static String getStateText(int state) {
        return STATE_MAP.get(state);
    }

    /**
     * Defines invalid state transition exception.
     */
    public static class InvalidTransitionException extends Exception {
        public InvalidTransitionException(String msg) {
            super(msg);
        }
    }
}
