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

#include "texture_definitions.h"

#include <plugins/common/common.h>
#include <optional>

namespace plugin_filament_view {

static constexpr char kTypeColor[] = "COLOR";
static constexpr char kTypeNormal[] = "NORMAL";
static constexpr char kTypeData[] = "DATA";

////////////////////////////////////////////////////////////////////////////
TextureDefinitions::TextureDefinitions(TextureType type,
                                       std::string assetPath,
                                       std::string url,
                                       TextureSampler* sampler)
    : assetPath_(std::move(assetPath)),
      url_(std::move(url)),
      type_(type),
      sampler_(sampler) {}

////////////////////////////////////////////////////////////////////////////
TextureDefinitions::~TextureDefinitions() {
  delete sampler_;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
std::string TextureDefinitions::szGetTextureDefinitionLookupName() const {
  if (!assetPath_.empty()) {
    return assetPath_;
  }
  if (!url_.empty()) {
    return url_;
  }
  return "Unknown";
}

////////////////////////////////////////////////////////////////////////////
std::unique_ptr<TextureDefinitions> TextureDefinitions::Deserialize(
    const flutter::EncodableMap& params) {
  SPDLOG_TRACE("++Texture::Texture");
  std::optional<std::string> assetPath;
  std::optional<std::string> url;
  std::optional<TextureType> type;
  std::optional<std::unique_ptr<TextureSampler>> sampler;

  for (const auto& [fst, snd] : params) {
    if (snd.IsNull())
      continue;

    auto key = std::get<std::string>(fst);
    if (key == "assetPath" && std::holds_alternative<std::string>(snd)) {
      assetPath = std::get<std::string>(snd);
    } else if (key == "url" && std::holds_alternative<std::string>(snd)) {
      url = std::get<std::string>(snd);
    } else if (key == "type" && std::holds_alternative<std::string>(snd)) {
      type = getType(std::get<std::string>(snd));
    } else if (key == "sampler" &&
               std::holds_alternative<flutter::EncodableMap>(snd)) {
      sampler = std::make_unique<TextureSampler>(
          std::get<flutter::EncodableMap>(snd));
    } else if (!snd.IsNull()) {
      spdlog::debug("[Texture] Unhandled Parameter");
      plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(), snd);
    }
  }
  if (!type.has_value()) {
    spdlog::error("[Texture] missing type");
    return nullptr;
  }

  SPDLOG_TRACE("--Texture::Texture");
  return std::make_unique<TextureDefinitions>(
      type.value(), assetPath.has_value() ? std::move(assetPath.value()) : "",
      url.has_value() ? std::move(url.value()) : "",
      sampler.has_value() ? sampler.value().get() : nullptr);
}

////////////////////////////////////////////////////////////////////////////
void TextureDefinitions::DebugPrint(const char* tag) {
  spdlog::debug("++++++++ (Texture) ++++++++");
  spdlog::debug("{} ", tag);
  if (!assetPath_.empty()) {
    spdlog::debug("assetPath: [{}]", assetPath_);
  }
  if (!url_.empty()) {
    spdlog::debug("url: [{}]", url_);
  }
  spdlog::debug("type: {}", getTextForType(type_));
  if (sampler_) {
    // TODO CRASH
    // sampler_->Print("\t\tsampler");
  }
  spdlog::debug("-------- (Texture) --------");
}

////////////////////////////////////////////////////////////////////////////
TextureDefinitions::TextureType TextureDefinitions::getType(
    const std::string& type) {
  if (type == kTypeColor) {
    return TextureType::COLOR;
  }
  if (type == kTypeNormal) {
    return TextureType::NORMAL;
  }
  if (type == kTypeData) {
    return TextureType::DATA;
  }
  assert(false);
}

////////////////////////////////////////////////////////////////////////////
const char* TextureDefinitions::getTextForType(
    TextureDefinitions::TextureType type) {
  return (const char*[]){
      kTypeColor,
      kTypeNormal,
      kTypeData,
  }[type];
}

}  // namespace plugin_filament_view