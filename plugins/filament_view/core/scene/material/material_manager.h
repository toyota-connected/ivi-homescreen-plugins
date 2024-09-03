/*
 * Copyright 2020-2023 Toyota Connected North America
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

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <filament/MaterialInstance.h>

#include "core/scene/material/loader/material_loader.h"
#include "core/scene/material/loader/texture_loader.h"
#include "viewer/custom_model_viewer.h"

namespace plugin_filament_view {

class CustomModelViewer;
class MaterialDefinitions;
class MaterialLoader;
class TextureLoader;

class MaterialManager {
 public:
  MaterialManager();

  Resource<::filament::MaterialInstance*> getMaterialInstance(
      MaterialDefinitions* materialDefinitions);

  // Disallow copy and assign.
  MaterialManager(const MaterialManager&) = delete;
  MaterialManager& operator=(const MaterialManager&) = delete;

 private:
  std::unique_ptr<plugin_filament_view::MaterialLoader> materialLoader_;
  std::unique_ptr<plugin_filament_view::TextureLoader> textureLoader_;

  static Resource<::filament::Material*> loadMaterialFromResource(
      MaterialDefinitions* materialDefinition);
  static Resource<::filament::MaterialInstance*> setupMaterialInstance(
      ::filament::Material* materialResult,
      const MaterialDefinitions* materialDefinition);

  // this map contains the loaded materials from disk, that are not actively
  // used but instead copies (instances) are made of, then the instances are
  // used. Reducing disk reload.
  std::map<std::string, Resource<::filament::Material*>>
      loadedTemplateMaterials_;
  std::mutex loadingMaterialsMutex_;
};
}  // namespace plugin_filament_view