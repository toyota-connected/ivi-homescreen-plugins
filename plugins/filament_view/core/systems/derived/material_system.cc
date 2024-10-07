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

#include "core/systems/derived/material_system.h"

#include "plugins/common/common.h"

namespace plugin_filament_view {

/////////////////////////////////////////////////////////////////////////////////////////
MaterialSystem::MaterialSystem() {
  SPDLOG_TRACE("++{}", __FUNCTION__);

  materialLoader_ = std::make_unique<MaterialLoader>();
  textureLoader_ = std::make_unique<TextureLoader>();

  SPDLOG_TRACE("--{}", __FUNCTION__);
}

/////////////////////////////////////////////////////////////////////////////////////////
MaterialSystem::~MaterialSystem() {
  SPDLOG_DEBUG("--{}", __FUNCTION__);
}

/////////////////////////////////////////////////////////////////////////////////////////
Resource<::filament::Material*> MaterialSystem::loadMaterialFromResource(
    MaterialDefinitions* materialDefinition) {
  // The Future object for loading Material
  if (!materialDefinition->szGetMaterialAssetPath().empty()) {
    // THIS does NOT set default a parameter values
    return MaterialLoader::loadMaterialFromAsset(
        materialDefinition->szGetMaterialAssetPath());
  }

  if (!materialDefinition->szGetMaterialURLPath().empty()) {
    return MaterialLoader::loadMaterialFromUrl(
        materialDefinition->szGetMaterialURLPath());
  }

  return Resource<::filament::Material*>::Error(
      "You must provide material asset path or url");
}

/////////////////////////////////////////////////////////////////////////////////////////
Resource<::filament::MaterialInstance*> MaterialSystem::setupMaterialInstance(
    ::filament::Material* materialResult,
    const MaterialDefinitions* materialDefinitions) {
  if (!materialResult) {
    SPDLOG_ERROR("Unable to {}::{}", __FILE__, __FUNCTION__);
    return Resource<::filament::MaterialInstance*>::Error("argument is NULL");
  }

  auto materialInstance = materialResult->createInstance();
  materialDefinitions->vSetMaterialInstancePropertiesFromMyPropertyMap(
      materialResult, materialInstance, loadedTextures_);

  return Resource<::filament::MaterialInstance*>::Success(materialInstance);
}

/////////////////////////////////////////////////////////////////////////////////////////
Resource<::filament::MaterialInstance*> MaterialSystem::getMaterialInstance(
    MaterialDefinitions* materialDefinitions) {
  SPDLOG_TRACE("++MaterialManager::getMaterialInstance");
  if (!materialDefinitions) {
    SPDLOG_ERROR(
        "--Bad MaterialDefinitions Result "
        "MaterialManager::getMaterialInstance");
    return Resource<::filament::MaterialInstance*>::Error("Material not found");
  }

  Resource<filament::Material*> materialToInstanceFrom =
      Resource<::filament::Material*>::Error("Unset");

  // In case of multi material load on <load>
  // we dont want to reload the same material several times and have collision
  // in the map
  std::lock_guard lock(loadingMaterialsMutex_);

  auto lookupName = materialDefinitions->szGetMaterialDefinitionLookupName();
  auto materialToInstanceFromIter = loadedTemplateMaterials_.find(lookupName);
  if (materialToInstanceFromIter != loadedTemplateMaterials_.end()) {
    materialToInstanceFrom = materialToInstanceFromIter->second;
  } else {
    SPDLOG_TRACE("++MaterialManager::LoadingMaterial");
    materialToInstanceFrom = loadMaterialFromResource(materialDefinitions);

    if (materialToInstanceFrom.getStatus() != Status::Success) {
      spdlog::error(
          "--Bad Material Result MaterialManager::getMaterialInstance");
      return Resource<::filament::MaterialInstance*>::Error(
          materialToInstanceFrom.getMessage());
    }

    // if we got here the material is valid, and we should add it into our map
    loadedTemplateMaterials_.insert(
        std::make_pair(lookupName, materialToInstanceFrom));
  }

  // here we need to see if any & all textures that are requested on the
  // material be loaded before we create an instance of it.
  auto materialsRequiredTextures =
      materialDefinitions->vecGetTextureMaterialParameters();
  for (auto materialParam : materialsRequiredTextures) {
    try {
      // Call the getTextureValue method
      const auto& textureValue = materialParam->getTextureValue();

      // Access the Texture pointer from the MaterialTextureValue variant
      const auto& texturePtr =
          std::get<std::unique_ptr<TextureDefinitions>>(textureValue);

      if (!texturePtr) {
        spdlog::error("Unable to access texture point value for {}",
                      materialParam->szGetParameterName());
        continue;
      }

      // see if the asset path is already in our map of saved textures
      const auto assetPath = materialParam->getTextureValueAssetPath();
      auto foundAsset = loadedTextures_.find(assetPath);
      if (foundAsset != loadedTextures_.end()) {
        // it exists already, don't need to load it.
        continue;
      }

      // its not loaded already, lets load it.
      auto loadedTexture = TextureLoader::loadTexture(texturePtr.get());

      if (loadedTexture.getStatus() != Status::Success) {
        spdlog::error("Unable to load texture from {}", assetPath);
        Resource<::filament::Texture*>::Error(
            materialToInstanceFrom.getMessage());
        continue;
      }

      loadedTextures_.insert(std::pair(assetPath, loadedTexture));
    } catch (const std::bad_variant_access& e) {
      spdlog::error("Error: Could not retrieve the texture value. {}",
                    e.what());
    } catch (const std::runtime_error& e) {
      spdlog::error("Error:  {}", e.what());
    }
  }

  auto materialInstance = setupMaterialInstance(
      materialToInstanceFrom.getData().value(), materialDefinitions);

  SPDLOG_TRACE("--MaterialManager::getMaterialInstance");
  return materialInstance;
}

/////////////////////////////////////////////////////////////////////////////////////////
void MaterialSystem::vInitSystem() {}
/////////////////////////////////////////////////////////////////////////////////////////
void MaterialSystem::vUpdate(float /*fElapsedTime*/) {}
/////////////////////////////////////////////////////////////////////////////////////////
void MaterialSystem::vShutdownSystem() {}
/////////////////////////////////////////////////////////////////////////////////////////
void MaterialSystem::DebugPrint() {
    spdlog::debug("{}::{}", __FILE__, __FUNCTION__);
}

}  // namespace plugin_filament_view