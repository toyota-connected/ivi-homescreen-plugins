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

#include "core/scene/material/material_manager.h"

#include "plugins/common/common.h"

namespace plugin_filament_view {

MaterialManager::MaterialManager() {
  SPDLOG_TRACE("++MaterialManager::MaterialManager");

  materialLoader_ = std::make_unique<MaterialLoader>();
  textureLoader_ = std::make_unique<TextureLoader>();

  SPDLOG_TRACE("--MaterialManager::MaterialManager");
}

Resource<::filament::Material*> MaterialManager::loadMaterial(
    Material* material) {
  // The Future object for loading Material
  if (!material->assetPath_.empty()) {
    // THIS does NOT set default a parameter values
    return materialLoader_->loadMaterialFromAsset(material->assetPath_);
  } else if (!material->url_.empty()) {
    return materialLoader_->loadMaterialFromUrl(material->url_);
  } else {
    return Resource<::filament::Material*>::Error(
        "You must provide material asset path or url");
  }
}

Resource<::filament::MaterialInstance*> MaterialManager::setupMaterialInstance(
    ::filament::Material* materialResult,
    const Material* material) {
  if (!materialResult) {
    SPDLOG_ERROR("Unable to {}::{}", __FILE__, __FUNCTION__);
    return Resource<::filament::MaterialInstance*>::Error("argument is NULL");
  }

  auto materialInstance = materialResult->createInstance();

  material->vSetMaterialInstancePropertiesFromMyPropertyMap(materialResult,
                                                            materialInstance);

  return Resource<::filament::MaterialInstance*>::Success(materialInstance);
}

Resource<::filament::MaterialInstance*> MaterialManager::getMaterialInstance(
    Material* material) {
  SPDLOG_TRACE("++MaterialManager::getMaterialInstance");

  if (!material) {
    SPDLOG_ERROR("--Bad Material Result MaterialManager::getMaterialInstance");
    Resource<::filament::MaterialInstance*>::Error("Material not found");
  }

  SPDLOG_TRACE("++MaterialManager::LoadingMaterial");
  auto materialResult = loadMaterial(material);

  if (materialResult.getStatus() != Status::Success) {
    SPDLOG_ERROR("--Bad Material Result MaterialManager::getMaterialInstance");
    return Resource<::filament::MaterialInstance*>::Error(
        materialResult.getMessage());
  }

  auto materialInstance =
      setupMaterialInstance(materialResult.getData().value(), material);

  SPDLOG_TRACE("--MaterialManager::getMaterialInstance");
  return materialInstance;
}

}  // namespace plugin_filament_view