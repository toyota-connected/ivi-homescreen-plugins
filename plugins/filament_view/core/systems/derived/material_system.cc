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

#include "material_system.h"
#include "filament_system.h"

#include <core/components/derived/material.h>
#include <core/entity/base/entityobject.h>
#include <core/systems/ecs.h>
#include <plugins/common/common.h>

namespace plugin_filament_view {

/////////////////////////////////////////////////////////////////////////////////////////
MaterialSystem::MaterialSystem() {
  SPDLOG_TRACE("++{}", __FUNCTION__);

  materialLoader_ = std::make_unique<MaterialLoader>();
  textureLoader_ = std::make_unique<TextureLoader>();

  SPDLOG_TRACE("--{}", __FUNCTION__);
}

/////////////////////////////////////////////////////////////////////////////////////////
MaterialSystem::~MaterialSystem() { SPDLOG_DEBUG("--{}", __FUNCTION__); }

void MaterialSystem::onComponentOperation(
  EntityObject& entity,   //
  Component& component,   //
  ECSOperation operation  //
) {
  auto* material = dynamic_cast<Material*>(&component);
  if (!material) {
    spdlog::warn("[{}] - Not a Material component", __FUNCTION__);
    return;
  }

  switch (operation) {
    case ECSOperation::Add:
      _addMaterial(entity, *material);
      break;
    case ECSOperation::Remove:
      _removeMaterial(entity, *material);
      break;
  }
}

void MaterialSystem::_updateMaterialInstanceProperty(
  const Material& material,
  const MaterialParameter* param
) {
  if (!material._instance.get()) {
    spdlog::error("Material hasn't been loaded yet");
    return;
  }

  if (material._instance->getStatus() != Status::Success) {
    spdlog::error("No material definition set for model, set one first that's not the "
                  "uber shader.");
    return;
  }

  auto materialInstance = material._instance->getData().value();

  const std::string paramName = param->getName();
  const char* szParamName = paramName.c_str();

  switch (param->type_) {
    case MaterialParameter::MaterialType::COLOR: {
      materialInstance->setParameter(
        szParamName, filament::RgbaType::LINEAR, param->colorValue_.value()
      );
    } break;

    case MaterialParameter::MaterialType::FLOAT: {
      materialInstance->setParameter(szParamName, param->fValue_.value());
    } break;

    case MaterialParameter::MaterialType::TEXTURE: {
      // make sure we have the texture:
      const auto* textureId = param->getTextureValueAssetPath();
      if (!textureId) {
        spdlog::warn("Texture path for parameter '{}' is null", param->getName());
        return;
      }

      const auto foundResource = _loadedTextures.find(*textureId);
      if (foundResource == _loadedTextures.end()) {
        // log and continue
        spdlog::warn("Got to a case where a texture was not loaded before trying to "
                     "apply to a material.");
        return;
      }

      // sampler will be on 'our' deserialized
      // texturedefinitions->texture_sampler
      const auto textureSampler = param->getTextureSampler();

      filament::TextureSampler sampler(MinFilter::LINEAR, MagFilter::LINEAR);

      if (textureSampler != nullptr) {
        // SPDLOG_INFO("Overloading filtering options with set param
        // values");
        sampler.setMinFilter(textureSampler->getMinFilter());
        sampler.setMagFilter(textureSampler->getMagFilter());
        sampler.setAnisotropy(static_cast<float>(textureSampler->getAnisotropy()));

        // Currently leaving this commented out, but this is for 3d
        // textures, which are not currently expected to be loaded
        // as time of writing.
        // sampler.setWrapModeR(textureSampler->getWrapModeR());

        sampler.setWrapModeS(textureSampler->getWrapModeS());
        sampler.setWrapModeT(textureSampler->getWrapModeT());
      }

      if (!foundResource->second.getData().has_value()) {
        spdlog::warn("Got to a case where a texture resource data was not loaded "
                     "before trying to "
                     "apply to a material.");
        return;
      }

      const auto texture = foundResource->second.getData().value();
      materialInstance->setParameter(szParamName, texture, sampler);
    } break;

    default: {
      SPDLOG_WARN("Type template not setup yet, see {}", __FUNCTION__);
    } break;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void MaterialSystem::_addMaterial(EntityObject& /*entity*/, Material& materialComponent) {
  SPDLOG_TRACE("++MaterialManager::_addMaterial");
  // In case of multi material load on <load>
  // we dont want to reload the same material several times and have collision
  // in the map
  std::lock_guard lock(_materialLoadingMutex);

  /*
   *  Load MaterialDefinition
   */
  const auto* lookupName = materialComponent.getLookupName();
  MaterialDefinition materialToInstanceFrom = _getMaterialDefinition(lookupName);

  /*
   *  Load textures
   */
  const auto materialsRequiredTextures = materialComponent.getTextureMaterialParameters();
  for (const auto& materialParam : materialsRequiredTextures) {
    try {
      const auto& textureValue = materialParam->getTextureValue();
      // TODO: what is this??? why get <- unique_ptr <- value ???
      const auto& texturePtr = std::get<std::unique_ptr<TextureDefinitions>>(textureValue);
      if (!texturePtr) {
        spdlog::error("Unable to access texture point value for {}", materialParam->getName());
        continue;
      }

      // Get from cache if loaded
      const auto* assetPath = materialParam->getTextureValueAssetPath();
      _getTexture(assetPath, texturePtr);
    } catch (const std::bad_variant_access& e) {
      spdlog::error("Error: Could not retrieve the texture value. {}", e.what());
    } catch (const std::runtime_error& e) {
      spdlog::error("Error:  {}", e.what());
    }
  }

  /*
   *  Create material instance
   */
  spdlog::debug("[{}] Instantiating material...", __FUNCTION__);
  MaterialInstance matInstance;
  const auto* matData = materialToInstanceFrom.getData().value();
  if (!matData) {
    spdlog::error("Unable to {}", __FUNCTION__);
    matInstance = MaterialInstance::Error("argument is NULL");
  }

  const auto matInstanceData = matData->createInstance();

  /*
   *  Copy parameters from material definition
   */
  {
    const auto count = matData->getParameterCount();
    std::vector<filament::Material::ParameterInfo> parameters(count);

    if (const auto actual = matData->getParameters(parameters.data(), count);
        count != actual || actual != parameters.size()) {
      spdlog::warn("Count of parameters from the material instance and loaded material do "
                   "not match; doesn't technically need to, but not ideal and could leave "
                   "to undefined results.");
    }

    // TODO: clean this up
    std::map<std::string, bool> savedParameters;

    for (const auto& param : materialComponent._tmpParams) {
      const auto& name = param->getName();
      SPDLOG_TRACE("[Material] name: {}, type: {}", name, static_cast<int>(param->type_));
      _updateMaterialInstanceProperty(materialComponent, param.get());
      savedParameters[name] = true;
    }

    // Print warnings for any parameters that were not set
    for (const auto& param : parameters) {
      if (param.name && !savedParameters[param.name]) {
        SPDLOG_WARN("No default parameter value available for {} {}", __FUNCTION__, param.name);
      }
    }
  }

  matInstance = MaterialInstance::Success(matInstanceData);
  // make unique_ptr
  // TODO: this is not optimal, does memcpy - fix it
  materialComponent._instance = std::make_shared<MaterialInstance>(matInstance);

  SPDLOG_TRACE("--MaterialManager::_addMaterial");
}

void MaterialSystem::_removeMaterial(EntityObject& entity, Material& material) {
  SPDLOG_TRACE("++MaterialManager::_removeMaterial");

  if (material._instance && material._instance->getData().has_value()) {
    const auto filamentSystem = ecs->getSystem<FilamentSystem>("MaterialSystem::_removeMaterial");
    const auto engine = filamentSystem->getFilamentEngine();

    engine->destroy(*material._instance->getData());
    material._instance.reset();
  } else {
    spdlog::warn("Material instance is null or has no data, cannot remove.");
  }

  SPDLOG_TRACE("--MaterialManager::_removeMaterial");
}

MaterialDefinition MaterialSystem::_getMaterialDefinition(const std::string* assetPath) {
  std::lock_guard<std::mutex> lock(_materialLoadingMutex);

  if (!assetPath) {
    SPDLOG_ERROR("MaterialSystem::_getMaterialDefinition - Invalid asset path");
    return MaterialDefinition::Error("Invalid asset path");
  }

  // Fetch from cache if present
  const auto matdefRecord = _loadedMaterialDefinitions.find(*assetPath);
  if (matdefRecord != _loadedMaterialDefinitions.end()) {
    return matdefRecord->second;
  }

  // If not found in cache, load from resource
  SPDLOG_TRACE("++MaterialSystem::LoadingMaterial");
  MaterialDefinition matdef;

  // The Future object for loading Material
  if (!assetPath->empty()) {
    // THIS does NOT set default a parameter values
    matdef = MaterialLoader::loadMaterialFromAsset(*assetPath);
    // TODO: implement URL resource handling
    // } else if (!materialDefinition->szGetMaterialURLPath().empty()) {
    //   matdef = MaterialLoader::loadMaterialFromUrl(materialDefinition->szGetMaterialURLPath());
  } else {
    spdlog::error("MaterialSystem::LoadingMaterial - No valid asset path or URL");
    return MaterialDefinition::Error("You must provide material asset path or url");
  }

  if (matdef.getStatus() != Status::Success) {
    spdlog::error("--Bad Material Result {}", __FUNCTION__);
    return matdef;
  }

  // if we got here the material is valid, and we should add it into our map
  _loadedMaterialDefinitions.insert(std::make_pair(*assetPath, matdef));
  return matdef;
}

Texture MaterialSystem::_getTexture(
  const std::string* assetPath,
  const std::unique_ptr<TextureDefinitions>& texturePtr
) {
  // Lookup in cache and return if found
  if (auto foundAsset = _loadedTextures.find(*assetPath); foundAsset != _loadedTextures.end()) {
    return foundAsset->second;
  }

  // Load if not already loaded
  auto loadedTexture = TextureLoader::loadTexture(texturePtr.get());
  if (loadedTexture.getStatus() != Status::Success) {
    spdlog::error("Unable to load texture from {}", *assetPath);
  } else {
    // If loading was successful, add it to the loaded textures map
    _loadedTextures.insert(std::pair(*assetPath, loadedTexture));
  }

  return loadedTexture;
}

/////////////////////////////////////////////////////////////////////////////////////////
void MaterialSystem::onSystemInit() {
  registerMessageHandler(ECSMessageType::ChangeMaterialParameter, [this](const ECSMessage& msg) {
    spdlog::debug("ChangeMaterialParameter");

    const flutter::EncodableMap& params = msg.getData<flutter::EncodableMap>(
      ECSMessageType::ChangeMaterialParameter
    );
    const auto guid = msg.getData<EntityGUID>(ECSMessageType::EntityToTarget);

    if (const auto matComponent = ecs->getComponent<Material>(guid); matComponent != nullptr) {
      spdlog::debug("ChangeMaterialParameter valid entity found.");

      auto parameter = MaterialParameter::Deserialize("", params);
      matComponent->setInstanceProperty(std::move(parameter));
    }

    spdlog::debug("ChangeMaterialParameter Complete");
  });

  registerMessageHandler(ECSMessageType::ChangeMaterialDefinition, [this](const ECSMessage& msg) {
    spdlog::debug("ChangeMaterialDefinition");

    const flutter::EncodableMap& params = msg.getData<flutter::EncodableMap>(
      ECSMessageType::ChangeMaterialDefinition
    );

    const auto guid = msg.getData<EntityGUID>(ECSMessageType::EntityToTarget);

    if (const auto entityObject = ecs->getEntity(guid); entityObject != nullptr) {
      spdlog::debug("ChangeMaterialDefinition valid entity found.");

      // const auto material = entityObject->getComponent<Material>();
      spdlog::warn("[{}] ChangeMaterialDefinition called, TODO: refactor");
      // material->ChangeMaterialDefinition(params, _loadedTextures);
    }

    // spdlog::debug("ChangeMaterialDefinition Complete");
  });
}

void MaterialSystem::update(double /*deltaTime*/) {
  // Fetch all Material components
  const auto materials = ecs->getComponentsOfType<Material>();

  // For each material, apply pending instance properties
  for (auto& material : materials) {
    auto& params = material->_tmpParams;
    if (params.empty()) continue;

    for (size_t i = params.size(); i < 0; --i) {
      auto& param = params[i];
      try {
        _updateMaterialInstanceProperty(*material, param.get());
        params.pop_back();
      } catch (const std::exception& e) {
        spdlog::error("Failed to update material instance property: {}", e.what());
      }
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////////////
void MaterialSystem::onDestroy() {
  const auto filamentSystem = ecs->getSystem<FilamentSystem>("MaterialSystem::onDestroy");
  const auto engine = filamentSystem->getFilamentEngine();

  for (const auto& [fst, snd] : _loadedMaterialDefinitions) {
    engine->destroy(*snd.getData());
  }

  for (const auto& [fst, snd] : _loadedTextures) {
    engine->destroy(*snd.getData());
  }

  _loadedMaterialDefinitions.clear();
  _loadedTextures.clear();

  materialLoader_.reset();
  textureLoader_.reset();
}

/////////////////////////////////////////////////////////////////////////////////////////
void MaterialSystem::debugPrint() { spdlog::debug("{}", __FUNCTION__); }

}  // namespace plugin_filament_view
