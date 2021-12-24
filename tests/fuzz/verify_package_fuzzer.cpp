/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "fuzzer/FuzzedDataProvider.h"

#include "install/install.h"
#include "otautil/package.h"
#include "recovery_ui/stub_ui.h"

std::unique_ptr<Package> CreatePackage(std::vector<uint8_t>& content) {
  return Package::CreateMemoryPackage(content, [](float) -> void {});
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider data_provider(data, size);
  auto package_contents = data_provider.ConsumeRemainingBytes<uint8_t>();
  if (package_contents.size() == 0) {
    return 0;
  }
  auto package = CreatePackage(package_contents);
  StubRecoveryUI ui;
  verify_package(package.get(), &ui);
  return 0;
}
