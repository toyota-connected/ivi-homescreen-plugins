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

#include "exposure.h"

#include <core/include/literals.h>
#include <plugins/common/common.h>

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////
Exposure::Exposure(const flutter::EncodableMap& params) {
  SPDLOG_TRACE("++Exposure::Exposure");
  for (const auto& [fst, snd] : params) {
    if (auto key = std::get<std::string>(fst); key == kAperture) {
      if (std::holds_alternative<double>(snd)) {
        aperture_ = std::get<double>(snd);
      } else if (std::holds_alternative<std::monostate>(snd)) {
        aperture_ = 16.0f;
      }
    } else if (key == kSensitivity) {
      if (std::holds_alternative<double>(snd)) {
        sensitivity_ = std::get<double>(snd);
      } else if (std::holds_alternative<std::monostate>(snd)) {
        sensitivity_ = 100.0f;
      }
    } else if (key == kShutterSpeed) {
      if (std::holds_alternative<double>(snd)) {
        shutterSpeed_ = std::get<double>(snd);
      } else if (std::holds_alternative<std::monostate>(snd)) {
        shutterSpeed_ = 1.0f / 125.0f;
      }
    } else if (key == kExposure) {
      if (std::holds_alternative<double>(snd)) {
        exposure_ = std::get<double>(snd);
      }
    }
  }
  SPDLOG_TRACE("--Exposure::Exposure");
}

////////////////////////////////////////////////////////////////////////////
void Exposure::DebugPrint(const char* tag) {
  spdlog::debug("++++++++");
  spdlog::debug("{} (Exposure)", tag);
  if (aperture_.has_value()) {
    spdlog::debug("\taperture: {}", aperture_.value());
  }
  if (sensitivity_.has_value()) {
    spdlog::debug("\tsensitivity: {}", sensitivity_.value());
  }
  if (shutterSpeed_.has_value()) {
    spdlog::debug("\tshutterSpeed: {}", shutterSpeed_.value());
  }
  if (exposure_.has_value()) {
    spdlog::debug("\texposure: {}", exposure_.value());
  }
  spdlog::debug("++++++++");
}

}  // namespace plugin_filament_view