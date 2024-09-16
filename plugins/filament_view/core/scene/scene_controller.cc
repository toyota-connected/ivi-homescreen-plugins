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

#include <core/utils/entitytransforms.h>
#include <asio/post.hpp>
#include <utility>

#include "core/systems//collision_manager.h"

#include "plugins/common/common.h"

namespace plugin_filament_view {

SceneController::SceneController(
    PlatformView* platformView,
    FlutterDesktopEngineState* state,
    std::string flutterAssetsPath,
    std::vector<std::unique_ptr<Model>>* models,
    Scene* scene,
    std::vector<std::unique_ptr<shapes::BaseShape>>* shapes,
    int32_t id)
    : id_(id),
      flutterAssetsPath_(std::move(flutterAssetsPath)),
      models_(models),
      scene_(scene) {
  SPDLOG_TRACE("++{} {}", __FILE__, __FUNCTION__);

  spdlog::info("SceneController {} setup", id_);

  setUpViewer(platformView, state);
  setUpLoadingModels();
  setUpCamera();
  setUpSkybox();
  setUpLight();
  setUpIndirectLight();
  setUpShapes(shapes);

  modelViewer_->setInitialized();

  SPDLOG_TRACE("--{} {}", __FILE__, __FUNCTION__);
}

SceneController::~SceneController() {
  SPDLOG_TRACE("SceneController::~SceneController");
}

void SceneController::setUpViewer(PlatformView* platformView,
                                  FlutterDesktopEngineState* state) {
  modelViewer_ = std::make_unique<CustomModelViewer>(platformView, state,
                                                     flutterAssetsPath_);
  materialManager_ = std::make_unique<MaterialManager>();

  // TODO surfaceView.setOnTouchListener(modelViewer)
  //  surfaceView.setZOrderOnTop(true) // necessary

  auto view = modelViewer_->getFilamentView();
  auto scene = modelViewer_->getFilamentScene();

  auto size = platformView->GetSize();
  view->setViewport({0, 0, static_cast<uint32_t>(size.first),
                     static_cast<uint32_t>(size.second)});

  view->setScene(scene);

  // TODO this may need to be turned off for target
  view->setPostProcessingEnabled(true);
}

void SceneController::setUpCamera() {
  cameraManager_ = std::make_unique<CameraManager>();
  modelViewer_->setCameraManager(cameraManager_.get());
  if (!scene_->camera_) {
    SPDLOG_ERROR("Camera failed to create {}", __FILE__, __FUNCTION__);
    return;
  }

  auto t = cameraManager_->updateCamera(scene_->camera_.get());
  t.wait();

  cameraManager_->setPrimaryCamera(std::move(scene_->camera_));
}

std::future<void> SceneController::setUpIblProfiler() {
  const auto promise(std::make_shared<std::promise<void>>());
  auto future(promise->get_future());
  asio::post(modelViewer_->getStrandContext(), [&/*, promise*/] {
    iblProfiler_ = std::make_unique<plugin_filament_view::IBLProfiler>(
        modelViewer_->getFilamentEngine());
  });
  return future;
}

void SceneController::setUpSkybox() {
  auto f = setUpIblProfiler();
  f.wait();
  skyboxManager_ =
      std::make_unique<plugin_filament_view::SkyboxManager>(iblProfiler_.get());

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
  }
}

void SceneController::setUpLight() {
  lightManager_ = std::make_unique<LightManager>();

  if (scene_) {
    if (scene_->light_) {
      lightManager_->changeLight(scene_->light_.get());
    } else {
      lightManager_->setDefaultLight();
    }
  } else {
    lightManager_->setDefaultLight();
  }
}

void SceneController::ChangeLightProperties(int /*nWhichLightIndex*/,
                                            const std::string& colorValue,
                                            int32_t intensity) {
  if (scene_) {
    if (scene_->light_) {
      SPDLOG_TRACE("Changing light values. {} {}", __FILE__, __FUNCTION__);

      scene_->light_->ChangeColor(colorValue);
      scene_->light_->ChangeIntensity(static_cast<float>(intensity));

      lightManager_->changeLight(scene_->light_.get());
      return;
    }
  }

  SPDLOG_WARN("Not implemented {} {}", __FILE__, __FUNCTION__);
}

void SceneController::ChangeIndirectLightProperties(int32_t intensity) {
  auto indirectLight = scene_->indirect_light_.get();
  indirectLight->setIntensity(static_cast<float>(intensity));

  indirectLight->Print("SceneController ChangeIndirectLightProperties");

  if (dynamic_cast<DefaultIndirectLight*>(indirectLight)) {
    SPDLOG_WARN("setIndirectLight  {} {}", __FILE__, __FUNCTION__);
    plugin_filament_view::IndirectLightManager::setIndirectLight(
        dynamic_cast<DefaultIndirectLight*>(indirectLight));
  }
}

