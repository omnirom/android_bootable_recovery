/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include "private/resources.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <limits>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include <android-base/strings.h>
#include <png.h>

#include "minui/minui.h"

static std::string g_resource_dir{ "/res/images" };

std::unique_ptr<GRSurface> GRSurface::Create(size_t width, size_t height, size_t row_bytes,
                                             size_t pixel_bytes) {
  if (width == 0 || row_bytes == 0 || height == 0 || pixel_bytes == 0) return nullptr;
  if (std::numeric_limits<size_t>::max() / row_bytes < height) return nullptr;

  // Cannot use std::make_unique to access non-public ctor.
  auto result = std::unique_ptr<GRSurface>(new GRSurface(width, height, row_bytes, pixel_bytes));
  size_t data_size = row_bytes * height;
  result->data_size_ =
      (data_size + kSurfaceDataAlignment - 1) / kSurfaceDataAlignment * kSurfaceDataAlignment;
  result->data_.reset(
      static_cast<uint8_t*>(aligned_alloc(kSurfaceDataAlignment, result->data_size_)));
  if (!result->data_) return nullptr;
  return result;
}

std::unique_ptr<GRSurface> GRSurface::Clone() const {
  auto result = GRSurface::Create(width, height, row_bytes, pixel_bytes);
  if (!result) return nullptr;
  memcpy(result->data(), data(), data_size_);
  return result;
}

PngHandler::PngHandler(const std::string& name) {
  std::string res_path = g_resource_dir + "/" + name + ".png";
  png_fp_.reset(fopen(res_path.c_str(), "rbe"));
  // Try to read from |name| if the resource path does not work.
  if (!png_fp_) {
    png_fp_.reset(fopen(name.c_str(), "rbe"));
  }
  if (!png_fp_) {
    error_code_ = -1;
    return;
  }

  uint8_t header[8];
  size_t bytesRead = fread(header, 1, sizeof(header), png_fp_.get());
  if (bytesRead != sizeof(header)) {
    error_code_ = -2;
    return;
  }

  if (png_sig_cmp(header, 0, sizeof(header))) {
    error_code_ = -3;
    return;
  }

  png_ptr_ = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png_ptr_) {
    error_code_ = -4;
    return;
  }

  info_ptr_ = png_create_info_struct(png_ptr_);
  if (!info_ptr_) {
    error_code_ = -5;
    return;
  }

  if (setjmp(png_jmpbuf(png_ptr_))) {
    error_code_ = -6;
    return;
  }

  png_init_io(png_ptr_, png_fp_.get());
  png_set_sig_bytes(png_ptr_, sizeof(header));
  png_read_info(png_ptr_, info_ptr_);

  png_get_IHDR(png_ptr_, info_ptr_, &width_, &height_, &bit_depth_, &color_type_, nullptr, nullptr,
               nullptr);

  channels_ = png_get_channels(png_ptr_, info_ptr_);

  if (bit_depth_ == 8 && channels_ == 3 && color_type_ == PNG_COLOR_TYPE_RGB) {
    // 8-bit RGB images: great, nothing to do.
  } else if (bit_depth_ <= 8 && channels_ == 1 && color_type_ == PNG_COLOR_TYPE_GRAY) {
    // 1-, 2-, 4-, or 8-bit gray images: expand to 8-bit gray.
    png_set_expand_gray_1_2_4_to_8(png_ptr_);
  } else if (bit_depth_ <= 8 && channels_ == 1 && color_type_ == PNG_COLOR_TYPE_PALETTE) {
    // paletted images: expand to 8-bit RGB.  Note that we DON'T
    // currently expand the tRNS chunk (if any) to an alpha
    // channel, because minui doesn't support alpha channels in
    // general.
    png_set_palette_to_rgb(png_ptr_);
    channels_ = 3;
  } else {
    fprintf(stderr, "minui doesn't support PNG depth %d channels %d color_type %d\n", bit_depth_,
            channels_, color_type_);
    error_code_ = -7;
  }
}

PngHandler::~PngHandler() {
  if (png_ptr_) {
    png_destroy_read_struct(&png_ptr_, &info_ptr_, nullptr);
  }
}

// "display" surfaces are transformed into the framebuffer's required pixel format (currently only
// RGBX is supported) at load time, so gr_blit() can be nothing more than a memcpy() for each row.

