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
#include "scene_text_deserializer.h"

#include <asio/post.hpp>
#include <core/entity/base/entityobject.h>
#include <core/include/literals.h>
#include <core/systems/derived/collision_system.h>
#include <core/systems/derived/indirect_light_system.h>
#include <core/systems/derived/light_system.h>
#include <core/systems/derived/model_system.h>
#include <core/systems/derived/shape_system.h>
#include <core/systems/derived/skybox_system.h>
#include <core/systems/derived/view_target_system.h>
#include <core/systems/ecs.h>
#include <core/utils/deserialize.h>
#include <plugins/common/common.h>

#include "shell/platform/common/client_wrapper/include/flutter/standard_message_codec.h"

namespace plugin_filament_view {

//////////////////////////////////////////////////////////////////////////////////////////
SceneTextDeserializer::SceneTextDeserializer(const std::vector<uint8_t>& params)
  : _ecs(nullptr) {
  _ecs = ECSManager::GetInstance();
  const std::string& flutterAssetsPath = _ecs->getConfigValue<std::string>(kAssetPath);

  // kick off process...
  spdlog::debug("[{}] deserializing root...", __FUNCTION__);
  DeserializeRootLevel(params, flutterAssetsPath);
  spdlog::debug("[{}] deserializing root done!", __FUNCTION__);
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::DeserializeRootLevel(
  const std::vector<uint8_t>& params,
  const std::string& /* flutterAssetsPath */
) {
  auto& codec = flutter::StandardMessageCodec::GetInstance();
  const auto decoded = codec.DecodeMessage(params.data(), params.size());
  const auto& creationParams = std::get_if<flutter::EncodableMap>(decoded.get());

  for (const auto& [fst, snd] : *creationParams) {
    auto key = std::get<std::string>(fst);
    if (snd.IsNull()) {
      SPDLOG_DEBUG("DeserializeRootLevel ITER is null {} {}", key.c_str(), __FUNCTION__);
      continue;
    }

    if (key == kModels && std::holds_alternative<flutter::EncodableList>(snd)) {
      spdlog::debug("===== Deserializing Multiple Models {} ...", key);

      auto list = std::get<flutter::EncodableList>(snd);
      for (const auto& iter : list) {
        if (iter.IsNull()) {
          spdlog::warn("CreationParamName unable to cast {}", key.c_str());
          continue;
        }
        auto deserializedModel = Model::Deserialize(std::get<flutter::EncodableMap>(iter));
        if (deserializedModel == nullptr) {
          spdlog::error("Unable to load model");
          continue;
        }
        models_.emplace_back(std::move(deserializedModel));
      }

      spdlog::debug("Deserialized {} models", models_.size());
    } else if (key == kScene) {
      spdlog::debug("===== Deserializing Scene {} ...", key);
      DeserializeSceneLevel(snd);
    } else if (key == kShapes && std::holds_alternative<flutter::EncodableList>(snd)) {
      auto list = std::get<flutter::EncodableList>(snd);

      spdlog::debug("===== Deserializing multiple Shapes {} ...", key);
      for (const auto& iter : list) {
        if (iter.IsNull()) {
          SPDLOG_DEBUG("CreationParamName unable to cast {}", key.c_str());
          continue;
        }

        auto shapeEntity = std::make_shared<EntityObject>(std::get<flutter::EncodableMap>(iter));

        // Deserialize material component
        shapeEntity->addComponent(Material(std::get<flutter::EncodableMap>(iter)));

        // Deserialize transform component
        shapeEntity->addComponent(Transform(std::get<flutter::EncodableMap>(iter)));

        // Deserialize shape component
        shapeEntity->addComponent(Shape(std::get<flutter::EncodableMap>(iter)));

        // Deserialize collider component if exists
        shapeEntity->addComponent(Collider(std::get<flutter::EncodableMap>(iter)));

        entities_.emplace_back(std::move(shapeEntity));
      }

      spdlog::debug("Deserialized {} shapes", shapes_.size());
    } else if (key == kCameras) {
      spdlog::debug("===== Deserializing Cameras {} ...", key);
      auto list = std::get<flutter::EncodableList>(snd);

      for (const auto& iter : list) {
        if (iter.IsNull()) {
          spdlog::warn("CreationParamName unable to cast {}", key.c_str());
          continue;
        }

        // Deserialize entity
        auto cameraEntity = std::make_shared<EntityObject>(std::get<flutter::EncodableMap>(iter));

        // Deserialize transform component
        cameraEntity->addComponent(Transform(std::get<flutter::EncodableMap>(iter)));

        // Deserialize camera component
        cameraEntity->addComponent(Camera(std::get<flutter::EncodableMap>(iter)));

        entities_.emplace_back(std::move(cameraEntity));
      }

      spdlog::debug("Deserialized {} cameras", entities_.size());
    } else if (key == kSkybox) {
      skybox_ = Skybox::Deserialize(std::get<flutter::EncodableMap>(snd));
    } else if (key == kIndirectLight) {
      indirect_light_ = IndirectLight::Deserialize(std::get<flutter::EncodableMap>(snd));
    } else {
      spdlog::warn("[SceneTextDeserializer] Unhandled Parameter {}", key.c_str());
      plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(), snd);
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::DeserializeSceneLevel(const flutter::EncodableValue& params) {
  for (const auto& [fst, snd] : std::get<flutter::EncodableMap>(params)) {
    auto key = std::get<std::string>(fst);
    if (snd.IsNull()) {
      SPDLOG_WARN("DeserializeSceneLevel Param ITER is null key:{} function:{}", key, __FUNCTION__);
      continue;
    }

    if (key == kLights && std::holds_alternative<flutter::EncodableList>(snd)) {
      auto list = std::get<flutter::EncodableList>(snd);
      for (const auto& iter : list) {
        if (iter.IsNull()) {
          spdlog::warn("CreationParamName unable to cast {}", key.c_str());
          continue;
        }

        auto encodableMap = std::get<flutter::EncodableMap>(iter);

        // This will get placed on an entity
        EntityGUID overWriteGuid;
        Deserialize::DecodeParameterWithDefaultInt64(
          kGuid, &overWriteGuid, encodableMap, kNullGuid
        );

        if (overWriteGuid == kNullGuid) {
          spdlog::warn("Light is missing a GUID, will not add to scene");
          continue;
        }

        lights_.insert(std::pair(overWriteGuid, std::make_shared<Light>(encodableMap)));
      }

      continue;
    }

    // everything in this loop looks to make sure its a map, we can continue
    // on if its not.
    if (!std::holds_alternative<flutter::EncodableMap>(snd)) {
      continue;
    }

    auto encodableMap = std::get<flutter::EncodableMap>(snd);

    SPDLOG_DEBUG("KEY {} ", key);

    if (key == kSkybox) {
      skybox_ = Skybox::Deserialize(encodableMap);
    } else if (key == kIndirectLight) {
      indirect_light_ = IndirectLight::Deserialize(encodableMap);
    } else if (!snd.IsNull()) {
      spdlog::debug("[SceneTextDeserializer] Unhandled Parameter {}", key.c_str());
      plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(), snd);
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::RunPostSetupLoad() {
  spdlog::debug("Running post setup load...");

  spdlog::trace("setUpLoadingModels");
  setUpLoadingModels();
  spdlog::trace("setUpSkybox");
  setUpSkybox();
  spdlog::trace("setUpLights");
  setUpLights();
  spdlog::trace("setUpIndirectLight");
  setUpIndirectLight();
  spdlog::trace("setUpShapes");
  setUpShapes();
  spdlog::trace("setUpEntities");
  setUpEntities();

  spdlog::debug("setups done!");

  indirect_light_.reset();
  skybox_.reset();
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::setUpLoadingModels() {
  SPDLOG_TRACE("++{}", __FUNCTION__);

  for (auto& iter : models_) {
    // Note: Instancing or prefab of models is not currently supported but might
    // affect the loading process here in the future. Backlogged.
    //
    // This will transfer ownership
    loadModel(iter);
  }

  SPDLOG_TRACE("--{}", __FUNCTION__);
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::setUpShapes() {
  spdlog::debug("{} {}", __FUNCTION__, __LINE__);

  spdlog::debug("getting systems");
  const auto shapeSystem = _ecs->getSystem<ShapeSystem>("setUpShapes");
  const auto collisionSystem = _ecs->getSystem<CollisionSystem>("setUpShapes");

  if (shapeSystem == nullptr || collisionSystem == nullptr) {
    throw std::runtime_error("[SceneTextDeserializer] Error.ShapeSystem or collisionSystem is null"
    );
  }

  for (const auto& shape : shapes_) {
    spdlog::trace("Adding shape to scene {}", shape->getGuid());
    _ecs->addEntity(shape);
    /// TODO: fix shape colliders
    // spdlog::trace("Adding collider...");
    // if (shape->hasComponent<Collider>()) {
    //   spdlog::trace("Shape {} has collider! Adding to collision system",
    //   shape->getGuid()); if (collisionSystem != nullptr) {
    //     collisionSystem->AddCollidable(shape.get());
    //   }
    // }
    // spdlog::trace("Collider added!");
  }

  spdlog::debug("Shape setup done, adding to scene");

  shapeSystem->addShapesToScene(&shapes_);

  shapes_.clear();
}

void SceneTextDeserializer::setUpEntities() {
  spdlog::debug("{} {}", __FUNCTION__, __LINE__);

  const auto viewTargetSystem = _ecs->getSystem<ViewTargetSystem>("setUpEntities");

  // For each entity
  for (const auto& entity : entities_) {
    const auto entityGuid = entity->getGuid();
    // Add the entity to the ECS
    spdlog::trace("Adding entity '{}'({}) to ECS", entity->name, entityGuid);
    _ecs->addEntity(entity);

    // If camera, use ViewTargetSystem to set it up
    if (_ecs->hasComponent<Camera>(entityGuid)) {
      viewTargetSystem->initializeEntity(entityGuid);
      spdlog::trace("Camera '{}'({}) initialized", entity->name, entityGuid);
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::loadModel(std::shared_ptr<Model>& model) {
  const auto& strand = *_ecs->getStrand();

  post(strand, [model = std::move(model)]() mutable {
    const auto modelSystem = ECSManager::GetInstance()->getSystem<ModelSystem>("loadModel");

    if (modelSystem == nullptr) {
      spdlog::error("Unable to find the model system.");
      return;
    }

    const Model* glb_model = dynamic_cast<Model*>(model.get());
    if (!glb_model->getAssetPath().empty()) {
      modelSystem->queueModelLoad(std::move(model));
    } else {
      spdlog::error("Model has no asset path, unable to load");
    }

    spdlog::trace("[loadModel] Model {} queued for loading", glb_model->getAssetPath());
  });
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::setUpSkybox() const {
  // Todo move to a message.

  auto skyboxSystem = ECSManager::GetInstance()->getSystem<SkyboxSystem>(__FUNCTION__);

  if (!skybox_) {
    skyboxSystem->setDefaultSkybox();
    // makeSurfaceViewTransparent();
  } else {
    if (const auto skybox = skybox_.get(); dynamic_cast<HdrSkybox*>(skybox)) {
      if (const auto hdr_skybox = dynamic_cast<HdrSkybox*>(skybox);
          !hdr_skybox->getAssetPath().empty()) {
        const auto shouldUpdateLight = hdr_skybox->getAssetPath()
                                       == indirect_light_->getAssetPath();

        skyboxSystem->setSkyboxFromHdrAsset(
          hdr_skybox->getAssetPath(), hdr_skybox->getShowSun(), shouldUpdateLight,
          indirect_light_->getIntensity()
        );
      } else if (!skybox->getUrl().empty()) {
        const auto shouldUpdateLight = hdr_skybox->szGetURLPath() == indirect_light_->getUrl();
        skyboxSystem->setSkyboxFromHdrUrl(
          hdr_skybox->szGetURLPath(), hdr_skybox->getShowSun(), shouldUpdateLight,
          indirect_light_->getIntensity()
        );
      }
    } else if (dynamic_cast<KxtSkybox*>(skybox)) {
      if (const auto kxt_skybox = dynamic_cast<KxtSkybox*>(skybox);
          !kxt_skybox->getAssetPath().empty()) {
        skyboxSystem->setSkyboxFromKTXAsset(kxt_skybox->getAssetPath());
      } else if (!kxt_skybox->szGetURLPath().empty()) {
        skyboxSystem->setSkyboxFromKTXUrl(kxt_skybox->szGetURLPath());
      }
    } else if (dynamic_cast<ColorSkybox*>(skybox)) {
      if (const auto color_skybox = dynamic_cast<ColorSkybox*>(skybox);
          !color_skybox->szGetColor().empty()) {
        skyboxSystem->setSkyboxFromColor(color_skybox->szGetColor());
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::setUpLights() {
  const auto lightSystem = _ecs->getSystem<LightSystem>(__FUNCTION__);

  // Note, this introduces a fire and forget functionality for entities
  // there's no "one" owner system, but its propagated to whomever cares for it.
  for (const auto& [fst, snd] : lights_) {
    const auto newEntity = std::make_shared<EntityObject>(
      "SceneTextDeserializer::setUpLights", fst
    );

    _ecs->addEntity(newEntity);
    _ecs->addComponent(newEntity->getGuid(), snd);

    lightSystem->BuildLightAndAddToScene(*snd);
  }

  // if a light didnt get deserialized, tell light system to create a default
  // one.
  if (lights_.empty()) {
    SPDLOG_DEBUG("No lights found, creating default light");
    lightSystem->CreateDefaultLight();
  }

  lights_.clear();
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::setUpIndirectLight() const {
  // Todo move to a message.
  auto indirectlightSystem = ECSManager::GetInstance()->getSystem<IndirectLightSystem>(__FUNCTION__
  );

  if (!indirect_light_) {
    // This was called in the constructor of indirectLightManager_ anyway.
    // plugin_filament_view::indirectlightSystem->setDefaultIndirectLight();
  } else {
    if (const auto indirectLight = indirect_light_.get();
        dynamic_cast<KtxIndirectLight*>(indirectLight)) {
      if (!indirectLight->getAssetPath().empty()) {
        indirectlightSystem->setIndirectLightFromKtxAsset(
          indirectLight->getAssetPath(), indirectLight->getIntensity()
        );
      } else if (!indirectLight->getUrl().empty()) {
        indirectlightSystem->setIndirectLightFromKtxUrl(
          indirectLight->getAssetPath(), indirectLight->getIntensity()
        );
      }
    } else if (dynamic_cast<HdrIndirectLight*>(indirectLight)) {
      if (!indirectLight->getAssetPath().empty()) {
        // val shouldUpdateLight = indirectLight->getAssetPath() !=
        // scene?.skybox?.assetPath if (shouldUpdateLight) {
        indirectlightSystem->setIndirectLightFromHdrAsset(
          indirectLight->getAssetPath(), indirectLight->getIntensity()
        );
        //}

      } else if (!indirectLight->getUrl().empty()) {
        // auto shouldUpdateLight = indirectLight->getUrl() !=
        // scene?.skybox?.url;
        //  if (shouldUpdateLight) {
        indirectlightSystem->setIndirectLightFromHdrUrl(
          indirectLight->getUrl(), indirectLight->getIntensity()
        );
        //}
      }
    } else if (dynamic_cast<DefaultIndirectLight*>(indirectLight)) {
      indirectlightSystem->setIndirectLight(dynamic_cast<DefaultIndirectLight*>(indirectLight));
    } else {
      // Already called in the default constructor.
      // plugin_filament_view::indirectlightSystem->setDefaultIndirectLight();
    }
  }
}

}  // namespace plugin_filament_view
