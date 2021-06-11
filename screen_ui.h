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

#ifndef RECOVERY_SCREEN_UI_H
#define RECOVERY_SCREEN_UI_H

#include <pthread.h>
#include <stdio.h>

#include <string>
#include <vector>

#include "ui.h"

// From minui/minui.h.
struct GRSurface;

// Implementation of RecoveryUI appropriate for devices with a screen
// (shows an icon + a progress bar, text logging, menu, etc.)
class ScreenRecoveryUI : public RecoveryUI {
 public:
  enum UIElement {
    HEADER,
    MENU,
    MENU_SEL_BG,
    MENU_SEL_BG_ACTIVE,
    MENU_SEL_FG,
    LOG,
    TEXT_FILL,
    INFO
  };

  ScreenRecoveryUI();

  bool Init(const std::string& locale) override;

  // overall recovery state ("background image")
  void SetBackground(Icon icon) override;
  void SetSystemUpdateText(bool security_update) override;

  // progress indicator
  void SetProgressType(ProgressType type) override;
  void ShowProgress(float portion, float seconds) override;
  void SetProgress(float fraction) override;

  void SetStage(int current, int max) override;

  // text log
  void ShowText(bool visible) override;
  bool IsTextVisible() override;
  bool WasTextEverVisible() override;

  // printing messages
  void Print(const char* fmt, ...) override __printflike(2, 3);
  void PrintOnScreenOnly(const char* fmt, ...) override __printflike(2, 3);
  void ShowFile(const char* filename) override;

  // menu display
  void StartMenu(const char* const* headers, const char* const* items,
                 int initial_selection) override;
  int SelectMenu(int sel) override;
  void EndMenu() override;

  void KeyLongPress(int) override;

  void Redraw();

  void SetColor(UIElement e) const;

  // Check the background text image. Use volume up/down button to cycle through the locales
  // embedded in the png file, and power button to go back to recovery main menu.
  void CheckBackgroundTextImages(const std::string& saved_locale);

 protected:
  // The margin that we don't want to use for showing texts (e.g. round screen, or screen with
  // rounded corners).
  const int kMarginWidth;
  const int kMarginHeight;

  // Number of frames per sec (default: 30) for both parts of the animation.
  const int kAnimationFps;

  // The scale factor from dp to pixels. 1.0 for mdpi, 4.0 for xxxhdpi.
  const float kDensity;

  virtual bool InitTextParams();

  virtual void draw_background_locked();
  virtual void draw_foreground_locked();
  virtual void draw_screen_locked();
  virtual void update_screen_locked();
  virtual void update_progress_locked();

  GRSurface* GetCurrentFrame() const;
  GRSurface* GetCurrentText() const;

  static void* ProgressThreadStartRoutine(void* data);
  void ProgressThreadLoop();

  virtual void ShowFile(FILE*);
  virtual void PrintV(const char*, bool, va_list);
  void PutChar(char);
  void ClearText();

  void LoadAnimation();
  void LoadBitmap(const char* filename, GRSurface** surface);
  void LoadLocalizedBitmap(const char* filename, GRSurface** surface);

  int PixelsFromDp(int dp) const;
  virtual int GetAnimationBaseline() const;
  virtual int GetProgressBaseline() const;
  virtual int GetTextBaseline() const;

  // Returns pixel width of draw buffer.
  virtual int ScreenWidth() const;
  // Returns pixel height of draw buffer.
  virtual int ScreenHeight() const;

  // Draws a highlight bar at (x, y) - (x + width, y + height).
  virtual void DrawHighlightBar(int x, int y, int width, int height) const;
  // Draws a horizontal rule at Y. Returns the offset it should be moving along Y-axis.
  virtual int DrawHorizontalRule(int y) const;
  // Draws a line of text. Returns the offset it should be moving along Y-axis.
  virtual int DrawTextLine(int x, int y, const char* line, bool bold) const;
  // Draws surface portion (sx, sy, w, h) at screen location (dx, dy).
  virtual void DrawSurface(GRSurface* surface, int sx, int sy, int w, int h, int dx, int dy) const;
  // Draws rectangle at (x, y) - (x + w, y + h).
  virtual void DrawFill(int x, int y, int w, int h) const;
  // Draws given surface (surface->pixel_bytes = 1) as text at (x, y).
  virtual void DrawTextIcon(int x, int y, GRSurface* surface) const;
  // Draws multiple text lines. Returns the offset it should be moving along Y-axis.
  int DrawTextLines(int x, int y, const char* const* lines) const;
  // Similar to DrawTextLines() to draw multiple text lines, but additionally wraps long lines.
  // Returns the offset it should be moving along Y-axis.
  int DrawWrappedTextLines(int x, int y, const char* const* lines) const;

  Icon currentIcon;

  // The layout to use.
  int layout_;

  GRSurface* error_icon;

  GRSurface* erasing_text;
  GRSurface* error_text;
  GRSurface* installing_text;
  GRSurface* no_command_text;

  GRSurface** introFrames;
  GRSurface** loopFrames;

  GRSurface* progressBarEmpty;
  GRSurface* progressBarFill;
  GRSurface* stageMarkerEmpty;
  GRSurface* stageMarkerFill;

  ProgressType progressBarType;

  float progressScopeStart, progressScopeSize, progress;
  double progressScopeTime, progressScopeDuration;

  // true when both graphics pages are the same (except for the progress bar).
  bool pagesIdentical;

  size_t text_cols_, text_rows_;

  // Log text overlay, displayed when a magic key is pressed.
  char** text_;
  size_t text_col_, text_row_;

  bool show_text;
  bool show_text_ever;  // has show_text ever been true?

  std::vector<std::string> menu_;
  const char* const* menu_headers_;
  bool show_menu;
  int menu_items, menu_sel;

  // An alternate text screen, swapped with 'text_' when we're viewing a log file.
  char** file_viewer_text_;

  pthread_t progress_thread_;

  // Number of intro frames and loop frames in the animation.
  size_t intro_frames;
  size_t loop_frames;

  size_t current_frame;
  bool intro_done;

  int stage, max_stage;

  int char_width_;
  int char_height_;

  // The locale that's used to show the rendered texts.
  std::string locale_;
  bool rtl_locale_;

  pthread_mutex_t updateMutex;

 private:
  void SetLocale(const std::string&);

  // Display the background texts for "erasing", "error", "no_command" and "installing" for the
  // selected locale.
  void SelectAndShowBackgroundText(const std::vector<std::string>& locales_entries, size_t sel);
};

#endif  // RECOVERY_UI_H
