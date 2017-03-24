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

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <android-base/logging.h>
#include <linux/input.h>

#include "wear_touch.h"

#define DEVICE_PATH "/dev/input"

WearSwipeDetector::WearSwipeDetector(int low, int high, OnSwipeCallback callback, void* cookie):
    mLowThreshold(low),
    mHighThreshold(high),
    mCallback(callback),
    mCookie(cookie),
    mCurrentSlot(-1) {
    pthread_create(&mThread, NULL, touch_thread, this);
}

WearSwipeDetector::~WearSwipeDetector() {
}

void WearSwipeDetector::detect(int dx, int dy) {
    enum SwipeDirection direction;

    if (abs(dy) < mLowThreshold && abs(dx) > mHighThreshold) {
        direction = dx < 0 ? LEFT : RIGHT;
    } else if (abs(dx) < mLowThreshold && abs(dy) > mHighThreshold) {
        direction = dy < 0 ? UP : DOWN;
    } else {
        LOG(DEBUG) << "Ignore " << dx << " " << dy;
        return;
    }

    LOG(DEBUG) << "Swipe direction=" << direction;
    mCallback(mCookie, direction);
}

void WearSwipeDetector::process(struct input_event *event) {
    if (mCurrentSlot < 0) {
        mCallback(mCookie, UP);
        mCurrentSlot = 0;
    }

    if (event->type == EV_ABS) {
        if (event->code == ABS_MT_SLOT)
            mCurrentSlot = event->value;

        // Ignore other fingers
        if (mCurrentSlot > 0) {
            return;
        }

        switch (event->code) {
        case ABS_MT_POSITION_X:
            mX = event->value;
            mFingerDown = true;
            break;

        case ABS_MT_POSITION_Y:
            mY = event->value;
            mFingerDown = true;
            break;

        case ABS_MT_TRACKING_ID:
            if (event->value < 0)
                mFingerDown = false;
            break;
        }
    } else if (event->type == EV_SYN) {
        if (event->code == SYN_REPORT) {
            if (mFingerDown && !mSwiping) {
                mStartX = mX;
                mStartY = mY;
                mSwiping = true;
            } else if (!mFingerDown && mSwiping) {
                mSwiping = false;
                detect(mX - mStartX, mY - mStartY);
            }
        }
    }
}

void WearSwipeDetector::run() {
    int fd = findDevice(DEVICE_PATH);
    if (fd < 0) {
        LOG(ERROR) << "no input devices found";
        return;
    }

    struct input_event event;
    while (read(fd, &event, sizeof(event)) == sizeof(event)) {
        process(&event);
    }

    close(fd);
}

void* WearSwipeDetector::touch_thread(void* cookie) {
    (static_cast<WearSwipeDetector*>(cookie))->run();
    return NULL;
}

#define test_bit(bit, array)    ((array)[(bit)/8] & (1<<((bit)%8)))

int WearSwipeDetector::openDevice(const char *device) {
    int fd = open(device, O_RDONLY);
    if (fd < 0) {
        PLOG(ERROR) << "could not open " << device;
        return false;
    }

    char name[80];
    name[sizeof(name) - 1] = '\0';
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), &name) < 1) {
        PLOG(ERROR) << "could not get device name for " << device;
        name[0] = '\0';
    }

    uint8_t bits[512];
    memset(bits, 0, sizeof(bits));
    int ret = ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits);
    if (ret > 0) {
        if (test_bit(ABS_MT_POSITION_X, bits) && test_bit(ABS_MT_POSITION_Y, bits)) {
            LOG(DEBUG) << "Found " << device << " " << name;
            return fd;
        }
    }

    close(fd);
    return -1;
}

int WearSwipeDetector::findDevice(const char* path) {
    DIR* dir = opendir(path);
    if (dir == NULL) {
        PLOG(ERROR) << "Could not open directory " << path;
        return false;
    }

    struct dirent* entry;
    int ret = -1;
    while (ret < 0 && (entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char device[PATH_MAX];
        device[PATH_MAX-1] = '\0';
        snprintf(device, PATH_MAX-1, "%s/%s", path, entry->d_name);

        ret = openDevice(device);
    }

    closedir(dir);
    return ret;
}