// Copies 'input_row' to 'output_row', transforming it to the framebuffer pixel format. The input
// format depends on the value of 'channels':
//
//   1 - input is 8-bit grayscale
//   3 - input is 24-bit RGB
//   4 - input is 32-bit RGBA/RGBX
//
// 'width' is the number of pixels in the row.
static void TransformRgbToDraw(const uint8_t* input_row, uint8_t* output_row, int channels,
                               int width) {
  const uint8_t* ip = input_row;
  uint8_t* op = output_row;

  switch (channels) {
    case 1:
      // expand gray level to RGBX
      for (int x = 0; x < width; ++x) {
        *op++ = *ip;
        *op++ = *ip;
        *op++ = *ip;
        *op++ = 0xff;
        ip++;
      }
      break;

    case 3:
      // expand RGBA to RGBX
      for (int x = 0; x < width; ++x) {
        *op++ = *ip++;
        *op++ = *ip++;
        *op++ = *ip++;
        *op++ = 0xff;
      }
      break;

    case 4:
      // copy RGBA to RGBX
      memcpy(output_row, input_row, width * 4);
      break;
  }
}

int res_create_display_surface(const char* name, GRSurface** pSurface) {
  *pSurface = nullptr;

  PngHandler png_handler(name);
  if (!png_handler) return png_handler.error_code();

  png_structp png_ptr = png_handler.png_ptr();
  png_uint_32 width = png_handler.width();
  png_uint_32 height = png_handler.height();

  auto surface = GRSurface::Create(width, height, width * 4, 4);
  if (!surface) {
    return -8;
  }

  PixelFormat pixel_format = gr_pixel_format();
  if (pixel_format == PixelFormat::ARGB || pixel_format == PixelFormat::BGRA) {
    png_set_bgr(png_ptr);
  }

  for (png_uint_32 y = 0; y < height; ++y) {
    std::vector<uint8_t> p_row(width * 4);
    png_read_row(png_ptr, p_row.data(), nullptr);
    TransformRgbToDraw(p_row.data(), surface->data() + y * surface->row_bytes,
                       png_handler.channels(), width);
  }

  *pSurface = surface.release();

  return 0;
}

int res_create_multi_display_surface(const char* name, int* frames, int* fps,
                                     GRSurface*** pSurface) {
  *pSurface = nullptr;
  *frames = -1;

  PngHandler png_handler(name);
  if (!png_handler) return png_handler.error_code();

  png_structp png_ptr = png_handler.png_ptr();
  png_uint_32 width = png_handler.width();
  png_uint_32 height = png_handler.height();

  *frames = 1;
  *fps = 20;
  png_textp text;
  int num_text;
  if (png_get_text(png_ptr, png_handler.info_ptr(), &text, &num_text)) {
    for (int i = 0; i < num_text; ++i) {
      if (text[i].key && strcmp(text[i].key, "Frames") == 0 && text[i].text) {
        *frames = atoi(text[i].text);
      } else if (text[i].key && strcmp(text[i].key, "FPS") == 0 && text[i].text) {
        *fps = atoi(text[i].text);
      }
    }
    printf("  found frames = %d\n", *frames);
    printf("  found fps = %d\n", *fps);
  }

  int result = 0;
  GRSurface** surface = nullptr;
  if (*frames <= 0 || *fps <= 0) {
    printf("bad number of frames (%d) and/or FPS (%d)\n", *frames, *fps);
    result = -10;
    goto exit;
  }

  if (height % *frames != 0) {
    printf("bad height (%d) for frame count (%d)\n", height, *frames);
    result = -9;
    goto exit;
  }

  surface = static_cast<GRSurface**>(calloc(*frames, sizeof(GRSurface*)));
  if (!surface) {
    result = -8;
    goto exit;
  }
  for (int i = 0; i < *frames; ++i) {
    auto created_surface = GRSurface::Create(width, height / *frames, width * 4, 4);
    if (!created_surface) {
      result = -8;
      goto exit;
    }
    surface[i] = created_surface.release();
  }

  if (gr_pixel_format() == PixelFormat::ARGB || gr_pixel_format() == PixelFormat::BGRA) {
    png_set_bgr(png_ptr);
  }

  for (png_uint_32 y = 0; y < height; ++y) {
    std::vector<uint8_t> p_row(width * 4);
    png_read_row(png_ptr, p_row.data(), nullptr);
    int frame = y % *frames;
    uint8_t* out_row = surface[frame]->data() + (y / *frames) * surface[frame]->row_bytes;
    TransformRgbToDraw(p_row.data(), out_row, png_handler.channels(), width);
  }

  *pSurface = surface;

exit:
  if (result < 0) {
    if (surface) {
      for (int i = 0; i < *frames; ++i) {
        free(surface[i]);
      }
      free(surface);
    }
  }
  return result;
}

