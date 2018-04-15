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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>
#include <android/log.h>
#include <gtest/gtest.h>
#include <private/android_logger.h>

#include "minui/minui.h"
#include "private/resources.h"

static const std::string kInjectTxtFilename = "/data/misc/recovery/inject.txt";
static const std::string kInjectTxtContent = "Hello World\nWelcome to my recovery\n";
static const std::string kLocale = "zu";

// Failure is expected on systems that do not deliver either the
// recovery-persist or recovery-refresh executables. Tests also require
// a reboot sequence of test to truly verify.

static ssize_t __pmsg_fn(log_id_t logId, char prio, const char *filename,
                         const char *buf, size_t len, void *arg) {
  EXPECT_EQ(LOG_ID_SYSTEM, logId);
  EXPECT_EQ(ANDROID_LOG_INFO, prio);
  EXPECT_NE(std::string::npos, kInjectTxtFilename.find(filename));
  EXPECT_EQ(kInjectTxtContent, buf);
  EXPECT_EQ(kInjectTxtContent.size(), len);
  EXPECT_EQ(nullptr, arg);
  return len;
}

// recovery.refresh - May fail. Requires recovery.inject, two reboots,
//                    then expect success after second reboot.
TEST(recovery, refresh) {
  EXPECT_EQ(0, access("/system/bin/recovery-refresh", F_OK));

  ssize_t ret = __android_log_pmsg_file_read(
      LOG_ID_SYSTEM, ANDROID_LOG_INFO, "recovery/", __pmsg_fn, nullptr);
  if (ret == -ENOENT) {
    EXPECT_LT(0, __android_log_pmsg_file_write(
                     LOG_ID_SYSTEM, ANDROID_LOG_INFO, kInjectTxtFilename.c_str(),
                     kInjectTxtContent.c_str(), kInjectTxtContent.size()));

    fprintf(stderr,
            "injected test data, requires two intervening reboots to check for replication\n");
  }
  EXPECT_EQ(static_cast<ssize_t>(kInjectTxtContent.size()), ret);
}

// recovery.persist - Requires recovery.inject, then a reboot, then
//                    expect success after for this test on that boot.
TEST(recovery, persist) {
  EXPECT_EQ(0, access("/system/bin/recovery-persist", F_OK));

  ssize_t ret = __android_log_pmsg_file_read(
      LOG_ID_SYSTEM, ANDROID_LOG_INFO, "recovery/", __pmsg_fn, nullptr);
  if (ret == -ENOENT) {
    EXPECT_LT(0, __android_log_pmsg_file_write(
                     LOG_ID_SYSTEM, ANDROID_LOG_INFO, kInjectTxtFilename.c_str(),
                     kInjectTxtContent.c_str(), kInjectTxtContent.size()));

    fprintf(stderr, "injected test data, requires intervening reboot to check for storage\n");
  }

  std::string buf;
  EXPECT_TRUE(android::base::ReadFileToString(kInjectTxtFilename, &buf));
  EXPECT_EQ(kInjectTxtContent, buf);
  if (access(kInjectTxtFilename.c_str(), F_OK) == 0) {
    fprintf(stderr, "Removing persistent test data, check if reconstructed on reboot\n");
  }
  EXPECT_EQ(0, unlink(kInjectTxtFilename.c_str()));
}

static const std::vector<std::string> kResourceImagesDirs{ "res-mdpi/images/", "res-hdpi/images/",
                                                           "res-xhdpi/images/",
                                                           "res-xxhdpi/images/",
                                                           "res-xxxhdpi/images/" };

static int png_filter(const dirent* de) {
  if (de->d_type != DT_REG || !android::base::EndsWith(de->d_name, "_text.png")) {
    return 0;
  }
  return 1;
}

// Finds out all the PNG files to test, which stay under the same dir with the executable.
static std::vector<std::string> add_files() {
  std::string exec_dir = android::base::GetExecutableDirectory();
  std::vector<std::string> files;
  for (const std::string& images_dir : kResourceImagesDirs) {
    std::string dir_path = exec_dir + "/" + images_dir;
    dirent** namelist;
    int n = scandir(dir_path.c_str(), &namelist, png_filter, alphasort);
    if (n == -1) {
      printf("Failed to scan dir %s: %s\n", exec_dir.c_str(), strerror(errno));
      return files;
    }
    if (n == 0) {
      printf("No file is added for test in %s\n", exec_dir.c_str());
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

    ASSERT_GT(png_->height(), y + 1 + h) << "Locale: " << kLocale << " is not found in the file.";
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
