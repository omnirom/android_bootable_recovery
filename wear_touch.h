/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef __WEAR_TOUCH_H
#define __WEAR_TOUCH_H

#include <pthread.h>

class WearSwipeDetector {

public:
    enum SwipeDirection { UP, DOWN, RIGHT, LEFT };
    typedef void (*OnSwipeCallback)(void* cookie, enum SwipeDirection direction);

    WearSwipeDetector(int low, int high, OnSwipeCallback cb, void* cookie);
    ~WearSwipeDetector();

private:
    void run();
    void process(struct input_event *event);
    void detect(int dx, int dy);

    pthread_t mThread;
    static void* touch_thread(void* cookie);

    int findDevice(const char* path);
    int openDevice(const char* device);

    int mLowThreshold;
    int mHighThreshold;

    OnSwipeCallback mCallback;
    void *mCookie;

    int mX;
    int mY;
    int mStartX;
    int mStartY;

    int mCurrentSlot;
    bool mFingerDown;
    bool mSwiping;
};

#endif // __WEAR_TOUCH_H
