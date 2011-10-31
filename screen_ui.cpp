/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include <cutils/android_reboot.h>
#include "minui/minui.h"
#include "ui.h"
#include "screen_ui.h"
#include "device.h"

#define CHAR_WIDTH 10
#define CHAR_HEIGHT 18

#define UI_WAIT_KEY_TIMEOUT_SEC    120

UIParameters ui_parameters = {
    6,       // indeterminate progress bar frames
    20,      // fps
    7,       // installation icon frames (0 == static image)
    13, 190, // installation icon overlay offset
};

// There's only (at most) one of these objects, and global callbacks
// (for pthread_create, and the input event system) need to find it,
// so use a global variable.
static ScreenRecoveryUI* self = NULL;

// Return the current time as a double (including fractions of a second).
static double now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

ScreenRecoveryUI::ScreenRecoveryUI() :
    currentIcon(NONE),
    installingFrame(0),
    progressBarType(EMPTY),
    progressScopeStart(0),
    progressScopeSize(0),
    progress(0),
    pagesIdentical(false),
    text_cols(0),
    text_rows(0),
    text_col(0),
    text_row(0),
    text_top(0),
    show_text(false),
    show_text_ever(false),
    show_menu(false),
    menu_top(0),
    menu_items(0),
    menu_sel(0),
    key_queue_len(0),
    key_last_down(-1) {
    pthread_mutex_init(&updateMutex, NULL);
    pthread_mutex_init(&key_queue_mutex, NULL);
    pthread_cond_init(&key_queue_cond, NULL);
    self = this;
}

// Draw the given frame over the installation overlay animation.  The
// background is not cleared or draw with the base icon first; we
// assume that the frame already contains some other frame of the
// animation.  Does nothing if no overlay animation is defined.
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::draw_install_overlay_locked(int frame) {
    if (installationOverlay == NULL) return;
    gr_surface surface = installationOverlay[frame];
    int iconWidth = gr_get_width(surface);
    int iconHeight = gr_get_height(surface);
    gr_blit(surface, 0, 0, iconWidth, iconHeight,
            ui_parameters.install_overlay_offset_x,
            ui_parameters.install_overlay_offset_y);
}

// Clear the screen and draw the currently selected background icon (if any).
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::draw_background_locked(Icon icon)
{
    pagesIdentical = false;
    gr_color(0, 0, 0, 255);
    gr_fill(0, 0, gr_fb_width(), gr_fb_height());

    if (icon) {
        gr_surface surface = backgroundIcon[icon];
        int iconWidth = gr_get_width(surface);
        int iconHeight = gr_get_height(surface);
        int iconX = (gr_fb_width() - iconWidth) / 2;
        int iconY = (gr_fb_height() - iconHeight) / 2;
        gr_blit(surface, 0, 0, iconWidth, iconHeight, iconX, iconY);
        if (icon == INSTALLING) {
            draw_install_overlay_locked(installingFrame);
        }
    }
}

// Draw the progress bar (if any) on the screen.  Does not flip pages.
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::draw_progress_locked()
{
    if (currentIcon == INSTALLING) {
        draw_install_overlay_locked(installingFrame);
    }

    if (progressBarType != EMPTY) {
        int iconHeight = gr_get_height(backgroundIcon[INSTALLING]);
        int width = gr_get_width(progressBarEmpty);
        int height = gr_get_height(progressBarEmpty);

        int dx = (gr_fb_width() - width)/2;
        int dy = (3*gr_fb_height() + iconHeight - 2*height)/4;

        // Erase behind the progress bar (in case this was a progress-only update)
        gr_color(0, 0, 0, 255);
        gr_fill(dx, dy, width, height);

        if (progressBarType == DETERMINATE) {
            float p = progressScopeStart + progress * progressScopeSize;
            int pos = (int) (p * width);

            if (pos > 0) {
                gr_blit(progressBarFill, 0, 0, pos, height, dx, dy);
            }
            if (pos < width-1) {
                gr_blit(progressBarEmpty, pos, 0, width-pos, height, dx+pos, dy);
            }
        }

        if (progressBarType == INDETERMINATE) {
            static int frame = 0;
            gr_blit(progressBarIndeterminate[frame], 0, 0, width, height, dx, dy);
            frame = (frame + 1) % ui_parameters.indeterminate_frames;
        }
    }
}

