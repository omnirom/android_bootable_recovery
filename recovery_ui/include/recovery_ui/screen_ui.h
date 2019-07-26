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

#include <stdio.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ui.h"

// From minui/minui.h.
class GRSurface;

enum class UIElement {
  HEADER,
  MENU,
  MENU_SEL_BG,
  MENU_SEL_BG_ACTIVE,
  MENU_SEL_FG,
  LOG,
  TEXT_FILL,
  INFO
};

// Interface to draw the UI elements on the screen.
class DrawInterface {
 public:
  virtual ~DrawInterface() = default;

  // Sets the color to the predefined value for |element|.
  virtual void SetColor(UIElement element) const = 0;

  // Draws a highlight bar at (x, y) - (x + width, y + height).
  virtual void DrawHighlightBar(int x, int y, int width, int height) const = 0;

  // Draws a horizontal rule at Y. Returns the offset it should be moving along Y-axis.
  virtual int DrawHorizontalRule(int y) const = 0;

  // Draws a line of text. Returns the offset it should be moving along Y-axis.
  virtual int DrawTextLine(int x, int y, const std::string& line, bool bold) const = 0;

  // Draws surface portion (sx, sy, w, h) at screen location (dx, dy).
  virtual void DrawSurface(const GRSurface* surface, int sx, int sy, int w, int h, int dx,
                           int dy) const = 0;

  // Draws rectangle at (x, y) - (x + w, y + h).
  virtual void DrawFill(int x, int y, int w, int h) const = 0;

  // Draws given surface (surface->pixel_bytes = 1) as text at (x, y).
  virtual void DrawTextIcon(int x, int y, const GRSurface* surface) const = 0;

  // Draws multiple text lines. Returns the offset it should be moving along Y-axis.
  virtual int DrawTextLines(int x, int y, const std::vector<std::string>& lines) const = 0;

  // Similar to DrawTextLines() to draw multiple text lines, but additionally wraps long lines. It
  // keeps symmetrical margins of 'x' at each end of a line. Returns the offset it should be moving
  // along Y-axis.
  virtual int DrawWrappedTextLines(int x, int y, const std::vector<std::string>& lines) const = 0;
};

// Interface for classes that maintain the menu selection and display.
class Menu {
 public:
  virtual ~Menu() = default;
  // Returns the current menu selection.
  size_t selection() const;
  // Sets the current selection to |sel|. Handle the overflow cases depending on if the menu is
  // scrollable.
  virtual int Select(int sel) = 0;
  // Displays the menu headers on the screen at offset x, y
  virtual int DrawHeader(int x, int y) const = 0;
  // Iterates over the menu items and displays each of them at offset x, y.
  virtual int DrawItems(int x, int y, int screen_width, bool long_press) const = 0;

 protected:
  Menu(size_t initial_selection, const DrawInterface& draw_func);
  // Current menu selection.
  size_t selection_;
  // Reference to the class that implements all the draw functions.
  const DrawInterface& draw_funcs_;
};

// This class uses strings as the menu header and items.
class TextMenu : public Menu {
 public:
  // Constructs a Menu instance with the given |headers|, |items| and properties. Sets the initial
  // selection to |initial_selection|.
  TextMenu(bool scrollable, size_t max_items, size_t max_length,
           const std::vector<std::string>& headers, const std::vector<std::string>& items,
           size_t initial_selection, int char_height, const DrawInterface& draw_funcs);

  int Select(int sel) override;
  int DrawHeader(int x, int y) const override;
  int DrawItems(int x, int y, int screen_width, bool long_press) const override;

  bool scrollable() const {
    return scrollable_;
  }

  // Returns count of menu items.
  size_t ItemsCount() const;

  // Returns the index of the first menu item.
  size_t MenuStart() const;

  // Returns the index of the last menu item + 1.
  size_t MenuEnd() const;

  // Menu example:
  // info:                           Android Recovery
  //                                 ....
  // help messages:                  Swipe up/down to move
  //                                 Swipe left/right to select
  // empty line (horizontal rule):
  // menu headers:                   Select file to view
  // menu items:                     /cache/recovery/last_log
  //                                 /cache/recovery/last_log.1
  //                                 /cache/recovery/last_log.2
  //                                 ...
  const std::vector<std::string>& text_headers() const;
  std::string TextItem(size_t index) const;

  // Checks if the menu items fit vertically on the screen. Returns true and set the
  // |cur_selection_str| if the items exceed the screen limit.
  bool ItemsOverflow(std::string* cur_selection_str) const;

