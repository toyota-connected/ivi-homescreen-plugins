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

#include <vector>

#include <filament/Engine.h>
#include <filament/Texture.h>
#include <image/LinearImage.h>

namespace plugin_filament_view {

class HDRLoader {
 public:
  static ::filament::Texture* createTexture(
      ::filament::Engine* engine,
      const std::string& asset_path,
      const std::string& name = "memory.hdr");

  static ::filament::Texture* createTexture(
      ::filament::Engine* engine,
      const std::vector<uint8_t>& buffer,
      const std::string& name = "memory.hdr");

 private:
  static ::filament::Texture* deleteImageAndLogError(image::LinearImage* image);

  static ::filament::Texture* createTextureFromImage(::filament::Engine* engine,
                                                     image::LinearImage* image);
};

}  // namespace plugin_filament_view
