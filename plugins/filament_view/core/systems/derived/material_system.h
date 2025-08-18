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

#include <core/scene/material/loader/material_loader.h>
#include <core/scene/material/loader/texture_loader.h>
#include <core/scene/material/material_parameter.h>
#include <core/systems/base/system.h>
#include <core/utils/filament_types.h>

#include <filament/MaterialInstance.h>

#include <map>
#include <memory>
#include <string>

namespace plugin_filament_view {

class Material;
class MaterialLoader;
class TextureLoader;

class MaterialSystem : public System {
  public:
    MaterialSystem();
    ~MaterialSystem() override;
    void onSystemInit() override;
    void update(double /*deltaTime*/) override;
    void onDestroy() override;
    void debugPrint() override;

    void onComponentOperation(
      EntityObject& entity,  //
      Component& component,
      ECSOperation operation
    ) override;

  private:
    void _addMaterial(EntityObject& /*entity*/, Material& material);
    void _removeMaterial(EntityObject& entity, Material& material);
    void _updateMaterialInstanceProperty(
      const Material& material,
      const MaterialParameter* matParam
    );

    /// Loads or retrieves a material definition from the cache
    MaterialDefinition _getMaterialDefinition(const std::string* assetPath);
    /// Loads or retrieves a texture from the cache
    Texture _getTexture(
      const std::string* assetPath,
      const std::unique_ptr<TextureDefinitions>& texturePtr
    );

    std::unique_ptr<plugin_filament_view::MaterialLoader> materialLoader_;
    std::unique_ptr<plugin_filament_view::TextureLoader> textureLoader_;

    static MaterialDefinition _loadMaterialFromResource(const Material* materialDefinition);
    MaterialInstance _setupMaterialInstance(
      const ::filament::Material* materialResult,
      const Material* materialDefinitions
    ) const;

    /// Loaded [MaterialDefinition]s - stored here before they can be instantiated
    /// @key: asset path [std::string]
    /// @value: material definition [MaterialDefinition]
    std::map<std::string, MaterialDefinition> _loadedMaterialDefinitions;
    std::mutex _materialLoadingMutex;
    // TODO: add a material use counter (or dependency graph?)
    //       to know when materials can be unloaded

    // Textures are tied to materials, so it makes sense to have a check
    // if a material needs a texture, to load it in that stack chain.
    /// @brief Loaded [Texture]s - stored here before they can be used in materials
    /// Multiple materials may reference the same texture
    /// @key: asset path [std::string]
    /// @value: texture [Texture]
    std::map<std::string, Texture> _loadedTextures;
    std::mutex _textureLoadingMutex;
    // TODO: add a texture use counter (or dependency graph?)
    //       to know when textures can be unloaded
};
}  // namespace plugin_filament_view
