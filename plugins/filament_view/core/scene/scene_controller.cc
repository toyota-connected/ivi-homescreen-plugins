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

#include "scene_controller.h"

#include <core/systems/ecsystems_manager.h>
#include <core/utils/entitytransforms.h>
#include <asio/post.hpp>
#include <utility>

#include <core/systems/derived/collision_system.h>
#include <core/systems/derived/indirect_light_system.h>
#include <core/systems/derived/light_system.h>
#include <core/systems/derived/model_system.h>
#include <core/systems/derived/skybox_system.h>

#include "core/systems/derived/filament_system.h"
#include "core/systems/derived/view_target_system.h"

#include "plugins/common/common.h"

namespace plugin_filament_view {

SceneController::SceneController(
    std::unique_ptr<std::vector<std::unique_ptr<Model>>> models,
    Scene* scene,
    std::unique_ptr<std::vector<std::unique_ptr<shapes::BaseShape>>> shapes,
    int32_t id)
    : id_(id),
      models_(std::move(models)),
      scene_(scene),
      shapes_(std::move(shapes)) {
  SPDLOG_TRACE("{}::{}::{}", __FILE__, __FUNCTION__, id_);
}

void SceneController::vRunPostSetupLoad() {
  // These setups to all be moved
  setUpLoadingModels();
  setUpSkybox();
  setUpLight();
  setUpIndirectLight();
  setUpShapes(shapes_.get());

  ECSMessage viewTargetCameraSet;
  viewTargetCameraSet.addData(ECSMessageType::SetCameraFromDeserializedLoad,
                              scene_->camera_.get());
  ECSystemManager::GetInstance()->vRouteMessage(viewTargetCameraSet);
}

SceneController::~SceneController() {
  SPDLOG_TRACE("SceneController::~SceneController");
}

void SceneController::setUpSkybox() {
  // Todo move to a message.

  auto skyboxSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<SkyboxSystem>(
          SkyboxSystem::StaticGetTypeID(), __FUNCTION__);

  if (!scene_->skybox_) {
    SkyboxSystem::setDefaultSkybox();
    // makeSurfaceViewTransparent();
  } else {
    auto skybox = scene_->skybox_.get();
    if (dynamic_cast<HdrSkybox*>(skybox)) {
      auto hdr_skybox = dynamic_cast<HdrSkybox*>(skybox);
      if (!hdr_skybox->assetPath_.empty()) {
        auto shouldUpdateLight =
            hdr_skybox->assetPath_ == scene_->indirect_light_->getAssetPath();
        SkyboxSystem::setSkyboxFromHdrAsset(
            hdr_skybox->assetPath_, hdr_skybox->showSun_, shouldUpdateLight,
            scene_->indirect_light_->getIntensity());
      } else if (!skybox->getUrl().empty()) {
        auto shouldUpdateLight =
            hdr_skybox->url_ == scene_->indirect_light_->getUrl();
        SkyboxSystem::setSkyboxFromHdrUrl(
            hdr_skybox->url_, hdr_skybox->showSun_, shouldUpdateLight,
            scene_->indirect_light_->getIntensity());
      }
    } else if (dynamic_cast<KxtSkybox*>(skybox)) {
      auto kxt_skybox = dynamic_cast<KxtSkybox*>(skybox);
      if (!kxt_skybox->assetPath_.empty()) {
        SkyboxSystem::setSkyboxFromKTXAsset(kxt_skybox->assetPath_);
      } else if (!kxt_skybox->url_.empty()) {
        SkyboxSystem::setSkyboxFromKTXUrl(kxt_skybox->url_);
      }
    } else if (dynamic_cast<ColorSkybox*>(skybox)) {
      auto color_skybox = dynamic_cast<ColorSkybox*>(skybox);
      if (!color_skybox->color_.empty()) {
        SkyboxSystem::setSkyboxFromColor(color_skybox->color_);
      }
    }
  }
}

void SceneController::setUpLight() {
  // Todo move to a message.

  auto lightSystem = ECSystemManager::GetInstance()->poGetSystemAs<LightSystem>(
      LightSystem::StaticGetTypeID(), __FUNCTION__);

  if (scene_ && scene_->light_) {
    lightSystem->changeLight(scene_->light_.get());
  } else {
    lightSystem->setDefaultLight();
  }
}

void SceneController::ChangeLightProperties(int /*nWhichLightIndex*/,
                                            const std::string& colorValue,
                                            int32_t intensity) {
  // Todo move to a message.

  if (scene_ && scene_->light_) {
    SPDLOG_TRACE("Changing light values. {} {}", __FILE__, __FUNCTION__);

    auto lightSystem =
        ECSystemManager::GetInstance()->poGetSystemAs<LightSystem>(
            LightSystem::StaticGetTypeID(), __FUNCTION__);

    scene_->light_->ChangeColor(colorValue);
    scene_->light_->ChangeIntensity(static_cast<float>(intensity));

    lightSystem->changeLight(scene_->light_.get());
    return;
  }

  SPDLOG_WARN("Not implemented {} {}", __FILE__, __FUNCTION__);
}

void SceneController::ChangeIndirectLightProperties(int32_t intensity) {
  // Todo move to a message.

  auto indirectLight = scene_->indirect_light_.get();
  indirectLight->setIntensity(static_cast<float>(intensity));
  indirectLight->Print("SceneController ChangeIndirectLightProperties");

  auto indirectlightSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<IndirectLightSystem>(
          IndirectLightSystem::StaticGetTypeID(), __FUNCTION__);

  IndirectLightSystem::setIndirectLight(
      dynamic_cast<DefaultIndirectLight*>(indirectLight));
}

void SceneController::setUpIndirectLight() {
  // Todo move to a message.
  auto indirectlightSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<IndirectLightSystem>(
          IndirectLightSystem::StaticGetTypeID(), __FUNCTION__);

  if (!scene_->indirect_light_) {
    // This was called in the constructor of indirectLightManager_ anyway.
    // plugin_filament_view::IndirectLightSystem::setDefaultIndirectLight();
  } else {
    auto indirectLight = scene_->indirect_light_.get();
    if (dynamic_cast<KtxIndirectLight*>(indirectLight)) {
      if (!indirectLight->getAssetPath().empty()) {
        plugin_filament_view::IndirectLightSystem::setIndirectLightFromKtxAsset(
            indirectLight->getAssetPath(), indirectLight->getIntensity());
      } else if (!indirectLight->getUrl().empty()) {
        plugin_filament_view::IndirectLightSystem::setIndirectLightFromKtxUrl(
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
        plugin_filament_view::IndirectLightSystem::setIndirectLightFromHdrUrl(
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

void SceneController::setUpAnimation(std::optional<Animation*> animation) {
  if (animation.has_value()) {
    auto a = animation.value();

    if (a == nullptr)
      return;

    if (a->GetAutoPlay()) {
      if (a->GetIndex().has_value()) {
        currentAnimationIndex_ = a->GetIndex();
      } else if (!a->GetName().empty()) {
        currentAnimationIndex_ =
            0;  //
                // Todo / to be implemented always returned 0.
                // animationManager_->getAnimationIndexByName(a->GetName());
      }
    }
  } else {
    currentAnimationIndex_ = std::nullopt;
  }
}

void SceneController::setUpLoadingModels() {
  SPDLOG_TRACE("++{}::{}", __FILE__, __FUNCTION__);
  animationManager_ = std::make_unique<AnimationManager>();

  for (const auto& iter : *models_) {
    plugin_filament_view::Model* poCurrModel = iter.get();
    // TODO loadModel needs to save the model internally in the map that's
    // there. backlogged.
    loadModel(poCurrModel);
  }

  SPDLOG_TRACE("--{}::{}", __FILE__, __FUNCTION__);
}

void SceneController::setUpShapes(
    std::vector<std::unique_ptr<shapes::BaseShape>>* shapes) {
  SPDLOG_TRACE("{} {}", __FUNCTION__, __LINE__);

  auto shapeSystem = ECSystemManager::GetInstance()->poGetSystemAs<ShapeSystem>(
      ShapeSystem::StaticGetTypeID(), "setUpShapes");
  auto collisionSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<CollisionSystem>(
          CollisionSystem::StaticGetTypeID(), "setUpShapes");

  if (shapeSystem == nullptr || collisionSystem == nullptr) {
    spdlog::error(
        "[SceneController] Error.ShapeSystem or collisionSystem is null");
    return;
  }

  for (const auto& shape : *shapes) {
    if (shape->HasComponentByStaticTypeID(Collidable::StaticGetTypeID())) {
      if (collisionSystem != nullptr) {
        collisionSystem->vAddCollidable(shape.get());
      }
    }
  }

  // This method releases shapes,
  shapeSystem->addShapesToScene(shapes);
}

void SceneController::vToggleAllShapesInScene(bool bValue) {
  auto shapeSystem = ECSystemManager::GetInstance()->poGetSystemAs<ShapeSystem>(
      ShapeSystem::StaticGetTypeID(), "setUpShapes");
  if (shapeSystem == nullptr) {
    SPDLOG_WARN("{} called before shapeManager created.", __FUNCTION__);
    return;
  }

  // Could become a message
  shapeSystem->vToggleAllShapesInScene(bValue);
}

void SceneController::loadModel(Model* model) {
  auto ecsManager = ECSystemManager::GetInstance();
  const auto& strand = *ecsManager->GetStrand();

  asio::post(strand, [=]() {
    auto modelSystem =
        ECSystemManager::GetInstance()->poGetSystemAs<ModelSystem>(
            ModelSystem::StaticGetTypeID(), "loadModel");

    if (modelSystem == nullptr) {
      spdlog::error("Unable to find the model system.");
      /*return Resource<std::string_view>::Error(
          "Unable to find the model system.");*/
    }

    const auto& loader = modelSystem;
    if (dynamic_cast<GlbModel*>(model)) {
      auto glb_model = dynamic_cast<GlbModel*>(model);
      if (!glb_model->assetPath_.empty()) {
        loader->loadGlbFromAsset(model, glb_model->assetPath_, false);
      }

      if (!glb_model->url_.empty()) {
        loader->loadGlbFromUrl(model, glb_model->url_);
      }
    } else if (dynamic_cast<GltfModel*>(model)) {
      auto gltf_model = dynamic_cast<GltfModel*>(model);
      if (!gltf_model->assetPath_.empty()) {
        plugin_filament_view::ModelSystem::loadGltfFromAsset(
            model, gltf_model->assetPath_, gltf_model->pathPrefix_,
            gltf_model->pathPostfix_);
      }

      if (!gltf_model->url_.empty()) {
        plugin_filament_view::ModelSystem::loadGltfFromUrl(model,
                                                           gltf_model->url_);
      }
    }
  });
}

}  // namespace plugin_filament_view