void ScreenRecoveryUI::draw_text_line(int row, const char* t) {
  if (t[0] != '\0') {
    gr_text(0, (row+1)*CHAR_HEIGHT-1, t);
  }
}

// Redraw everything on the screen.  Does not flip pages.
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::draw_screen_locked()
{
    draw_background_locked(currentIcon);
    draw_progress_locked();

    if (show_text) {
        gr_color(0, 0, 0, 160);
        gr_fill(0, 0, gr_fb_width(), gr_fb_height());

        int i = 0;
        if (show_menu) {
            gr_color(64, 96, 255, 255);
            gr_fill(0, (menu_top+menu_sel) * CHAR_HEIGHT,
                    gr_fb_width(), (menu_top+menu_sel+1)*CHAR_HEIGHT+1);

            for (; i < menu_top + menu_items; ++i) {
                if (i == menu_top + menu_sel) {
                    gr_color(255, 255, 255, 255);
                    draw_text_line(i, menu[i]);
                    gr_color(64, 96, 255, 255);
                } else {
                    draw_text_line(i, menu[i]);
                }
            }
            gr_fill(0, i*CHAR_HEIGHT+CHAR_HEIGHT/2-1,
                    gr_fb_width(), i*CHAR_HEIGHT+CHAR_HEIGHT/2+1);
            ++i;
        }

        gr_color(255, 255, 0, 255);

        for (; i < text_rows; ++i) {
            draw_text_line(i, text[(i+text_top) % text_rows]);
        }
    }
}

// Redraw everything on the screen and flip the screen (make it visible).
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::update_screen_locked()
{
    draw_screen_locked();
    gr_flip();
}

// Updates only the progress bar, if possible, otherwise redraws the screen.
// Should only be called with updateMutex locked.
void ScreenRecoveryUI::update_progress_locked()
{
    if (show_text || !pagesIdentical) {
        draw_screen_locked();    // Must redraw the whole screen
        pagesIdentical = true;
    } else {
        draw_progress_locked();  // Draw only the progress bar and overlays
    }
    gr_flip();
}

// Keeps the progress bar updated, even when the process is otherwise busy.
void* ScreenRecoveryUI::progress_thread(void *cookie)
{
    double interval = 1.0 / ui_parameters.update_fps;
    for (;;) {
        double start = now();
        pthread_mutex_lock(&self->updateMutex);

        int redraw = 0;

        // update the installation animation, if active
        // skip this if we have a text overlay (too expensive to update)
        if (self->currentIcon == INSTALLING &&
            ui_parameters.installing_frames > 0 &&
            !self->show_text) {
            self->installingFrame =
                (self->installingFrame + 1) % ui_parameters.installing_frames;
            redraw = 1;
        }

        // update the progress bar animation, if active
        // skip this if we have a text overlay (too expensive to update)
        if (self->progressBarType == INDETERMINATE && !self->show_text) {
            redraw = 1;
        }

        // move the progress bar forward on timed intervals, if configured
        int duration = self->progressScopeDuration;
        if (self->progressBarType == DETERMINATE && duration > 0) {
            double elapsed = now() - self->progressScopeTime;
            float progress = 1.0 * elapsed / duration;
            if (progress > 1.0) progress = 1.0;
            if (progress > progress) {
                progress = progress;
                redraw = 1;
            }
        }

        if (redraw) self->update_progress_locked();

        pthread_mutex_unlock(&self->updateMutex);
        double end = now();
        // minimum of 20ms delay between frames
        double delay = interval - (end-start);
        if (delay < 0.02) delay = 0.02;
        usleep((long)(delay * 1000000));
    }
    return NULL;
}

