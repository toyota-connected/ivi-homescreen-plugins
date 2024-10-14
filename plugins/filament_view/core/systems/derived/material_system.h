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
#include "core/systems/base/ecsystem.h"

namespace plugin_filament_view {

class MaterialDefinitions;
class MaterialLoader;
class TextureLoader;

using TextureMap = std::map<std::string, Resource<::filament::Texture*>>;

class MaterialSystem : public ECSystem {
 public:
  MaterialSystem();
  ~MaterialSystem() override;

  Resource<::filament::MaterialInstance*> getMaterialInstance(
      MaterialDefinitions* materialDefinitions);

  // Disallow copy and assign.
  MaterialSystem(const MaterialSystem&) = delete;
  MaterialSystem& operator=(const MaterialSystem&) = delete;

  void vInitSystem() override;
  void vUpdate(float fElapsedTime) override;
  void vShutdownSystem() override;
  void DebugPrint() override;

  [[nodiscard]] size_t GetTypeID() const override { return StaticGetTypeID(); }

  [[nodiscard]] static size_t StaticGetTypeID() {
    return typeid(MaterialSystem).hash_code();
  }

 private:
  std::unique_ptr<plugin_filament_view::MaterialLoader> materialLoader_;
  std::unique_ptr<plugin_filament_view::TextureLoader> textureLoader_;

  static Resource<::filament::Material*> loadMaterialFromResource(
      MaterialDefinitions* materialDefinition);
  Resource<::filament::MaterialInstance*> setupMaterialInstance(
      ::filament::Material* materialResult,
      const MaterialDefinitions* materialDefinition);

  // this map contains the loaded materials from disk, that are not actively
  // used but instead copies (instances) are made of, then the instances are
  // used. Reducing disk reload.
  std::map<std::string, Resource<::filament::Material*>>
      loadedTemplateMaterials_;
  std::mutex loadingMaterialsMutex_;

  // This map is a list of all loaded textures. Multiple materials might
  // reference the same texture, and instead of loading them separately; they'll
  // be reused here. As of writing 202409 Textures are tied to materials, so it
  // makes sense to have a check if a material needs a texture, to load it in
  // that stack chain.
  TextureMap loadedTextures_;
};
}  // namespace plugin_filament_view