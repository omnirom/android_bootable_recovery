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

#pragma once

#include <stdio.h>

#include <memory>
#include <string>

#include <png.h>

// This class handles the PNG file parsing. It also holds the ownership of the PNG pointer and the
// opened file pointer. Both will be destroyed / closed when this object goes out of scope.
class PngHandler {
 public:
  // Constructs an instance by loading the PNG file from '/res/images/<name>.png', or '<name>'.
  PngHandler(const std::string& name);

  ~PngHandler();

  png_uint_32 width() const {
    return width_;
  }

  png_uint_32 height() const {
    return height_;
  }

  png_byte channels() const {
    return channels_;
  }

  int bit_depth() const {
    return bit_depth_;
  }

  int color_type() const {
    return color_type_;
  }

  png_structp png_ptr() const {
    return png_ptr_;
  }

  png_infop info_ptr() const {
    return info_ptr_;
  }

  int error_code() const {
    return error_code_;
  };

  operator bool() const {
    return error_code_ == 0;
  }

 private:
  png_structp png_ptr_{ nullptr };
  png_infop info_ptr_{ nullptr };
  png_uint_32 width_;
  png_uint_32 height_;
  png_byte channels_;
  int bit_depth_;
  int color_type_;

  // The |error_code_| is set to a negative value if an error occurs when opening the png file.
  int error_code_{ 0 };
  // After initialization, we'll keep the file pointer open before destruction of PngHandler.
  std::unique_ptr<FILE, decltype(&fclose)> png_fp_{ nullptr, fclose };
};

// Overrides the default resource dir, for testing purpose.
void res_set_resource_dir(const std::string&);
