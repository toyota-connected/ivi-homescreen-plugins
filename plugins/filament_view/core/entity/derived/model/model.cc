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

#include "model.h"

#include <core/components/derived/collider.h>
#include <core/include/literals.h>
#include <core/systems/derived/filament_system.h>
#include <core/systems/derived/material_system.h>
#include <core/systems/ecs.h>
#include <core/utils/deserialize.h>
#include <filament/RenderableManager.h>
#include <plugins/common/common.h>
#include <utility>
#include <utils/Slice.h>

namespace plugin_filament_view {

const char* modelInstancingModeToString(ModelInstancingMode mode) {
  switch (mode) {
    case ModelInstancingMode::none:
      return "none";
    case ModelInstancingMode::primary:
      return "primary";
    case ModelInstancingMode::secondary:
      return "secondary";
    default:
      return "unknown";
  }
}

////////////////////////////////////////////////////////////////////////////
Model::Model()
  : EntityObject(),
    assetPath_(),
    m_poAsset(nullptr),
    m_poAssetInstance(nullptr) {}

void Model::deserializeFrom(const flutter::EncodableMap& params) {
  EntityObject::deserializeFrom(params);

  // assetPath_
  assetPath_ = Deserialize::DecodeParameter<std::string>(kAssetPath, params);

  // is_glb
  bool is_glb = Deserialize::DecodeParameter<bool>(kIsGlb, params);
  runtime_assert(is_glb, "Model::deserializeFrom - is_glb must be true for Model");

  // _instancingMode
  Deserialize::DecodeEnumParameterWithDefault(
    kModelInstancingMode, &_instancingMode, params, ModelInstancingMode::none
  );

  spdlog::trace(
    "Model({}), instanceMode: {}", assetPath_, modelInstancingModeToString(_instancingMode)
  );

  // Animation (optional)
  spdlog::trace("Making Animation...");
  if (Deserialize::HasKey(params, kAnimation)) {
    // they're requesting an animation on this object. Make one.
    addComponent(Animation(params));
  } else {
    spdlog::trace("This entity params has no animation");
  }
}

////////////////////////////////////////////////////////////////////////////
std::shared_ptr<Model> Model::Deserialize(const flutter::EncodableMap& params) {
  std::shared_ptr<Model> toReturn = std::make_shared<Model>();
  toReturn->deserializeFrom(params);

  return toReturn;
}

////////////////////////////////////////////////////////////////////////////
void Model::debugPrint() const { debugPrintComponents(); }

////////////////////////////////////////////////////////////////////////////
// TODO: this goes to ModelSystem
/*
void Model::ChangeMaterialDefinition(
  const flutter::EncodableMap& params,
  const TextureMap& // loadedTextures
) {
  // if we have a materialdefinitions component, we need to remove it
  // and remake / add a new one.
  ecs->removeComponent<Material>(guid_);

  // If you want to inspect the params coming in.
  // for (const auto& [fst, snd] : params) {
  //     auto key = std::get<std::string>(fst);
  //     plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(), snd);
  // }

  auto materialDefinitions = std::make_shared<Material>(params);
  ecs->addComponent(guid_, std::move(materialDefinitions));

  m_poMaterialInstance.reset();

  // then tell material system to load us the correct way once
  // we're deserialized.
  LoadMaterialDefinitionsToMaterialInstance();

  if (m_poMaterialInstance.getStatus() != Status::Success) {
    spdlog::error("Unable to load material definition to instance, bailing out.");
    return;
  }

  // now, reload / rebuild the material?
  const auto filamentSystem = ECSManager::GetInstance()->getSystem<FilamentSystem>(
    "BaseShape::ChangeMaterialDefinition"
  );

  // If your entity has multiple primitives, you’ll need to call
  // setMaterialInstanceAt for each primitive you want to update.
  auto& renderManager = filamentSystem->getFilamentEngine()->getRenderableManager();

  if (getAsset()) {
    const utils::Slice listOfRenderables{
      getAsset()->getRenderableEntities(), getAsset()->getRenderableEntityCount()
    };

    // Note this will apply to EVERYTHING currently. You might want a custom
    // <only effect these pieces> type functionality.
    for (const auto entity : listOfRenderables) {
      const auto ri = renderManager.getInstance(entity);

      // I dont know about primitive index being non zero if our tree has
      // multiple nodes getting from the asset.
      renderManager.setMaterialInstanceAt(ri, 0, *m_poMaterialInstance.getData());
    }
  } else if (getAssetInstance()) {
    const FilamentEntity* instanceEntities = getAssetInstance()->getEntities();
    const size_t instanceEntityCount = getAssetInstance()->getEntityCount();

    for (size_t i = 0; i < instanceEntityCount; i++) {
      // Check if this entity has a Renderable component
      if (const FilamentEntity entity = instanceEntities[i]; renderManager.hasComponent(entity)) {
        const auto ri = renderManager.getInstance(entity);

        // A Renderable can have multiple primitives (submeshes)
        const size_t submeshCount = renderManager.getPrimitiveCount(ri);
        for (size_t sm = 0; sm < submeshCount; sm++) {
          // Give the submesh our new material instance
          renderManager.setMaterialInstanceAt(ri, sm, *m_poMaterialInstance.getData());
        }
      }
    }
  }
}
*/
}  // namespace plugin_filament_view
