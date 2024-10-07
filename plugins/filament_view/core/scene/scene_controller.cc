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
#include "core/systems/derived/filament_system.h"

#include "plugins/common/common.h"

namespace plugin_filament_view {

SceneController::SceneController(
    PlatformView* platformView,
    FlutterDesktopEngineState* state,
    std::string flutterAssetsPath,
    std::unique_ptr<std::vector<std::unique_ptr<Model>>> models,
    Scene* scene,
    std::unique_ptr<std::vector<std::unique_ptr<shapes::BaseShape>>> shapes,
    int32_t id)
    : id_(id),
      flutterAssetsPath_(std::move(flutterAssetsPath)),
      models_(std::move(models)),
      scene_(scene),
      shapes_(std::move(shapes)) {
  SPDLOG_TRACE("++{} {}", __FILE__, __FUNCTION__);

  spdlog::info("SceneController {} setup", id_);

  SPDLOG_INFO("ALLEN DELETE: {} {}", __FUNCTION__, __LINE__);

  setUpViewer(platformView, state);

  setUpIblProfiler();
  SPDLOG_TRACE("--{} {}", __FILE__, __FUNCTION__);
}

void SceneController::vRunPostSetupLoad() {

  auto filamentSystem =
       ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
           FilamentSystem::StaticGetTypeID(), __FUNCTION__);

  auto view = filamentSystem->getFilamentView();
  auto scene = filamentSystem->getFilamentScene();
  SPDLOG_INFO("ALLEN DELETE: {} {}", __FUNCTION__, __LINE__);

  // auto size = platformView->GetSize();
  //  todo.
  view->setViewport({0, 0, 800, 600});

  view->setScene(scene);
  SPDLOG_INFO("ALLEN DELETE: {} {}", __FUNCTION__, __LINE__);

  // TODO this may need to be turned off for target
  view->setPostProcessingEnabled(true);

  SPDLOG_INFO("ALLEN DELETE: {} {}", __FUNCTION__, __LINE__);

  setUpLoadingModels();
  SPDLOG_INFO("ALLEN DELETE: {} {}", __FUNCTION__, __LINE__);

  setUpCamera();
  SPDLOG_INFO("ALLEN DELETE: {} {}", __FUNCTION__, __LINE__);

  setUpSkybox();
  SPDLOG_INFO("ALLEN DELETE: {} {}", __FUNCTION__, __LINE__);

  setUpLight();
  SPDLOG_INFO("ALLEN DELETE: {} {}", __FUNCTION__, __LINE__);

  setUpIndirectLight();
  SPDLOG_INFO("ALLEN DELETE: {} {}", __FUNCTION__, __LINE__);

  setUpShapes(shapes_.get());
  SPDLOG_INFO("ALLEN DELETE: {} {}", __FUNCTION__, __LINE__);

  // This kicks off the first frame. Should probably be moved.
  modelViewer_->setInitialized();
}

SceneController::~SceneController() {
  SPDLOG_TRACE("SceneController::~SceneController");
}

void SceneController::setUpViewer(PlatformView* platformView,
                                  FlutterDesktopEngineState* state) {
  SPDLOG_INFO("ALLEN DELETE: {} {}", __FUNCTION__, __LINE__);

  modelViewer_ = std::make_unique<CustomModelViewer>(platformView, state,
                                                     flutterAssetsPath_);

  SPDLOG_INFO("ALLEN DELETE: {} {}", __FUNCTION__, __LINE__);

  // TODO surfaceView.setOnTouchListener(modelViewer)
  //  surfaceView.setZOrderOnTop(true) // necessary

  SPDLOG_INFO("ALLEN DELETE: {} {}", __FUNCTION__, __LINE__);
}

void SceneController::setUpCamera() {
  cameraManager_ = std::make_unique<CameraManager>();
  modelViewer_->setCameraManager(cameraManager_.get());
  if (!scene_->camera_) {
    SPDLOG_ERROR("Camera failed to create {}", __FILE__, __FUNCTION__);
    return;
  }

  // Note right now cameraManager creates a default camera on startup; if we're
  // immediately setting it to a different one; that's extra work that shouldn't
  // be done. Backlogged
  cameraManager_->updateCamera(scene_->camera_.get());

  cameraManager_->setPrimaryCamera(std::move(scene_->camera_));
}