int res_create_alpha_surface(const char* name, GRSurface** pSurface) {
  *pSurface = nullptr;

  PngHandler png_handler(name);
  if (!png_handler) return png_handler.error_code();

  if (png_handler.channels() != 1) {
    return -7;
  }

  png_structp png_ptr = png_handler.png_ptr();
  png_uint_32 width = png_handler.width();
  png_uint_32 height = png_handler.height();

  auto surface = GRSurface::Create(width, height, width, 1);
  if (!surface) {
    return -8;
  }

  PixelFormat pixel_format = gr_pixel_format();
  if (pixel_format == PixelFormat::ARGB || pixel_format == PixelFormat::BGRA) {
    png_set_bgr(png_ptr);
  }

  for (png_uint_32 y = 0; y < height; ++y) {
    uint8_t* p_row = surface->data() + y * surface->row_bytes;
    png_read_row(png_ptr, p_row, nullptr);
  }

  *pSurface = surface.release();

  return 0;
}

void res_set_resource_dir(const std::string& dirname) {
  g_resource_dir = dirname;
}

// This function tests if a locale string stored in PNG (prefix) matches
// the locale string provided by the system (locale).
bool matches_locale(const std::string& prefix, const std::string& locale) {
  // According to the BCP 47 format, A locale string may consists of:
  // language-{extlang}-{script}-{region}-{variant}
  // The locale headers in PNG mostly consist of language-{region} except for sr-Latn, and some
  // android's system locale can have the format language-{script}-{region}.

  // Return true if the whole string of prefix matches the top part of locale. Otherwise try to
  // match the locale string without the {script} section.
  // For instance, prefix == "en" matches locale == "en-US", prefix == "sr-Latn" matches locale
  // == "sr-Latn-BA", and prefix == "zh-CN" matches locale == "zh-Hans-CN".
  if (prefix.empty()) {
    return false;
  }

  if (android::base::StartsWith(locale, prefix)) {
    return true;
  }

  size_t separator = prefix.find('-');
  if (separator == std::string::npos) {
    return false;
  }
  std::regex loc_regex(prefix.substr(0, separator) + "-[A-Za-z]*" + prefix.substr(separator));
  return std::regex_match(locale, loc_regex);
}

std::vector<std::string> get_locales_in_png(const std::string& png_name) {
  PngHandler png_handler(png_name);
  if (!png_handler) {
    printf("Failed to open %s, error: %d\n", png_name.c_str(), png_handler.error_code());
    return {};
  }
  if (png_handler.channels() != 1) {
    printf("Expect input png to have 1 data channel, this file has %d\n", png_handler.channels());
    return {};
  }

  std::vector<std::string> result;
  std::vector<uint8_t> row(png_handler.width());
  for (png_uint_32 y = 0; y < png_handler.height(); ++y) {
    png_read_row(png_handler.png_ptr(), row.data(), nullptr);
    int h = (row[3] << 8) | row[2];
    std::string loc(reinterpret_cast<char*>(&row[5]));
    if (!loc.empty()) {
      result.push_back(loc);
    }
    for (int i = 0; i < h; ++i, ++y) {
      png_read_row(png_handler.png_ptr(), row.data(), nullptr);
    }
  }

  return result;
}

int res_create_localized_alpha_surface(const char* name,
                                       const char* locale,
                                       GRSurface** pSurface) {
  *pSurface = nullptr;
  if (locale == nullptr) {
    return 0;
  }

  PngHandler png_handler(name);
  if (!png_handler) return png_handler.error_code();

  if (png_handler.channels() != 1) {
    return -7;
  }

  png_structp png_ptr = png_handler.png_ptr();
  png_uint_32 width = png_handler.width();
  png_uint_32 height = png_handler.height();

  for (png_uint_32 y = 0; y < height; ++y) {
    std::vector<uint8_t> row(width);
    png_read_row(png_ptr, row.data(), nullptr);
    int w = (row[1] << 8) | row[0];
    int h = (row[3] << 8) | row[2];
    __unused int len = row[4];
    char* loc = reinterpret_cast<char*>(&row[5]);

    // We need to include one additional line for the metadata of the localized image.
    if (y + 1 + h > height) {
      printf("Read exceeds the image boundary, y %u, h %d, height %u\n", y, h, height);
      return -8;
    }

    if (matches_locale(loc, locale)) {
      printf("  %20s: %s (%d x %d @ %d)\n", name, loc, w, h, y);

      auto surface = GRSurface::Create(w, h, w, 1);
      if (!surface) {
        return -9;
      }

      for (int i = 0; i < h; ++i, ++y) {
        png_read_row(png_ptr, row.data(), nullptr);
        memcpy(surface->data() + i * w, row.data(), w);
      }

      *pSurface = surface.release();
      return 0;
    }

    for (int i = 0; i < h; ++i, ++y) {
      png_read_row(png_ptr, row.data(), nullptr);
    }
  }

  return -10;
}

void res_free_surface(GRSurface* surface) {
  free(surface);
}
