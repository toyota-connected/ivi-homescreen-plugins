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

#include "animation.h"

#include <core/utils/deserialize.h>
#include <plugins/common/common.h>
#include <filesystem>

namespace plugin_filament_view {
Animation::Animation(const std::string& flutter_assets_path,
                     const flutter::EncodableMap& params)
    : flutterAssetsPath_(flutter_assets_path) {
  for (const auto& [fst, snd] : params) {
    if (snd.IsNull())
      continue;

    auto key = std::get<std::string>(fst);
    if (key == "autoPlay" && std::holds_alternative<bool>(snd)) {
      auto_play_ = std::get<bool>(snd);
    } else if (key == "index" && std::holds_alternative<int32_t>(snd)) {
      index_ = std::optional{std::get<int32_t>(snd)};
    } else if (key == "name" && std::holds_alternative<std::string>(snd)) {
      name_ = std::get<std::string>(snd);
    } else if (key == "assetPath" && std::holds_alternative<std::string>(snd)) {
      asset_path_ = std::get<std::string>(snd);
    } else if (key == "centerPosition" &&
               std::holds_alternative<flutter::EncodableMap>(snd)) {
      center_position_ = std::make_unique<filament::math::float3>(
          Deserialize::Format3(std::get<flutter::EncodableMap>(snd)));
    } else if (!snd.IsNull()) {
      spdlog::debug("[Animation] Unhandled Parameter");
      plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(), snd);
    }
  }
}

void Animation::DebugPrint(const char* tag) {
  spdlog::debug("++++++++");
  spdlog::debug("{} (Animation)", tag);
  spdlog::debug("\tname: [{}]", name_);
  if (index_.has_value()) {
    spdlog::debug("\tindex_: {}", index_.value());
  }
  spdlog::debug("\tautoPlay: {}", auto_play_);
  spdlog::debug("\tasset_path: [{}]", asset_path_);
  const std::filesystem::path asset_folder(flutterAssetsPath_);
  spdlog::debug("\tasset_path {} valid",
                exists(asset_folder / asset_path_) ? "is" : "is not");
  // TODO  center_position_->Print("\tcenterPosition:");
  spdlog::debug("++++++++");
}

}  // namespace plugin_filament_view