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

#include "texture_sampler.h"

#include "enums/mag_filter.h"
#include "enums/min_filter.h"
#include "enums/wrap_mode.h"
#include "plugins/common/common.h"

namespace plugin_filament_view {

TextureSampler::TextureSampler(const flutter::EncodableMap& params) {
  SPDLOG_TRACE("++TextureSampler::TextureSampler");
  for (auto& it : params) {
    if (it.second.IsNull())
      continue;

    auto key = std::get<std::string>(it.first);
    if (key == "min" && std::holds_alternative<std::string>(it.second)) {
      min_ = std::get<std::string>(it.second);
    } else if (key == "mag" && std::holds_alternative<std::string>(it.second)) {
      mag_ = std::get<std::string>(it.second);
    } else if (key == "wrap" &&
               std::holds_alternative<std::string>(it.second)) {
      wrapR_ = std::get<std::string>(it.second);
      wrapS_ = std::get<std::string>(it.second);
      wrapT_ = std::get<std::string>(it.second);
    } else if (key == "wrapR" &&
               std::holds_alternative<std::string>(it.second)) {
      wrapR_ = std::get<std::string>(it.second);
    } else if (key == "wrapS" &&
               std::holds_alternative<std::string>(it.second)) {
      wrapS_ = std::get<std::string>(it.second);
    } else if (key == "wrapT" &&
               std::holds_alternative<std::string>(it.second)) {
      wrapT_ = std::get<std::string>(it.second);
    } else if (key == "anisotropy" &&
               std::holds_alternative<double>(it.second)) {
      anisotropy_ = std::get<double>(it.second);
    } else if (!it.second.IsNull()) {
      spdlog::debug("[TextureSampler] Unhandled Parameter");
      plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(),
                                                           it.second);
    }
  }
  SPDLOG_TRACE("--TextureSampler::TextureSampler");
}

::filament::TextureSampler::MagFilter TextureSampler::getMagFilter() const {
  if (mag_ == kMagFilterNearest) {
    return ::filament::TextureSampler::MagFilter::NEAREST;
  }
  return ::filament::TextureSampler::MagFilter::LINEAR;
}

::filament::TextureSampler::MinFilter TextureSampler::getMinFilter() const {
  if (min_ == kMinFilterNearest) {
    return ::filament::TextureSampler::MinFilter::NEAREST;
  }

  if (min_ == kMinFilterLinear) {
    return ::filament::TextureSampler::MinFilter::LINEAR;
  }

  if (min_ == kMinFilterNearestMipmapNearest) {
    return ::filament::TextureSampler::MinFilter::NEAREST_MIPMAP_NEAREST;
  }

  if (min_ == kMinFilterLinearMipmapNearest) {
    return ::filament::TextureSampler::MinFilter::LINEAR_MIPMAP_NEAREST;
  }

  if (min_ == kMinFilterNearestMipmapLinear) {
    return ::filament::TextureSampler::MinFilter::NEAREST_MIPMAP_LINEAR;
  }

  // Note: might need to change default in the future.
  return ::filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR;
}

::filament::TextureSampler::WrapMode TextureSampler::getWrapModeR() const {
  if (wrapR_ == KWrapModeClampToEdge) {
    return ::filament::TextureSampler::WrapMode::CLAMP_TO_EDGE;
  }

  if (wrapR_ == KWrapModeRepeat) {
    return ::filament::TextureSampler::WrapMode::REPEAT;
  }

  return ::filament::TextureSampler::WrapMode::MIRRORED_REPEAT;
}

::filament::TextureSampler::WrapMode TextureSampler::getWrapModeS() const {
  if (wrapS_ == KWrapModeClampToEdge) {
    return ::filament::TextureSampler::WrapMode::CLAMP_TO_EDGE;
  }

  if (wrapS_ == KWrapModeRepeat) {
    return ::filament::TextureSampler::WrapMode::REPEAT;
  }

  return ::filament::TextureSampler::WrapMode::MIRRORED_REPEAT;
}

::filament::TextureSampler::WrapMode TextureSampler::getWrapModeT() const {
  if (wrapT_ == KWrapModeClampToEdge) {
    return ::filament::TextureSampler::WrapMode::CLAMP_TO_EDGE;
  }

  if (wrapT_ == KWrapModeRepeat) {
    return ::filament::TextureSampler::WrapMode::REPEAT;
  }

  return ::filament::TextureSampler::WrapMode::MIRRORED_REPEAT;
}

void TextureSampler::DebugPrint(const char* tag) {
  spdlog::debug("++++++++");
  spdlog::debug("{} (TextureSampler)", tag);
  if (!min_.empty()) {
    spdlog::debug("\tmin: [{}]", min_);
  }
  if (!mag_.empty()) {
    spdlog::debug("\tmag: [{}]", mag_);
  }
  if (!wrapR_.empty()) {
    spdlog::debug("\twrapR: [{}]", wrapR_);
  }
  if (!wrapS_.empty()) {
    spdlog::debug("\twrapS: [{}]", wrapS_);
  }
  if (!wrapT_.empty()) {
    spdlog::debug("\twrapT: [{}]", wrapT_);
  }
  if (anisotropy_.has_value()) {
    spdlog::debug("\tanisotropy: [{}]", anisotropy_.value());
  }
  spdlog::debug("++++++++");
}

}  // namespace plugin_filament_view