/*
 * Copyright 2020-2024 Toyota Connected North America
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

#include <string>
#include <string_view>

#include "backend_interface.h"

namespace video_player_linux {

// Look up the SoC this process is running on and map it to one of the
// PlatformProfile values defined in backend_interface.h. The probe
// order is:
//
//   1. /sys/firmware/devicetree/base/compatible — primary source on
//      ARM systems with a device tree. Compatible strings are
//      null-separated; we match any substring against a prefix list.
//   2. Vendor-specific sysfs probes for systems with a thin DT or
//      ACPI (Qualcomm's /sys/class/soc, MediaTek's vcodec nodes).
//   3. Any V4L2 codec device under /dev/video* → GenericV4L2.
//   4. Otherwise Software.
//
// Safe to call from any thread; result is stable for the process
// lifetime (platform doesn't change at runtime).
PlatformProfile DetectPlatform();

// Short string name matching the TOML config-file keys, e.g.
// PlatformProfile::Imx8MPlus → "imx8m_plus". Lowercase snake_case
// throughout. Returns "auto" for PlatformProfile::Auto, "unknown"
// for anything outside the enum.
std::string PlatformProfileName(PlatformProfile p);

// Inverse of PlatformProfileName. Unknown names map to
// PlatformProfile::Auto so config-file typos don't silently select
// the wrong platform.
PlatformProfile PlatformProfileFromName(std::string_view name);

}  // namespace video_player_linux
