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

#ifndef _DATA_HEADER
#define _DATA_HEADER

int DataManager_ResetDefaults();
void DataManager_LoadDefaults();
int DataManager_LoadValues(const char* filename);
int DataManager_Flush();
const char* DataManager_GetStrValue(const char* varName);
const char* DataManager_GetCurrentStoragePath();
const char* DataManager_GetCurrentStorageMount();
const char* DataManager_GetSettingsStoragePath();
const char* DataManager_GetSettingsStorageMount();
int DataManager_GetIntValue(const char* varName);

int DataManager_SetStrValue(const char* varName, char* value);
int DataManager_SetIntValue(const char* varName, int value);
int DataManager_SetFloatValue(const char* varName, float value);

int DataManager_ToggleIntValue(const char* varName);

void DataManager_DumpValues();
void DataManager_ReadSettingsFile();

#endif  // _DATA_HEADER

