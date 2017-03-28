/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef OTAUTIL_THERMALUTIL_H
#define OTAUTIL_THERMALUTIL_H

// We can find the temperature reported by all sensors in /sys/class/thermal/thermal_zone*/temp.
// Their values are in millidegree Celsius; and we will log the maximum one.
int GetMaxValueFromThermalZone();

#endif  // OTAUTIL_THERMALUTIL_H
