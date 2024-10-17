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

#include <core/include/resource.h>
#include <core/scene/material/texture/texture_definitions.h>
#include <filament/Texture.h>
#include <future>

namespace plugin_filament_view {

class TextureDefinitions;

class TextureLoader {
 public:
  TextureLoader();
  ~TextureLoader() = default;

  static Resource<::filament::Texture*> loadTexture(
      const TextureDefinitions* texture);

  // Disallow copy and assign.
  TextureLoader(const TextureLoader&) = delete;
  TextureLoader& operator=(const TextureLoader&) = delete;

 private:
  static ::filament::Texture* createTextureFromImage(
      const std::string& file_path,
      const TextureDefinitions::TextureType type);

  static ::filament::Texture* loadTextureFromStream(
      const std::string& file_path,
      const TextureDefinitions::TextureType type);

  static ::filament::Texture* loadTextureFromUrl(
      const std::string& url,
      const TextureDefinitions::TextureType type);
};
}  // namespace plugin_filament_view