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

#ifndef _ERROR_CODE_H_
#define _ERROR_CODE_H_

enum ErrorCode {
  kNoError = -1,
  kLowBattery = 20,
  kZipVerificationFailure,
  kZipOpenFailure,
  kBootreasonInBlacklist,
  kPackageCompatibilityFailure,
};

enum CauseCode {
  kNoCause = -1,
  kArgsParsingFailure = 100,
  kStashCreationFailure,
  kFileOpenFailure,
  kLseekFailure,
  kFreadFailure,
  kFwriteFailure,
  kFsyncFailure,
  kLibfecFailure,
  kFileGetPropFailure,
  kFileRenameFailure,
  kSymlinkFailure,
  kSetMetadataFailure,
  kTune2FsFailure,
  kRebootFailure,
  kPackageExtractFileFailure,
  kVendorFailure = 200
};

enum UncryptErrorCode {
  kUncryptNoError = -1,
  kUncryptErrorPlaceholder = 50,
  kUncryptTimeoutError = 100,
  kUncryptFileRemoveError,
  kUncryptFileOpenError,
  kUncryptSocketOpenError,
  kUncryptSocketWriteError,
  kUncryptSocketListenError,
  kUncryptSocketAcceptError,
  kUncryptFstabReadError,
  kUncryptFileStatError,
  kUncryptBlockOpenError,
  kUncryptIoctlError,
  kUncryptReadError,
  kUncryptWriteError,
  kUncryptFileSyncError,
  kUncryptFileCloseError,
  kUncryptFileRenameError,
  kUncryptPackageMissingError,
};

#endif // _ERROR_CODE_H_
