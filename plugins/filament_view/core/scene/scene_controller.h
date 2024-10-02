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

#pragma once

#include <filament/Engine.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/ResourceLoader.h>

#include "core/entity/model/animation/animation_manager.h"
#include "core/entity/model/model.h"
#include "core/entity/shapes/baseshape.h"
#include "core/include/resource.h"
#include "core/scene/indirect_light/indirect_light_manager.h"
#include "core/scene/light/light_manager.h"
#include "core/scene/material/material_manager.h"
#include "core/scene/skybox/skybox_manager.h"
#include "core/systems/derived/shape_manager.h"
#include "core/utils/ibl_profiler.h"
#include "flutter_desktop_engine_state.h"
#include "platform_views/platform_view.h"
#include "scene.h"
#include "viewer/custom_model_viewer.h"

namespace plugin_filament_view {

class Model;
class Scene;
class LightManager;
class IndirectLightManager;
class SkyboxManager;
class Animation;
class AnimationManager;
class CameraManager;
class GroundManager;
class MaterialManager;
class ShapeManager;

namespace shapes {
class BaseShape;
}

class IBLProfiler;

class SceneController {
 public:
  SceneController(PlatformView* platformView,
                  FlutterDesktopEngineState* state,
                  std::string flutterAssetsPath,
                  std::vector<std::unique_ptr<Model>>* models,
                  Scene* scene,
                  std::vector<std::unique_ptr<shapes::BaseShape>>* shapes,
                  int32_t id);

  ~SceneController();

  [[nodiscard]] CustomModelViewer* getModelViewer() const {
    return modelViewer_.get();
  }

  void onTouch(int32_t action,
               int32_t point_count,
               size_t point_data_size,
               const double* point_data);

  [[nodiscard]] CameraManager* getCameraManager() const {
    return cameraManager_.get();
  }

  plugin_filament_view::MaterialManager* poGetMaterialManager();

  void ChangeLightProperties(int nWhichLightIndex,
                             const std::string& colorValue,
                             int32_t intensity);

  void ChangeIndirectLightProperties(int32_t intensity);

  void vToggleAllShapesInScene(bool bValue);

 private:
  // Note: id_ will be moved in a future version when we start to maintain
  // scenes to views to swapchains more appropriately.
  int32_t id_;
  std::string flutterAssetsPath_;

  std::vector<std::unique_ptr<Model>>* models_;
  Scene* scene_;

  std::unique_ptr<CustomModelViewer> modelViewer_;

  std::optional<int32_t> currentAnimationIndex_;

  std::unique_ptr<plugin_filament_view::IBLProfiler> iblProfiler_;
  std::unique_ptr<plugin_filament_view::LightManager> lightManager_;
  std::unique_ptr<plugin_filament_view::IndirectLightManager>
      indirectLightManager_;
  std::unique_ptr<plugin_filament_view::SkyboxManager> skyboxManager_;
  std::unique_ptr<plugin_filament_view::AnimationManager> animationManager_;
  std::unique_ptr<plugin_filament_view::CameraManager> cameraManager_;
  // this should probably be promoted to outside this class TODO
  std::unique_ptr<plugin_filament_view::MaterialManager> materialManager_;
  std::unique_ptr<plugin_filament_view::ShapeManager> shapeManager_;

  void setUpViewer(PlatformView* platformView,
                   FlutterDesktopEngineState* state);

  void setUpLoadingModels();

  void setUpCamera();

  std::future<void> setUpIblProfiler();

  void setUpSkybox();

  void setUpLight();

  void setUpIndirectLight();

  void setUpShapes(std::vector<std::unique_ptr<shapes::BaseShape>>* shapes);

  std::string setDefaultCamera();

  Resource<std::string_view> loadModel(Model* model);

  void setUpAnimation(std::optional<Animation*> animation);

  void makeSurfaceViewTransparent();

  void makeSurfaceViewNotTransparent();
};
}  // namespace plugin_filament_view