int ScreenRecoveryUI::input_callback(int fd, short revents, void* data)
{
    struct input_event ev;
    int ret;

    ret = ev_get_input(fd, revents, &ev);
    if (ret)
        return -1;

    if (ev.type == EV_SYN) {
        return 0;
    } else if (ev.type == EV_REL) {
        if (ev.code == REL_Y) {
            // accumulate the up or down motion reported by
            // the trackball.  When it exceeds a threshold
            // (positive or negative), fake an up/down
            // key event.
            self->rel_sum += ev.value;
            if (self->rel_sum > 3) {
                self->process_key(KEY_DOWN, 1);   // press down key
                self->process_key(KEY_DOWN, 0);   // and release it
                self->rel_sum = 0;
            } else if (self->rel_sum < -3) {
                self->process_key(KEY_UP, 1);     // press up key
                self->process_key(KEY_UP, 0);     // and release it
                self->rel_sum = 0;
            }
        }
    } else {
        self->rel_sum = 0;
    }

    if (ev.type == EV_KEY && ev.code <= KEY_MAX)
        self->process_key(ev.code, ev.value);

    return 0;
}

// Process a key-up or -down event.  A key is "registered" when it is
// pressed and then released, with no other keypresses or releases in
// between.  Registered keys are passed to CheckKey() to see if it
// should trigger a visibility toggle, an immediate reboot, or be
// queued to be processed next time the foreground thread wants a key
// (eg, for the menu).
//
// We also keep track of which keys are currently down so that
// CheckKey can call IsKeyPressed to see what other keys are held when
// a key is registered.
//
// updown == 1 for key down events; 0 for key up events
void ScreenRecoveryUI::process_key(int key_code, int updown) {
    bool register_key = false;

    pthread_mutex_lock(&key_queue_mutex);
    key_pressed[key_code] = updown;
    if (updown) {
        key_last_down = key_code;
    } else {
        if (key_last_down == key_code)
            register_key = true;
        key_last_down = -1;
    }
    pthread_mutex_unlock(&key_queue_mutex);

    if (register_key) {
        switch (CheckKey(key_code)) {
          case RecoveryUI::TOGGLE:
            pthread_mutex_lock(&updateMutex);
            show_text = !show_text;
            if (show_text) show_text_ever = true;
            update_screen_locked();
            pthread_mutex_unlock(&updateMutex);
            break;

          case RecoveryUI::REBOOT:
            android_reboot(ANDROID_RB_RESTART, 0, 0);
            break;

          case RecoveryUI::ENQUEUE:
            pthread_mutex_lock(&key_queue_mutex);
            const int queue_max = sizeof(key_queue) / sizeof(key_queue[0]);
            if (key_queue_len < queue_max) {
                key_queue[key_queue_len++] = key_code;
                pthread_cond_signal(&key_queue_cond);
            }
            pthread_mutex_unlock(&key_queue_mutex);
            break;
        }
    }
}

// Reads input events, handles special hot keys, and adds to the key queue.
void* ScreenRecoveryUI::input_thread(void *cookie)
{
    for (;;) {
        if (!ev_wait(-1))
            ev_dispatch();
    }
    return NULL;
}

void ScreenRecoveryUI::LoadBitmap(const char* filename, gr_surface* surface) {
    int result = res_create_surface(filename, surface);
    if (result < 0) {
        LOGE("missing bitmap %s\n(Code %d)\n", filename, result);
    }
}