std::future<void> SceneController::setUpIblProfiler() {
  const auto promise(std::make_shared<std::promise<void>>());
  auto future(promise->get_future());

  asio::post(*ECSystemManager::GetInstance()->GetStrand(), [&/*, promise*/] {
    auto filamentSystem =
        ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
            FilamentSystem::StaticGetTypeID(), "setUpIblProfiler");


  });
  return future;
}

void SceneController::setUpSkybox() {
  // TODO
  /*skyboxManager_ =
      std::make_unique<plugin_filament_view::SkyboxManager>();
  return;

  if (!scene_->skybox_) {
    plugin_filament_view::SkyboxManager::setDefaultSkybox();
    makeSurfaceViewTransparent();
  } else {
    auto skybox = scene_->skybox_.get();
    if (dynamic_cast<HdrSkybox*>(skybox)) {
      auto hdr_skybox = dynamic_cast<HdrSkybox*>(skybox);
      if (!hdr_skybox->assetPath_.empty()) {
        auto shouldUpdateLight =
            hdr_skybox->assetPath_ == scene_->indirect_light_->getAssetPath();
        skyboxManager_->setSkyboxFromHdrAsset(
            hdr_skybox->assetPath_, hdr_skybox->showSun_, shouldUpdateLight,
            scene_->indirect_light_->getIntensity());
      } else if (!skybox->getUrl().empty()) {
        auto shouldUpdateLight =
            hdr_skybox->url_ == scene_->indirect_light_->getUrl();
        skyboxManager_->setSkyboxFromHdrUrl(
            hdr_skybox->url_, hdr_skybox->showSun_, shouldUpdateLight,
            scene_->indirect_light_->getIntensity());
      }
    } else if (dynamic_cast<KxtSkybox*>(skybox)) {
      auto kxt_skybox = dynamic_cast<KxtSkybox*>(skybox);
      if (!kxt_skybox->assetPath_.empty()) {
        plugin_filament_view::SkyboxManager::setSkyboxFromKTXAsset(
            kxt_skybox->assetPath_);
      } else if (!kxt_skybox->url_.empty()) {
        plugin_filament_view::SkyboxManager::setSkyboxFromKTXUrl(
            kxt_skybox->url_);
      }
    } else if (dynamic_cast<ColorSkybox*>(skybox)) {
      auto color_skybox = dynamic_cast<ColorSkybox*>(skybox);
      if (!color_skybox->color_.empty()) {
        plugin_filament_view::SkyboxManager::setSkyboxFromColor(
            color_skybox->color_);
      }
    }
  }*/
}

void SceneController::setUpLight() {
  /*lightManager_ = std::make_unique<LightSystem>();

  if (scene_) {
    if (scene_->light_) {
      lightManager_->changeLight(scene_->light_.get());
    } else {
      lightManager_->setDefaultLight();
    }
  } else {
    lightManager_->setDefaultLight();
  }*/
}

void SceneController::ChangeLightProperties(int /*nWhichLightIndex*/,
                                            const std::string& colorValue,
                                            int32_t intensity) {
  if (scene_) {
    if (scene_->light_) {
      SPDLOG_TRACE("Changing light values. {} {}", __FILE__, __FUNCTION__);

      scene_->light_->ChangeColor(colorValue);
      scene_->light_->ChangeIntensity(static_cast<float>(intensity));

      // TODO
      //lightManager_->changeLight(scene_->light_.get());
      return;
    }
  }

  SPDLOG_WARN("Not implemented {} {}", __FILE__, __FUNCTION__);
}

void SceneController::ChangeIndirectLightProperties(int32_t intensity) {
  auto indirectLight = scene_->indirect_light_.get();
  indirectLight->setIntensity(static_cast<float>(intensity));
  indirectLight->Print("SceneController ChangeIndirectLightProperties");

  // TODO

  /*if (dynamic_cast<DefaultIndirectLight*>(indirectLight)) {
    SPDLOG_WARN("setIndirectLight  {} {}", __FILE__, __FUNCTION__);
    plugin_filament_view::IndirectLightSystem::setIndirectLight(
        dynamic_cast<DefaultIndirectLight*>(indirectLight));
  }*/
}

