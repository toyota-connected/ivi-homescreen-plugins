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

#include "lens_projection.h"

#include <plugins/common/common.h>

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////
LensProjection::LensProjection(const float cameraFocalLength, float aspect)
    : focalLength_(cameraFocalLength),
      aspect_(aspect),
      near_(0.0f),
      far_(0.0f) {}

////////////////////////////////////////////////////////////////////////////
LensProjection::LensProjection(const flutter::EncodableMap& params) {
  SPDLOG_TRACE("++LensProjection::LensProjection");
  for (const auto& [fst, snd] : params) {
    auto key = std::get<std::string>(fst);
    if (key == "focalLength") {
      if (std::holds_alternative<double>(snd)) {
        focalLength_ = static_cast<float>(std::get<double>(snd));
      } else if (std::holds_alternative<std::monostate>(snd)) {
        focalLength_ = 28.0f;
      }
    } else if (key == "aspect") {
      if (std::holds_alternative<double>(snd)) {
        aspect_ = std::get<double>(snd);
      }
    } else if (key == "near") {
      if (std::holds_alternative<double>(snd)) {
        near_ = std::get<double>(snd);
      } else if (std::holds_alternative<std::monostate>(snd)) {
        near_ = 0.05;  // 5 cm
      }
    } else if (key == "far") {
      if (std::holds_alternative<double>(snd)) {
        far_ = std::get<double>(snd);
      } else if (std::holds_alternative<std::monostate>(snd)) {
        far_ = 1000.0;  // 1 km
      }
    } else if (!snd.IsNull()) {
      spdlog::debug("[LensProjection] Unhandled Parameter");
      plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(), snd);
    }
  }
  SPDLOG_TRACE("--LensProjection::LensProjection");
  DebugPrint("LensProjection");
}

////////////////////////////////////////////////////////////////////////////
void LensProjection::DebugPrint(const char* tag) {
  spdlog::debug("++++++++");
  spdlog::debug("{} (LensProjection)", tag);
  spdlog::debug("\tfocalLength: {}", focalLength_);

  if (aspect_.has_value()) {
    spdlog::debug("\taspect: {}", aspect_.value());
  }
  if (near_.has_value()) {
    spdlog::debug("\tnear: {}", near_.value());
  }
  if (far_.has_value()) {
    spdlog::debug("\tfar: {}", far_.value());
  }
  spdlog::debug("++++++++");
}

}  // namespace plugin_filament_view