void ScreenRecoveryUI::Init()
{
    gr_init();
    ev_init(input_callback, NULL);

    text_col = text_row = 0;
    text_rows = gr_fb_height() / CHAR_HEIGHT;
    if (text_rows > kMaxRows) text_rows = kMaxRows;
    text_top = 1;

    text_cols = gr_fb_width() / CHAR_WIDTH;
    if (text_cols > kMaxCols - 1) text_cols = kMaxCols - 1;

    LoadBitmap("icon_installing", &backgroundIcon[INSTALLING]);
    LoadBitmap("icon_error", &backgroundIcon[ERROR]);
    LoadBitmap("progress_empty", &progressBarEmpty);
    LoadBitmap("progress_fill", &progressBarFill);

    int i;

    progressBarIndeterminate = (gr_surface*)malloc(ui_parameters.indeterminate_frames *
                                                    sizeof(gr_surface));
    for (i = 0; i < ui_parameters.indeterminate_frames; ++i) {
        char filename[40];
        // "indeterminate01.png", "indeterminate02.png", ...
        sprintf(filename, "indeterminate%02d", i+1);
        LoadBitmap(filename, progressBarIndeterminate+i);
    }

    if (ui_parameters.installing_frames > 0) {
        installationOverlay = (gr_surface*)malloc(ui_parameters.installing_frames *
                                                   sizeof(gr_surface));
        for (i = 0; i < ui_parameters.installing_frames; ++i) {
            char filename[40];
            // "icon_installing_overlay01.png",
            // "icon_installing_overlay02.png", ...
            sprintf(filename, "icon_installing_overlay%02d", i+1);
            LoadBitmap(filename, installationOverlay+i);
        }

        // Adjust the offset to account for the positioning of the
        // base image on the screen.
        if (backgroundIcon[INSTALLING] != NULL) {
            gr_surface bg = backgroundIcon[INSTALLING];
            ui_parameters.install_overlay_offset_x +=
                (gr_fb_width() - gr_get_width(bg)) / 2;
            ui_parameters.install_overlay_offset_y +=
                (gr_fb_height() - gr_get_height(bg)) / 2;
        }
    } else {
        installationOverlay = NULL;
    }

    pthread_create(&progress_t, NULL, progress_thread, NULL);
    pthread_create(&input_t, NULL, input_thread, NULL);
}

