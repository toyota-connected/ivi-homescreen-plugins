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

#include <core/include/literals.h>
#include <core/systems/derived/collision_system.h>
#include <core/systems/derived/indirect_light_system.h>
#include <core/systems/derived/light_system.h>
#include <core/systems/derived/model_system.h>
#include <core/systems/derived/shape_system.h>
#include <core/systems/derived/skybox_system.h>
#include <core/systems/ecsystems_manager.h>
#include <core/utils/deserialize.h>
#include <plugins/common/common.h>
#include <asio/post.hpp>

#include "shell/platform/common/client_wrapper/include/flutter/standard_message_codec.h"

namespace plugin_filament_view {

//////////////////////////////////////////////////////////////////////////////////////////
SceneTextDeserializer::SceneTextDeserializer(
    const std::vector<uint8_t>& params) {
  const auto ecsManager = ECSystemManager::GetInstance();
  const std::string& flutterAssetsPath =
      ecsManager->getConfigValue<std::string>(kAssetPath);

  // kick off process...
  vDeserializeRootLevel(params, flutterAssetsPath);
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::vDeserializeRootLevel(
    const std::vector<uint8_t>& params,
    const std::string& flutterAssetsPath) {
  auto& codec = flutter::StandardMessageCodec::GetInstance();
  const auto decoded = codec.DecodeMessage(params.data(), params.size());
  const auto& creationParams =
      std::get_if<flutter::EncodableMap>(decoded.get());

  for (const auto& [fst, snd] : *creationParams) {
    auto key = std::get<std::string>(fst);
    if (snd.IsNull()) {
      SPDLOG_DEBUG("vDeserializeRootLevel ITER is null {} {} {}", key.c_str(),
                   __FILE__, __FUNCTION__);
      continue;
    }

    if (key == kModel) {
      spdlog::warn("Loading Single Model - Deprecated Functionality {}", key);

      auto deserializedModel = Model::Deserialize(
          flutterAssetsPath, std::get<flutter::EncodableMap>(snd));
      if (deserializedModel == nullptr) {
        // load fallback
        auto fallbackToDeserialize =
            Deserialize::DeserializeParameter(kFallback, snd);
        deserializedModel = Model::Deserialize(
            flutterAssetsPath,
            std::get<flutter::EncodableMap>(fallbackToDeserialize));
      }
      if (deserializedModel == nullptr) {
        spdlog::error("Unable to load model and fallback model");
        continue;
      }
      models_.emplace_back(std::move(deserializedModel));
    } else if (key == kModels &&
               std::holds_alternative<flutter::EncodableList>(snd)) {
      SPDLOG_TRACE("Loading Multiple Models {}", key);

      auto list = std::get<flutter::EncodableList>(snd);
      for (const auto& iter : list) {
        if (iter.IsNull()) {
          spdlog::warn("CreationParamName unable to cast {}", key.c_str());
          continue;
        }

        auto deserializedModel = Model::Deserialize(
            flutterAssetsPath, std::get<flutter::EncodableMap>(iter));
        if (deserializedModel == nullptr) {
          // load fallback
          auto fallbackToDeserialize =
              Deserialize::DeserializeParameter(kFallback, iter);
          deserializedModel = Model::Deserialize(
              flutterAssetsPath,
              std::get<flutter::EncodableMap>(fallbackToDeserialize));
        }
        if (deserializedModel == nullptr) {
          spdlog::error("Unable to load model and fallback model");
          continue;
        }
        models_.emplace_back(std::move(deserializedModel));
      }

    } else if (key == kScene) {
      vDeserializeSceneLevel(snd, flutterAssetsPath);
    } else if (key == kShapes &&
               std::holds_alternative<flutter::EncodableList>(snd)) {
      auto list = std::get<flutter::EncodableList>(snd);

      for (const auto& iter : list) {
        if (iter.IsNull()) {
          SPDLOG_DEBUG("CreationParamName unable to cast {}", key.c_str());
          continue;
        }
        auto shape = ShapeSystem::poDeserializeShapeFromData(
            flutterAssetsPath, std::get<flutter::EncodableMap>(iter));

        shapes_.emplace_back(shape.release());
      }
    } else {
      spdlog::warn("[SceneTextDeserializer] Unhandled Parameter {}",
                   key.c_str());
      plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(), snd);
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::vDeserializeSceneLevel(
    const flutter::EncodableValue& params,
    const std::string& /*flutterAssetsPath*/) {
  for (const auto& [fst, snd] : std::get<flutter::EncodableMap>(params)) {
    auto key = std::get<std::string>(fst);
    if (snd.IsNull()) {
      SPDLOG_WARN(
          "vDeserializeSceneLevel Param ITER is null key:{} file:{} "
          "function:{}",
          key, __FILE__, __FUNCTION__);
      continue;
    }

    // everything in this loop looks to make sure its a map, we can continue
    // on if its not.
    if (!std::holds_alternative<flutter::EncodableMap>(snd)) {
      continue;
    }

    auto encodableMap = std::get<flutter::EncodableMap>(snd);

    if (key == kSkybox) {
      skybox_ = Skybox::Deserialize(encodableMap);
    } else if (key == kLight) {
      lights_.emplace_back(std::make_unique<Light>(encodableMap));
    } else if (key == kIndirectLight) {
      indirect_light_ = IndirectLight::Deserialize(encodableMap);
    } else if (key == kCamera) {
      camera_ = std::make_unique<Camera>(encodableMap);
    } else if (key == "ground") {
      spdlog::warn(
          "Specifying a ground is no longer supporting, a ground is now a "
          "plane in shapes.");
    } else if (!snd.IsNull()) {
      spdlog::debug("[SceneTextDeserializer] Unhandled Parameter {}",
                    key.c_str());
      plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(), snd);
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::vRunPostSetupLoad() {
  setUpLoadingModels();
  setUpSkybox();
  setUpLight();
  setUpIndirectLight();
  setUpShapes();

  ECSMessage viewTargetCameraSet;
  viewTargetCameraSet.addData(ECSMessageType::SetCameraFromDeserializedLoad,
                              camera_.get());
  ECSystemManager::GetInstance()->vRouteMessage(viewTargetCameraSet);
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::setUpLoadingModels() {
  SPDLOG_TRACE("++{}::{}", __FILE__, __FUNCTION__);
  // animationManager_ = std::make_unique<AnimationManager>();

  for (const auto& iter : models_) {
    Model* poCurrModel = iter.get();
    // Note: Instancing or prefab of models is not currently supported but might
    // affect the loading process here in the future. Backlogged.
    loadModel(poCurrModel);
  }

  SPDLOG_TRACE("--{}::{}", __FILE__, __FUNCTION__);
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::setUpShapes() {
  SPDLOG_TRACE("{} {}", __FUNCTION__, __LINE__);

  const auto shapeSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<ShapeSystem>(
          ShapeSystem::StaticGetTypeID(), "setUpShapes");
  const auto collisionSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<CollisionSystem>(
          CollisionSystem::StaticGetTypeID(), "setUpShapes");

  if (shapeSystem == nullptr || collisionSystem == nullptr) {
    spdlog::error(
        "[SceneTextDeserializer] Error.ShapeSystem or collisionSystem is null");
    return;
  }

  for (const auto& shape : shapes_) {
    if (shape->HasComponentByStaticTypeID(Collidable::StaticGetTypeID())) {
      if (collisionSystem != nullptr) {
        collisionSystem->vAddCollidable(shape.get());
      }
    }
  }

  // This method releases shapes,
  shapeSystem->addShapesToScene(&shapes_);
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::loadModel(Model* model) {
  const auto ecsManager = ECSystemManager::GetInstance();
  const auto& strand = *ecsManager->GetStrand();

  post(strand, [=] {
    const auto modelSystem =
        ECSystemManager::GetInstance()->poGetSystemAs<ModelSystem>(
            ModelSystem::StaticGetTypeID(), "loadModel");

    if (modelSystem == nullptr) {
      spdlog::error("Unable to find the model system.");
      /*return Resource<std::string_view>::Error(
          "Unable to find the model system.");*/
    }

    const auto& loader = modelSystem;
    if (dynamic_cast<GlbModel*>(model)) {
      const auto glb_model = dynamic_cast<GlbModel*>(model);
      if (!glb_model->szGetAssetPath().empty()) {
        loader->loadGlbFromAsset(model, glb_model->szGetAssetPath(), false);
      }

      if (!glb_model->szGetURLPath().empty()) {
        loader->loadGlbFromUrl(model, glb_model->szGetURLPath());
      }
    } else if (dynamic_cast<GltfModel*>(model)) {
      const auto gltf_model = dynamic_cast<GltfModel*>(model);
      if (!gltf_model->szGetAssetPath().empty()) {
        ModelSystem::loadGltfFromAsset(model, gltf_model->szGetAssetPath(),
                                       gltf_model->szGetPrefix(),
                                       gltf_model->szGetPostfix());
      }

      if (!gltf_model->szGetURLPath().empty()) {
        ModelSystem::loadGltfFromUrl(model, gltf_model->szGetURLPath());
      }
    }
  });
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::setUpSkybox() {
  // Todo move to a message.

  auto skyboxSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<SkyboxSystem>(
          SkyboxSystem::StaticGetTypeID(), __FUNCTION__);

  if (!skybox_) {
    SkyboxSystem::setDefaultSkybox();
    // makeSurfaceViewTransparent();
  } else {
    if (const auto skybox = skybox_.get(); dynamic_cast<HdrSkybox*>(skybox)) {
      if (const auto hdr_skybox = dynamic_cast<HdrSkybox*>(skybox);
          !hdr_skybox->szGetAssetPath().empty()) {
        const auto shouldUpdateLight =
            hdr_skybox->szGetAssetPath() == indirect_light_->getAssetPath();
        SkyboxSystem::setSkyboxFromHdrAsset(
            hdr_skybox->szGetAssetPath(), hdr_skybox->getShowSun(),
            shouldUpdateLight, indirect_light_->getIntensity());
      } else if (!skybox->getUrl().empty()) {
        const auto shouldUpdateLight =
            hdr_skybox->szGetURLPath() == indirect_light_->getUrl();
        SkyboxSystem::setSkyboxFromHdrUrl(
            hdr_skybox->szGetURLPath(), hdr_skybox->getShowSun(),
            shouldUpdateLight, indirect_light_->getIntensity());
      }
    } else if (dynamic_cast<KxtSkybox*>(skybox)) {
      if (const auto kxt_skybox = dynamic_cast<KxtSkybox*>(skybox);
          !kxt_skybox->szGetAssetPath().empty()) {
        SkyboxSystem::setSkyboxFromKTXAsset(kxt_skybox->szGetAssetPath());
      } else if (!kxt_skybox->szGetURLPath().empty()) {
        SkyboxSystem::setSkyboxFromKTXUrl(kxt_skybox->szGetURLPath());
      }
    } else if (dynamic_cast<ColorSkybox*>(skybox)) {
      if (const auto color_skybox = dynamic_cast<ColorSkybox*>(skybox);
          !color_skybox->szGetColor().empty()) {
        SkyboxSystem::setSkyboxFromColor(color_skybox->szGetColor());
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::setUpLight() {
  // Todo move to a message.

  const auto lightSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<LightSystem>(
          LightSystem::StaticGetTypeID(), __FUNCTION__);

  // TODO make sure this is copied over
  if (!lights_.empty()) {
    lightSystem->changeLight(lights_[0].get());
  } else {
    lightSystem->setDefaultLight();
  }
}

//////////////////////////////////////////////////////////////////////////////////////////
void SceneTextDeserializer::setUpIndirectLight() {
  // Todo move to a message.
  auto indirectlightSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<IndirectLightSystem>(
          IndirectLightSystem::StaticGetTypeID(), __FUNCTION__);

  if (!indirect_light_) {
    // This was called in the constructor of indirectLightManager_ anyway.
    // plugin_filament_view::IndirectLightSystem::setDefaultIndirectLight();
  } else {
    if (const auto indirectLight = indirect_light_.get();
        dynamic_cast<KtxIndirectLight*>(indirectLight)) {
      if (!indirectLight->getAssetPath().empty()) {
        IndirectLightSystem::setIndirectLightFromKtxAsset(
            indirectLight->getAssetPath(), indirectLight->getIntensity());
      } else if (!indirectLight->getUrl().empty()) {
        IndirectLightSystem::setIndirectLightFromKtxUrl(
            indirectLight->getAssetPath(), indirectLight->getIntensity());
      }
    } else if (dynamic_cast<HdrIndirectLight*>(indirectLight)) {
      if (!indirectLight->getAssetPath().empty()) {
        // val shouldUpdateLight = indirectLight->getAssetPath() !=
        // scene?.skybox?.assetPath if (shouldUpdateLight) {
        IndirectLightSystem::setIndirectLightFromHdrAsset(
            indirectLight->getAssetPath(), indirectLight->getIntensity());
        //}

      } else if (!indirectLight->getUrl().empty()) {
        // auto shouldUpdateLight = indirectLight->getUrl() !=
        // scene?.skybox?.url;
        //  if (shouldUpdateLight) {
        IndirectLightSystem::setIndirectLightFromHdrUrl(
            indirectLight->getUrl(), indirectLight->getIntensity());
        //}
      }
    } else if (dynamic_cast<DefaultIndirectLight*>(indirectLight)) {
      IndirectLightSystem::setIndirectLight(
          dynamic_cast<DefaultIndirectLight*>(indirectLight));
    } else {
      // Already called in the default constructor.
      // plugin_filament_view::IndirectLightSystem::setDefaultIndirectLight();
    }
  }
}

}  // namespace plugin_filament_view