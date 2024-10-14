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

#include "texture_sampler.h"
#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"

#include <memory>


namespace plugin_filament_view {
class TextureDefinitions {
 public:
  enum TextureType {
    COLOR,
    NORMAL,
    DATA,
  };

  TextureDefinitions(TextureType type,
                     std::string assetPath,
                     std::string url,
                     TextureSampler* sampler);

  ~TextureDefinitions();

  static std::unique_ptr<TextureDefinitions> Deserialize(
      const flutter::EncodableMap& params);

  void DebugPrint(const char* tag);

  // Disallow copy and assign.
  TextureDefinitions(const TextureDefinitions&) = delete;
  TextureDefinitions& operator=(const TextureDefinitions&) = delete;

  static TextureType getType(const std::string& type);

  static const char* getTextForType(TextureType type);

  friend class TextureLoader;
  friend class MaterialSystem;

  // this will either get the assetPath or the url, priority of assetPath
  // looking for which is valid. Used to see if we have this loaded in cache.
  [[nodiscard]] std::string szGetTextureDefinitionLookupName() const;

  TextureSampler* getSampler() { return sampler_; }

 private:
  std::string assetPath_;
  std::string url_;
  TextureType type_;
  TextureSampler* sampler_;
};
}  // namespace plugin_filament_view