void SceneController::setUpIndirectLight() {
  // TODO

  /*indirectLightManager_ =
      std::make_unique<IndirectLightSystem>();
  if (!scene_->indirect_light_) {
    // This was called in the constructor of indirectLightManager_ anyway.
    // plugin_filament_view::IndirectLightSystem::setDefaultIndirectLight();
  } else {
    auto indirectLight = scene_->indirect_light_.get();
    if (dynamic_cast<KtxIndirectLight*>(indirectLight)) {
      if (!indirectLight->getAssetPath().empty()) {
        plugin_filament_view::IndirectLightSystem::
            setIndirectLightFromKtxAsset(indirectLight->getAssetPath(),
                                         indirectLight->getIntensity());
      } else if (!indirectLight->getUrl().empty()) {
        plugin_filament_view::IndirectLightSystem::setIndirectLightFromKtxUrl(
            indirectLight->getAssetPath(), indirectLight->getIntensity());
      }
    } else if (dynamic_cast<HdrIndirectLight*>(indirectLight)) {
      if (!indirectLight->getAssetPath().empty()) {
        // val shouldUpdateLight = indirectLight->getAssetPath() !=
        // scene?.skybox?.assetPath if (shouldUpdateLight) {
        indirectLightManager_->setIndirectLightFromHdrAsset(
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
      plugin_filament_view::IndirectLightSystem::setIndirectLight(
          dynamic_cast<DefaultIndirectLight*>(indirectLight));
    } else {
      // Already called in the default constructor.
      //plugin_filament_view::IndirectLightSystem::setDefaultIndirectLight();
    }
  }*/
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
    /*if (result.getStatus() != Status::Success && poCurrModel->GetFallback()) {
      auto fallback = poCurrModel->GetFallback();
      if (fallback) {
        result = loadModel(fallback);
        SPDLOG_DEBUG("Fallback loadModel: {}", result.getMessage());
        setUpAnimation(fallback->GetAnimation());
      } else {
        spdlog::error("[SceneController] Error.FallbackLoadFailed");
      }*/
    //} else {
      // use the entities transform(s) data.
      //EntityTransforms::vApplyTransform(poCurrModel->getAsset(),
      //                                  *poCurrModel->GetBaseTransform());

      //setUpAnimation(poCurrModel->GetAnimation());
    //}
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

std::string SceneController::setDefaultCamera() {
  cameraManager_->setDefaultCamera();
  return "Default camera updated successfully";
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

void SceneController::onTouch(int32_t action,
                              int32_t point_count,
                              size_t point_data_size,
                              const double* point_data) {
  auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), __FUNCTION__);

  // if action is 0, then on 'first' touch, cast ray from camera;
  auto viewport = filamentSystem->getFilamentView()->getViewport();
  auto touch =
      TouchPair(point_count, point_data_size, point_data, viewport.height);

  static constexpr int ACTION_DOWN = 0;

  if (action == ACTION_DOWN) {
    auto rayInfo = cameraManager_->oGetRayInformationFromOnTouchPosition(touch);

    ECSMessage rayInformation;
    rayInformation.addData(ECSMessageType::DebugLine, rayInfo);
    ECSystemManager::GetInstance()->vRouteMessage(rayInformation);

    ECSMessage collisionRequest;
    collisionRequest.addData(ECSMessageType::CollisionRequest, rayInfo);
    collisionRequest.addData(ECSMessageType::CollisionRequestRequestor,
                             std::string(__FUNCTION__));
    collisionRequest.addData(ECSMessageType::CollisionRequestType,
                             CollisionEventType::eNativeOnTouchBegin);
    ECSystemManager::GetInstance()->vRouteMessage(collisionRequest);
  }

  if (cameraManager_) {
    cameraManager_->onAction(action, point_count, point_data_size, point_data);
  }
}

}  // namespace plugin_filament_view