void SceneController::setUpIndirectLight() {
  indirectLightManager_ =
      std::make_unique<IndirectLightManager>(iblProfiler_.get());
  if (!scene_->indirect_light_) {
    plugin_filament_view::IndirectLightManager::setDefaultIndirectLight();
  } else {
    auto indirectLight = scene_->indirect_light_.get();
    if (dynamic_cast<KtxIndirectLight*>(indirectLight)) {
      if (!indirectLight->getAssetPath().empty()) {
        plugin_filament_view::IndirectLightManager::
            setIndirectLightFromKtxAsset(indirectLight->getAssetPath(),
                                         indirectLight->getIntensity());
      } else if (!indirectLight->getUrl().empty()) {
        plugin_filament_view::IndirectLightManager::setIndirectLightFromKtxUrl(
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
        plugin_filament_view::IndirectLightManager::setIndirectLightFromHdrUrl(
            indirectLight->getUrl(), indirectLight->getIntensity());
        //}
      }
    } else if (dynamic_cast<DefaultIndirectLight*>(indirectLight)) {
      plugin_filament_view::IndirectLightManager::setIndirectLight(
          dynamic_cast<DefaultIndirectLight*>(indirectLight));
    } else {
      plugin_filament_view::IndirectLightManager::setDefaultIndirectLight();
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
    auto result = loadModel(poCurrModel);
    if (result.getStatus() != Status::Success && poCurrModel->GetFallback()) {
      auto fallback = poCurrModel->GetFallback();
      if (fallback) {
        result = loadModel(fallback);
        SPDLOG_DEBUG("Fallback loadModel: {}", result.getMessage());
        setUpAnimation(fallback->GetAnimation());
      } else {
        spdlog::error("[SceneController] Error.FallbackLoadFailed");
      }
    } else {
      // use the entities transform(s) data.
      EntityTransforms::vApplyTransform(poCurrModel->getAsset(),
                                        *poCurrModel->GetBaseTransform());

      setUpAnimation(poCurrModel->GetAnimation());
    }
  }

  SPDLOG_TRACE("--{}::{}", __FILE__, __FUNCTION__);
}

plugin_filament_view::MaterialManager* SceneController::poGetMaterialManager() {
  return materialManager_.get();
}

void SceneController::setUpShapes(
    std::vector<std::unique_ptr<shapes::BaseShape>>* shapes) {
  SPDLOG_TRACE("{} {}", __FUNCTION__, __LINE__);
  shapeManager_ = std::make_unique<ShapeManager>(materialManager_.get());

  for (const auto& shape : *shapes) {
    if (shape->HasComponentByStaticTypeID(Collidable::StaticGetTypeID())) {
      CollisionManager::Instance()->vAddCollidable(shape.get());
    }
  }

  // This method releases shapes,
  shapeManager_->addShapesToScene(shapes);
}

void SceneController::vToggleAllShapesInScene(bool bValue) {
  if (shapeManager_ == nullptr) {
    SPDLOG_WARN("{} called before shapeManager created.", __FUNCTION__);
    return;
  }

  shapeManager_->vToggleAllShapesInScene(bValue);
}

std::string SceneController::setDefaultCamera() {
  cameraManager_->setDefaultCamera();
  return "Default camera updated successfully";
}

Resource<std::string_view> SceneController::loadModel(Model* model) {
  auto loader = modelViewer_->getModelLoader();
  if (dynamic_cast<GlbModel*>(model)) {
    auto glb_model = dynamic_cast<GlbModel*>(model);
    if (!glb_model->assetPath_.empty()) {
      auto f = loader->loadGlbFromAsset(model, glb_model->assetPath_, false);
      f.wait();
      return f.get();
    }

    if (!glb_model->url_.empty()) {
      auto f = loader->loadGlbFromUrl(model, glb_model->url_);
      f.wait();
      return f.get();
    }
  } else if (dynamic_cast<GltfModel*>(model)) {
    auto gltf_model = dynamic_cast<GltfModel*>(model);
    if (!gltf_model->assetPath_.empty()) {
      auto f = plugin_filament_view::ModelManager::loadGltfFromAsset(
          model, gltf_model->assetPath_, gltf_model->pathPrefix_,
          gltf_model->pathPostfix_);
      f.wait();
      return f.get();
    }

    if (!gltf_model->url_.empty()) {
      auto f = plugin_filament_view::ModelManager::loadGltfFromUrl(
          model, gltf_model->url_);
      f.wait();
      return f.get();
    }
  }
  return Resource<std::string_view>::Error("Unknown");
}

// TODO Move to model viewer
void SceneController::makeSurfaceViewTransparent() {
  modelViewer_->getFilamentView()->setBlendMode(
      ::filament::View::BlendMode::TRANSLUCENT);

  // TODO - not sure if needed.
  // surfaceView.holder.setFormat(PixelFormat.TRANSLUCENT)

  auto clearOptions = modelViewer_->getFilamentRenderer()->getClearOptions();
  clearOptions.clear = true;
  modelViewer_->getFilamentRenderer()->setClearOptions(clearOptions);
}

// TODO Move to model viewer - if still needed.
void SceneController::makeSurfaceViewNotTransparent() {
  modelViewer_->getFilamentView()->setBlendMode(
      ::filament::View::BlendMode::OPAQUE);

  // TODO surfaceView.setZOrderOnTop(true) // necessary
  // TODO surfaceView.holder.setFormat(PixelFormat.OPAQUE)
}

void SceneController::onTouch(int32_t action,
                              int32_t point_count,
                              size_t point_data_size,
                              const double* point_data) {
  if (cameraManager_) {
    cameraManager_->onAction(action, point_count, point_data_size, point_data);
  }
}

}  // namespace plugin_filament_view