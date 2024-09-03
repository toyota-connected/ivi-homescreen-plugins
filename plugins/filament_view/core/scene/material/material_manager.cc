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

#include "core/scene/material/material_manager.h"

#include "plugins/common/common.h"

namespace plugin_filament_view {

MaterialManager::MaterialManager() {
  SPDLOG_TRACE("++MaterialManager::MaterialManager");

  materialLoader_ = std::make_unique<MaterialLoader>();
  textureLoader_ = std::make_unique<TextureLoader>();

  SPDLOG_TRACE("--MaterialManager::MaterialManager");
}

Resource<::filament::Material*> MaterialManager::loadMaterialFromResource(
    MaterialDefinitions* materialDefinition) {
  // The Future object for loading Material
  if (!materialDefinition->assetPath_.empty()) {
    // THIS does NOT set default a parameter values
    return MaterialLoader::loadMaterialFromAsset(
        materialDefinition->assetPath_);
  } else if (!materialDefinition->url_.empty()) {
    return MaterialLoader::loadMaterialFromUrl(materialDefinition->url_);
  } else {
    return Resource<::filament::Material*>::Error(
        "You must provide material asset path or url");
  }
}

Resource<::filament::MaterialInstance*> MaterialManager::setupMaterialInstance(
    ::filament::Material* materialResult,
    const MaterialDefinitions* materialDefinitions) {
  if (!materialResult) {
    SPDLOG_ERROR("Unable to {}::{}", __FILE__, __FUNCTION__);
    return Resource<::filament::MaterialInstance*>::Error("argument is NULL");
  }

  auto materialInstance = materialResult->createInstance();
  materialDefinitions->vSetMaterialInstancePropertiesFromMyPropertyMap(
      materialResult, materialInstance);

  return Resource<::filament::MaterialInstance*>::Success(materialInstance);
}

Resource<::filament::MaterialInstance*> MaterialManager::getMaterialInstance(
    MaterialDefinitions* materialDefinitions) {
  SPDLOG_TRACE("++MaterialManager::getMaterialInstance");

  if (!materialDefinitions) {
    SPDLOG_ERROR(
        "--Bad MaterialDefinitions Result "
        "MaterialManager::getMaterialInstance");
    Resource<::filament::MaterialInstance*>::Error("Material not found");
  }

  Resource<filament::Material*> materialToInstanceFrom =
      Resource<::filament::Material*>::Error("Unset");

  // In case of multi material load on <load>
  // we dont want to reload the same material several times and have collision
  // in the map
  std::lock_guard<std::mutex> lock(loadingMaterialsMutex_);

  auto lookupName = materialDefinitions->szGetMaterialDefinitionLookupName();
  auto materialToInstanceFromIter = loadedTemplateMaterials_.find(lookupName);
  if (materialToInstanceFromIter != loadedTemplateMaterials_.end()) {
    materialToInstanceFrom = materialToInstanceFromIter->second;
  } else {
    SPDLOG_TRACE("++MaterialManager::LoadingMaterial");
    materialToInstanceFrom = loadMaterialFromResource(materialDefinitions);

    if (materialToInstanceFrom.getStatus() != Status::Success) {
      SPDLOG_ERROR(
          "--Bad Material Result MaterialManager::getMaterialInstance");
      return Resource<::filament::MaterialInstance*>::Error(
          materialToInstanceFrom.getMessage());
    }

    // if we got here the material is valid, and we should add it into our map
    loadedTemplateMaterials_.insert(
        std::make_pair(lookupName, materialToInstanceFrom));
  }

  auto materialInstance = setupMaterialInstance(
      materialToInstanceFrom.getData().value(), materialDefinitions);

  SPDLOG_TRACE("--MaterialManager::getMaterialInstance");
  return materialInstance;
}

}  // namespace plugin_filament_view