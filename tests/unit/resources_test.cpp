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

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

#include <memory>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>
#include <gtest/gtest.h>
#include <png.h>

#include "common/test_constants.h"
#include "minui/minui.h"
#include "private/resources.h"

static const std::string kLocale = "zu";

static const std::vector<std::string> kResourceImagesDirs{
  "res-mdpi/images/",   "res-hdpi/images/",    "res-xhdpi/images/",
  "res-xxhdpi/images/", "res-xxxhdpi/images/",
};

static int png_filter(const dirent* de) {
  if (de->d_type != DT_REG || !android::base::EndsWith(de->d_name, "_text.png")) {
    return 0;
  }
  return 1;
}

// Finds out all the PNG files to test, which stay under the same dir with the executabl..
static std::vector<std::string> add_files() {
  std::vector<std::string> files;
  for (const std::string& images_dir : kResourceImagesDirs) {
    static std::string exec_dir = android::base::GetExecutableDirectory();
    std::string dir_path = exec_dir + "/" + images_dir;
    dirent** namelist;
    int n = scandir(dir_path.c_str(), &namelist, png_filter, alphasort);
    if (n == -1) {
      printf("Failed to scandir %s: %s\n", dir_path.c_str(), strerror(errno));
      continue;
    }
    if (n == 0) {
      printf("No file is added for test in %s\n", dir_path.c_str());
    }

    while (n--) {
      std::string file_path = dir_path + namelist[n]->d_name;
      files.push_back(file_path);
      free(namelist[n]);
    }
    free(namelist);
  }
  return files;
}

TEST(ResourcesTest, res_create_multi_display_surface) {
  GRSurface** frames;
  int frame_count;
  int fps;
  ASSERT_EQ(0, res_create_multi_display_surface(from_testdata_base("battery_scale.png").c_str(),
                                                &frame_count, &fps, &frames));
  ASSERT_EQ(6, frame_count);
  ASSERT_EQ(20, fps);

  for (auto i = 0; i < frame_count; i++) {
    free(frames[i]);
  }
  free(frames);
}

class ResourcesTest : public testing::TestWithParam<std::string> {
 public:
  static std::vector<std::string> png_list;

 protected:
  void SetUp() override {
    png_ = std::make_unique<PngHandler>(GetParam());
    ASSERT_TRUE(png_);

    ASSERT_EQ(PNG_COLOR_TYPE_GRAY, png_->color_type()) << "Recovery expects grayscale PNG file.";
    ASSERT_LT(static_cast<png_uint_32>(5), png_->width());
    ASSERT_LT(static_cast<png_uint_32>(0), png_->height());
    ASSERT_EQ(1, png_->channels()) << "Recovery background text images expects 1-channel PNG file.";
  }

  std::unique_ptr<PngHandler> png_{ nullptr };
};

// Parses a png file and tests if it's qualified for the background text image under recovery.
TEST_P(ResourcesTest, ValidateLocale) {
  std::vector<unsigned char> row(png_->width());
  for (png_uint_32 y = 0; y < png_->height(); ++y) {
    png_read_row(png_->png_ptr(), row.data(), nullptr);
    int w = (row[1] << 8) | row[0];
    int h = (row[3] << 8) | row[2];
    int len = row[4];
    EXPECT_LT(0, w);
    EXPECT_LT(0, h);
    EXPECT_LT(0, len) << "Locale string should be non-empty.";
    EXPECT_NE(0, row[5]) << "Locale string is missing.";

    ASSERT_GE(png_->height(), y + 1 + h) << "Locale: " << kLocale << " is not found in the file.";
    char* loc = reinterpret_cast<char*>(&row[5]);
    if (matches_locale(loc, kLocale.c_str())) {
      EXPECT_TRUE(android::base::StartsWith(loc, kLocale));
      break;
    }
    for (int i = 0; i < h; ++i, ++y) {
      png_read_row(png_->png_ptr(), row.data(), nullptr);
    }
  }
}

std::vector<std::string> ResourcesTest::png_list = add_files();

INSTANTIATE_TEST_CASE_P(BackgroundTextValidation, ResourcesTest,
                        ::testing::ValuesIn(ResourcesTest::png_list.cbegin(),
                                            ResourcesTest::png_list.cend()));