 private:
  // The menu is scrollable to display more items. Used on wear devices who have smaller screens.
  const bool scrollable_;
  // The max number of menu items to fit vertically on a screen.
  const size_t max_display_items_;
  // The length of each item to fit horizontally on a screen.
  const size_t max_item_length_;
  // The menu headers.
  std::vector<std::string> text_headers_;
  // The actual menu items trimmed to fit the given properties.
  std::vector<std::string> text_items_;
  // The first item to display on the screen.
  size_t menu_start_;

  // Height in pixels of each character.
  int char_height_;
};

// This class uses GRSurface's as the menu header and items.
class GraphicMenu : public Menu {
 public:
  // Constructs a Menu instance with the given |headers|, |items| and properties. Sets the initial
  // selection to |initial_selection|. |headers| and |items| will be made local copies.
  GraphicMenu(const GRSurface* graphic_headers, const std::vector<const GRSurface*>& graphic_items,
              size_t initial_selection, const DrawInterface& draw_funcs);

  int Select(int sel) override;
  int DrawHeader(int x, int y) const override;
  int DrawItems(int x, int y, int screen_width, bool long_press) const override;

  // Checks if all the header and items are valid GRSurface's; and that they can fit in the area
  // defined by |max_width| and |max_height|.
  static bool Validate(size_t max_width, size_t max_height, const GRSurface* graphic_headers,
                       const std::vector<const GRSurface*>& graphic_items);

  // Returns true if |surface| fits on the screen with a vertical offset |y|.
  static bool ValidateGraphicSurface(size_t max_width, size_t max_height, int y,
                                     const GRSurface* surface);

 private:
  // Menu headers and items in graphic icons. These are the copies owned by the class instance.
  std::unique_ptr<GRSurface> graphic_headers_;
  std::vector<std::unique_ptr<GRSurface>> graphic_items_;
};

// Implementation of RecoveryUI appropriate for devices with a screen
// (shows an icon + a progress bar, text logging, menu, etc.)
class ScreenRecoveryUI : public RecoveryUI, public DrawInterface {
 public:
  ScreenRecoveryUI();
  explicit ScreenRecoveryUI(bool scrollable_menu);
  ~ScreenRecoveryUI() override;

  bool Init(const std::string& locale) override;
  std::string GetLocale() const override;

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
  void ShowFile(const std::string& filename) override;

  // menu display
  size_t ShowMenu(const std::vector<std::string>& headers, const std::vector<std::string>& items,
                  size_t initial_selection, bool menu_only,
                  const std::function<int(int, bool)>& key_handler) override;
  void SetTitle(const std::vector<std::string>& lines) override;

  void KeyLongPress(int) override;

  void Redraw();

  // Checks the background text image, for debugging purpose. It iterates the locales embedded in
  // the on-device resource files and shows the localized text, for manual inspection.
  void CheckBackgroundTextImages();

  // Displays the localized wipe data menu.
  size_t ShowPromptWipeDataMenu(const std::vector<std::string>& backup_headers,
                                const std::vector<std::string>& backup_items,
                                const std::function<int(int, bool)>& key_handler) override;

  // Displays the localized wipe data confirmation menu.
  size_t ShowPromptWipeDataConfirmationMenu(
      const std::vector<std::string>& backup_headers, const std::vector<std::string>& backup_items,
      const std::function<int(int, bool)>& key_handler) override;

 protected:
  static constexpr int kMenuIndent = 4;

  // The margin that we don't want to use for showing texts (e.g. round screen, or screen with
  // rounded corners).
  const int margin_width_;
  const int margin_height_;

  // Number of frames per sec (default: 30) for both parts of the animation.
  const int animation_fps_;

  // The scale factor from dp to pixels. 1.0 for mdpi, 4.0 for xxxhdpi.
  const float density_;

  virtual bool InitTextParams();

  virtual bool LoadWipeDataMenuText();

  // Creates a GraphicMenu with |graphic_header| and |graphic_items|. If the GraphicMenu isn't
  // valid or it doesn't fit on the screen; falls back to create a TextMenu instead. If succeeds,
  // returns a unique pointer to the created menu; otherwise returns nullptr.
  virtual std::unique_ptr<Menu> CreateMenu(const GRSurface* graphic_header,
                                           const std::vector<const GRSurface*>& graphic_items,
                                           const std::vector<std::string>& text_headers,
                                           const std::vector<std::string>& text_items,
                                           size_t initial_selection) const;

  // Creates a TextMenu with |text_headers| and |text_items|; and sets the menu selection to
  // |initial_selection|.
  virtual std::unique_ptr<Menu> CreateMenu(const std::vector<std::string>& text_headers,
                                           const std::vector<std::string>& text_items,
                                           size_t initial_selection) const;

