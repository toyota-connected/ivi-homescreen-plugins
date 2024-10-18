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

#include "projection.h"

#include <core/include/literals.h>
#include <plugins/common/common.h>

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////
Projection::Projection(const flutter::EncodableMap& params) {
  SPDLOG_TRACE("++Projection::Projection");
  for (const auto& [fst, snd] : params) {
    if (auto key = std::get<std::string>(fst); key == kProjection) {
      if (std::holds_alternative<std::string>(snd)) {
        projection_ = getTypeForText(std::get<std::string>(snd));
      } else if (std::holds_alternative<std::monostate>(snd)) {
        projection_ = filament::Camera::Projection::ORTHO;
      }
    } else if (key == kLeft) {
      if (std::holds_alternative<double>(snd)) {
        left_ = std::get<double>(snd);
      }
    } else if (key == kRight) {
      if (std::holds_alternative<double>(snd)) {
        right_ = std::get<double>(snd);
      }
    } else if (key == kBottom) {
      if (std::holds_alternative<double>(snd)) {
        bottom_ = std::get<double>(snd);
      }
    } else if (key == kTop) {
      if (std::holds_alternative<double>(snd)) {
        top_ = std::get<double>(snd);
      }
    } else if (key == kNear) {
      if (std::holds_alternative<double>(snd)) {
        near_ = std::get<double>(snd);
      }
    } else if (key == kFar) {
      if (std::holds_alternative<double>(snd)) {
        far_ = std::get<double>(snd);
      }
    } else if (key == kFovInDegrees) {
      if (std::holds_alternative<double>(snd)) {
        fovInDegrees_ = std::get<double>(snd);
      }
    } else if (key == kAspect) {
      if (std::holds_alternative<double>(snd)) {
        aspect_ = std::get<double>(snd);
      }
    } else if (key == kDirection) {
      if (std::holds_alternative<std::string>(snd)) {
        fovDirection_ = getFovForText(std::get<std::string>(snd));
      } else if (std::holds_alternative<std::monostate>(snd)) {
        fovDirection_ = filament::Camera::Fov::HORIZONTAL;
      }
    }
  }
  SPDLOG_TRACE("--Projection::Projection");
}

////////////////////////////////////////////////////////////////////////////
void Projection::DebugPrint(const char* tag) {
  spdlog::debug("++++++++");
  spdlog::debug("{} (Projection)", tag);
  if (projection_.has_value()) {
    spdlog::debug("projection: {}", getTextForType(projection_.value()));
  }
  if (left_.has_value()) {
    spdlog::debug("left: {}", left_.value());
  }
  if (right_.has_value()) {
    spdlog::debug("right: {}", right_.value());
  }
  if (bottom_.has_value()) {
    spdlog::debug("bottom: {}", bottom_.value());
  }
  if (top_.has_value()) {
    spdlog::debug("top: {}", top_.value());
  }
  if (near_.has_value()) {
    spdlog::debug("near: {}", near_.value());
  }
  if (far_.has_value()) {
    spdlog::debug("far: {}", far_.value());
  }
  if (fovInDegrees_.has_value()) {
    spdlog::debug("fovInDegrees: {}", fovInDegrees_.value());
  }
  if (aspect_.has_value()) {
    spdlog::debug("aspect: {}", aspect_.value());
  }
  if (fovDirection_.has_value()) {
    spdlog::debug("fovDirection: {}", getTextForFov(fovDirection_.value()));
  }
  spdlog::debug("++++++++");
}

////////////////////////////////////////////////////////////////////////////
const char* Projection::getTextForType(filament::Camera::Projection type) {
  return (const char*[]){
      kTypePerspective,
      kTypeOrtho,
  }[static_cast<int>(type)];
}

////////////////////////////////////////////////////////////////////////////
filament::Camera::Projection Projection::getTypeForText(
    const std::string& type) {
  if (type == kTypePerspective)
    return filament::Camera::Projection::PERSPECTIVE;
  return filament::Camera::Projection::ORTHO;
}

////////////////////////////////////////////////////////////////////////////
const char* Projection::getTextForFov(filament::Camera::Fov fov) {
  return (const char*[]){
      kFovVertical,
      kFovHorizontal,
  }[static_cast<int>(fov)];
}

////////////////////////////////////////////////////////////////////////////
filament::Camera::Fov Projection::getFovForText(const std::string& fov) {
  if (fov == kFovVertical) {
    return filament::Camera::Fov::VERTICAL;
  }
  if (fov == kFovHorizontal) {
    return filament::Camera::Fov::HORIZONTAL;
  }
  return filament::Camera::Fov::HORIZONTAL;
}

}  // namespace plugin_filament_view