void ScreenRecoveryUI::SetBackground(Icon icon)
{
    pthread_mutex_lock(&updateMutex);
    currentIcon = icon;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::SetProgressType(ProgressType type)
{
    pthread_mutex_lock(&updateMutex);
    if (progressBarType != type) {
        progressBarType = type;
        update_progress_locked();
    }
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::ShowProgress(float portion, float seconds)
{
    pthread_mutex_lock(&updateMutex);
    progressBarType = DETERMINATE;
    progressScopeStart += progressScopeSize;
    progressScopeSize = portion;
    progressScopeTime = now();
    progressScopeDuration = seconds;
    progress = 0;
    update_progress_locked();
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::SetProgress(float fraction)
{
    pthread_mutex_lock(&updateMutex);
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    if (progressBarType == DETERMINATE && fraction > progress) {
        // Skip updates that aren't visibly different.
        int width = gr_get_width(progressBarIndeterminate[0]);
        float scale = width * progressScopeSize;
        if ((int) (progress * scale) != (int) (fraction * scale)) {
            progress = fraction;
            update_progress_locked();
        }
    }
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::Print(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, 256, fmt, ap);
    va_end(ap);

    fputs(buf, stdout);

    // This can get called before ui_init(), so be careful.
    pthread_mutex_lock(&updateMutex);
    if (text_rows > 0 && text_cols > 0) {
        char *ptr;
        for (ptr = buf; *ptr != '\0'; ++ptr) {
            if (*ptr == '\n' || text_col >= text_cols) {
                text[text_row][text_col] = '\0';
                text_col = 0;
                text_row = (text_row + 1) % text_rows;
                if (text_row == text_top) text_top = (text_top + 1) % text_rows;
            }
            if (*ptr != '\n') text[text_row][text_col++] = *ptr;
        }
        text[text_row][text_col] = '\0';
        update_screen_locked();
    }
    pthread_mutex_unlock(&updateMutex);
}

void ScreenRecoveryUI::StartMenu(const char* const * headers, const char* const * items,
                                 int initial_selection) {
    int i;
    pthread_mutex_lock(&updateMutex);
    if (text_rows > 0 && text_cols > 0) {
        for (i = 0; i < text_rows; ++i) {
            if (headers[i] == NULL) break;
            strncpy(menu[i], headers[i], text_cols-1);
            menu[i][text_cols-1] = '\0';
        }
        menu_top = i;
        for (; i < text_rows; ++i) {
            if (items[i-menu_top] == NULL) break;
            strncpy(menu[i], items[i-menu_top], text_cols-1);
            menu[i][text_cols-1] = '\0';
        }
        menu_items = i - menu_top;
        show_menu = 1;
        menu_sel = initial_selection;
        update_screen_locked();
    }
    pthread_mutex_unlock(&updateMutex);
}

int ScreenRecoveryUI::SelectMenu(int sel) {
    int old_sel;
    pthread_mutex_lock(&updateMutex);
    if (show_menu > 0) {
        old_sel = menu_sel;
        menu_sel = sel;
        if (menu_sel < 0) menu_sel = 0;
        if (menu_sel >= menu_items) menu_sel = menu_items-1;
        sel = menu_sel;
        if (menu_sel != old_sel) update_screen_locked();
    }
    pthread_mutex_unlock(&updateMutex);
    return sel;
}

void ScreenRecoveryUI::EndMenu() {
    int i;
    pthread_mutex_lock(&updateMutex);
    if (show_menu > 0 && text_rows > 0 && text_cols > 0) {
        show_menu = 0;
        update_screen_locked();
    }
    pthread_mutex_unlock(&updateMutex);
}

bool ScreenRecoveryUI::IsTextVisible()
{
    pthread_mutex_lock(&updateMutex);
    int visible = show_text;
    pthread_mutex_unlock(&updateMutex);
    return visible;
}

bool ScreenRecoveryUI::WasTextEverVisible()
{
    pthread_mutex_lock(&updateMutex);
    int ever_visible = show_text_ever;
    pthread_mutex_unlock(&updateMutex);
    return ever_visible;
}

void ScreenRecoveryUI::ShowText(bool visible)
{
    pthread_mutex_lock(&updateMutex);
    show_text = visible;
    if (show_text) show_text_ever = 1;
    update_screen_locked();
    pthread_mutex_unlock(&updateMutex);
}

// Return true if USB is connected.
bool ScreenRecoveryUI::usb_connected() {
    int fd = open("/sys/class/android_usb/android0/state", O_RDONLY);
    if (fd < 0) {
        printf("failed to open /sys/class/android_usb/android0/state: %s\n",
               strerror(errno));
        return 0;
    }

    char buf;
    /* USB is connected if android_usb state is CONNECTED or CONFIGURED */
    int connected = (read(fd, &buf, 1) == 1) && (buf == 'C');
    if (close(fd) < 0) {
        printf("failed to close /sys/class/android_usb/android0/state: %s\n",
               strerror(errno));
    }
    return connected;
}

int ScreenRecoveryUI::WaitKey()
{
    pthread_mutex_lock(&key_queue_mutex);

    // Time out after UI_WAIT_KEY_TIMEOUT_SEC, unless a USB cable is
    // plugged in.
    do {
        struct timeval now;
        struct timespec timeout;
        gettimeofday(&now, NULL);
        timeout.tv_sec = now.tv_sec;
        timeout.tv_nsec = now.tv_usec * 1000;
        timeout.tv_sec += UI_WAIT_KEY_TIMEOUT_SEC;

        int rc = 0;
        while (key_queue_len == 0 && rc != ETIMEDOUT) {
            rc = pthread_cond_timedwait(&key_queue_cond, &key_queue_mutex,
                                        &timeout);
        }
    } while (usb_connected() && key_queue_len == 0);

    int key = -1;
    if (key_queue_len > 0) {
        key = key_queue[0];
        memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);
    }
    pthread_mutex_unlock(&key_queue_mutex);
    return key;
}

bool ScreenRecoveryUI::IsKeyPressed(int key)
{
    pthread_mutex_lock(&key_queue_mutex);
    int pressed = key_pressed[key];
    pthread_mutex_unlock(&key_queue_mutex);
    return pressed;
}

void ScreenRecoveryUI::FlushKeys() {
    pthread_mutex_lock(&key_queue_mutex);
    key_queue_len = 0;
    pthread_mutex_unlock(&key_queue_mutex);
}

RecoveryUI::KeyAction ScreenRecoveryUI::CheckKey(int key) {
    return RecoveryUI::ENQUEUE;
}