  // Takes the ownership of |menu| and displays it.
  virtual size_t ShowMenu(std::unique_ptr<Menu>&& menu, bool menu_only,
                          const std::function<int(int, bool)>& key_handler);

  // Sets the menu highlight to the given index, wrapping if necessary. Returns the actual item
  // selected.
  virtual int SelectMenu(int sel);

  // Returns the help message displayed on top of the menu.
  virtual std::vector<std::string> GetMenuHelpMessage() const;

  virtual void draw_background_locked();
  virtual void draw_foreground_locked();
  virtual void draw_screen_locked();
  virtual void draw_menu_and_text_buffer_locked(const std::vector<std::string>& help_message);
  virtual void update_screen_locked();
  virtual void update_progress_locked();

  const GRSurface* GetCurrentFrame() const;
  const GRSurface* GetCurrentText() const;

  void ProgressThreadLoop();

  virtual void ShowFile(FILE*);
  virtual void PrintV(const char*, bool, va_list);
  void PutChar(char);
  void ClearText();

  void LoadAnimation();
  std::unique_ptr<GRSurface> LoadBitmap(const std::string& filename);
  std::unique_ptr<GRSurface> LoadLocalizedBitmap(const std::string& filename);

  int PixelsFromDp(int dp) const;
  virtual int GetAnimationBaseline() const;
  virtual int GetProgressBaseline() const;
  virtual int GetTextBaseline() const;

  // Returns pixel width of draw buffer.
  virtual int ScreenWidth() const;
  // Returns pixel height of draw buffer.
  virtual int ScreenHeight() const;

  // Implementation of the draw functions in DrawInterface.
  void SetColor(UIElement e) const override;
  void DrawHighlightBar(int x, int y, int width, int height) const override;
  int DrawHorizontalRule(int y) const override;
  void DrawSurface(const GRSurface* surface, int sx, int sy, int w, int h, int dx,
                   int dy) const override;
  void DrawFill(int x, int y, int w, int h) const override;
  void DrawTextIcon(int x, int y, const GRSurface* surface) const override;
  int DrawTextLine(int x, int y, const std::string& line, bool bold) const override;
  int DrawTextLines(int x, int y, const std::vector<std::string>& lines) const override;
  int DrawWrappedTextLines(int x, int y, const std::vector<std::string>& lines) const override;

  // The layout to use.
  int layout_;

  // The images that contain localized texts.
  std::unique_ptr<GRSurface> erasing_text_;
  std::unique_ptr<GRSurface> error_text_;
  std::unique_ptr<GRSurface> installing_text_;
  std::unique_ptr<GRSurface> no_command_text_;

  // Localized text images for the wipe data menu.
  std::unique_ptr<GRSurface> cancel_wipe_data_text_;
  std::unique_ptr<GRSurface> factory_data_reset_text_;
  std::unique_ptr<GRSurface> try_again_text_;
  std::unique_ptr<GRSurface> wipe_data_confirmation_text_;
  std::unique_ptr<GRSurface> wipe_data_menu_header_text_;

  std::unique_ptr<GRSurface> fastbootd_logo_;

  // current_icon_ points to one of the frames in intro_frames_ or loop_frames_, indexed by
  // current_frame_, or error_icon_.
  Icon current_icon_;
  std::unique_ptr<GRSurface> error_icon_;
  std::vector<std::unique_ptr<GRSurface>> intro_frames_;
  std::vector<std::unique_ptr<GRSurface>> loop_frames_;
  size_t current_frame_;
  bool intro_done_;

  // progress_bar and stage_marker images.
  std::unique_ptr<GRSurface> progress_bar_empty_;
  std::unique_ptr<GRSurface> progress_bar_fill_;
  std::unique_ptr<GRSurface> stage_marker_empty_;
  std::unique_ptr<GRSurface> stage_marker_fill_;

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

  std::vector<std::string> title_lines_;

  bool scrollable_menu_;
  std::unique_ptr<Menu> menu_;

  // An alternate text screen, swapped with 'text_' when we're viewing a log file.
  char** file_viewer_text_;

  std::thread progress_thread_;
  std::atomic<bool> progress_thread_stopped_{ false };

  int stage, max_stage;

  int char_width_;
  int char_height_;

  // The locale that's used to show the rendered texts.
  std::string locale_;
  bool rtl_locale_;

  std::mutex updateMutex;

 private:
  void SetLocale(const std::string&);

  // Display the background texts for "erasing", "error", "no_command" and "installing" for the
  // selected locale.
  void SelectAndShowBackgroundText(const std::vector<std::string>& locales_entries, size_t sel);
};

#endif  // RECOVERY_UI_H
