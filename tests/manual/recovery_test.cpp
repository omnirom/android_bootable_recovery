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
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>
#include <android/log.h>
#include <gtest/gtest.h>
#include <png.h>
#include <private/android_logger.h>

#include "minui/minui.h"

static const std::string myFilename = "/data/misc/recovery/inject.txt";
static const std::string myContent = "Hello World\nWelcome to my recovery\n";
static const std::string kLocale = "zu";
static const std::string kResourceTestDir = "/data/nativetest/recovery/";

// Failure is expected on systems that do not deliver either the
// recovery-persist or recovery-refresh executables. Tests also require
// a reboot sequence of test to truly verify.

static ssize_t __pmsg_fn(log_id_t logId, char prio, const char *filename,
                         const char *buf, size_t len, void *arg) {
  EXPECT_EQ(LOG_ID_SYSTEM, logId);
  EXPECT_EQ(ANDROID_LOG_INFO, prio);
  EXPECT_NE(std::string::npos, myFilename.find(filename));
  EXPECT_EQ(myContent, buf);
  EXPECT_EQ(myContent.size(), len);
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
    EXPECT_LT(0, __android_log_pmsg_file_write(LOG_ID_SYSTEM, ANDROID_LOG_INFO,
        myFilename.c_str(), myContent.c_str(), myContent.size()));

    fprintf(stderr, "injected test data, requires two intervening reboots "
        "to check for replication\n");
  }
  EXPECT_EQ(static_cast<ssize_t>(myContent.size()), ret);
}

// recovery.persist - Requires recovery.inject, then a reboot, then
//                    expect success after for this test on that boot.
TEST(recovery, persist) {
  EXPECT_EQ(0, access("/system/bin/recovery-persist", F_OK));

  ssize_t ret = __android_log_pmsg_file_read(
      LOG_ID_SYSTEM, ANDROID_LOG_INFO, "recovery/", __pmsg_fn, nullptr);
  if (ret == -ENOENT) {
    EXPECT_LT(0, __android_log_pmsg_file_write(LOG_ID_SYSTEM, ANDROID_LOG_INFO,
        myFilename.c_str(), myContent.c_str(), myContent.size()));

    fprintf(stderr, "injected test data, requires intervening reboot "
        "to check for storage\n");
  }

  std::string buf;
  EXPECT_TRUE(android::base::ReadFileToString(myFilename, &buf));
  EXPECT_EQ(myContent, buf);
  if (access(myFilename.c_str(), F_OK) == 0) {
    fprintf(stderr, "Removing persistent test data, "
        "check if reconstructed on reboot\n");
  }
  EXPECT_EQ(0, unlink(myFilename.c_str()));
}

std::vector<std::string> image_dir {
  "res-mdpi/images/",
  "res-hdpi/images/",
  "res-xhdpi/images/",
  "res-xxhdpi/images/",
  "res-xxxhdpi/images/"
};

static int png_filter(const dirent* de) {
  if (de->d_type != DT_REG || !android::base::EndsWith(de->d_name, "_text.png")) {
    return 0;
  }
  return 1;
}

// Find out all png files to test under /data/nativetest/recovery/.
static std::vector<std::string> add_files() {
  std::vector<std::string> files;
  for (const std::string& str : image_dir) {
    std::string dir_path = kResourceTestDir + str;
    dirent** namelist;
    int n = scandir(dir_path.c_str(), &namelist, png_filter, alphasort);
    if (n == -1) {
      printf("Failed to scan dir %s: %s\n", kResourceTestDir.c_str(), strerror(errno));
      return files;
    }
    if (n == 0) {
      printf("No file is added for test in %s\n", kResourceTestDir.c_str());
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

class ResourceTest : public testing::TestWithParam<std::string> {
 public:
  static std::vector<std::string> png_list;

  // Parse a png file and test if it's qualified for the background text image
  // under recovery.
  void SetUp() override {
    std::string file_path = GetParam();
    fp = fopen(file_path.c_str(), "rb");
    ASSERT_NE(nullptr, fp);

    unsigned char header[8];
    size_t bytesRead = fread(header, 1, sizeof(header), fp);
    ASSERT_EQ(sizeof(header), bytesRead);
    ASSERT_EQ(0, png_sig_cmp(header, 0, sizeof(header)));

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    ASSERT_NE(nullptr, png_ptr);

    info_ptr = png_create_info_struct(png_ptr);
    ASSERT_NE(nullptr, info_ptr);

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, sizeof(header));
    png_read_info(png_ptr, info_ptr);

    int color_type, bit_depth;
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, nullptr, nullptr,
                 nullptr);
    ASSERT_EQ(PNG_COLOR_TYPE_GRAY, color_type) << "Recovery expects grayscale PNG file.";
    ASSERT_LT(static_cast<png_uint_32>(5), width);
    ASSERT_LT(static_cast<png_uint_32>(0), height);
    if (bit_depth <= 8) {
      // 1-, 2-, 4-, or 8-bit gray images: expand to 8-bit gray.
      png_set_expand_gray_1_2_4_to_8(png_ptr);
    }

    png_byte channels = png_get_channels(png_ptr, info_ptr);
    ASSERT_EQ(1, channels) << "Recovery background text images expects 1-channel PNG file.";
  }

  void TearDown() override {
    if (png_ptr != nullptr && info_ptr != nullptr) {
      png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    }

    if (fp != nullptr) {
      fclose(fp);
    }
  }

 protected:
  png_structp png_ptr;
  png_infop info_ptr;
  png_uint_32 width, height;

  FILE* fp;
};

std::vector<std::string> ResourceTest::png_list = add_files();

TEST_P(ResourceTest, ValidateLocale) {
  std::vector<unsigned char> row(width);
  for (png_uint_32 y = 0; y < height; ++y) {
    png_read_row(png_ptr, row.data(), nullptr);
    int w = (row[1] << 8) | row[0];
    int h = (row[3] << 8) | row[2];
    int len = row[4];
    EXPECT_LT(0, w);
    EXPECT_LT(0, h);
    EXPECT_LT(0, len) << "Locale string should be non-empty.";
    EXPECT_NE(0, row[5]) << "Locale string is missing.";

    ASSERT_GT(height, y + 1 + h) << "Locale: " << kLocale << " is not found in the file.";
    char* loc = reinterpret_cast<char*>(&row[5]);
    if (matches_locale(loc, kLocale.c_str())) {
      EXPECT_TRUE(android::base::StartsWith(loc, kLocale.c_str()));
      break;
    } else {
      for (int i = 0; i < h; ++i, ++y) {
        png_read_row(png_ptr, row.data(), nullptr);
      }
    }
  }
}

INSTANTIATE_TEST_CASE_P(BackgroundTextValidation, ResourceTest,
                        ::testing::ValuesIn(ResourceTest::png_list.cbegin(),
                                            ResourceTest::png_list.cend()));
