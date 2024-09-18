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

#include "filament_view_plugin.h"

#include <core/systems/collision_manager.h>
#include <flutter/standard_message_codec.h>

#include "filament_scene.h"
#include "messages.g.h"
#include "plugins/common/common.h"

class FlutterView;

class Display;

namespace plugin_filament_view {

// static
void FilamentViewPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrar* registrar,
    int32_t id,
    std::string viewType,
    int32_t direction,
    double top,
    double left,
    double width,
    double height,
    const std::vector<uint8_t>& params,
    std::string assetDirectory,
    FlutterDesktopEngineRef engine,
    PlatformViewAddListener addListener,
    PlatformViewRemoveListener removeListener,
    void* platform_view_context) {
  auto plugin = std::make_unique<FilamentViewPlugin>(
      id, std::move(viewType), direction, top, left, width, height, params,
      std::move(assetDirectory), engine, addListener, removeListener,
      platform_view_context);

  FilamentViewApi::SetUp(registrar->messenger(), plugin.get(), id);
  ModelStateChannelApi::SetUp(registrar->messenger(), plugin.get(), id);
  SceneStateApi::SetUp(registrar->messenger(), plugin.get(), id);
  ShapeStateApi::SetUp(registrar->messenger(), plugin.get(), id);
  RendererChannelApi::SetUp(registrar->messenger(), plugin.get(), id);

  CustomModelViewer::Instance("RegisterWithRegistrar")
      ->setupMessageChannels(registrar);

  CollisionManager::Instance()->setupMessageChannels(registrar);

  registrar->AddPlugin(std::move(plugin));
}

FilamentViewPlugin::FilamentViewPlugin(
    int32_t id,
    std::string viewType,
    int32_t direction,
    double top,
    double left,
    double width,
    double height,
    const std::vector<uint8_t>& params,
    std::string assetDirectory,
    FlutterDesktopEngineState* state,
    PlatformViewAddListener addListener,
    PlatformViewRemoveListener removeListener,
    void* platform_view_context)
    : PlatformView(id,
                   std::move(viewType),
                   direction,
                   top,
                   left,
                   width,
                   height),
      id_(id),
      platformViewsContext_(platform_view_context),
      removeListener_(removeListener),
      flutterAssetsPath_(std::move(assetDirectory)) {
  SPDLOG_TRACE("++FilamentViewPlugin::FilamentViewPlugin");
  filamentScene_ = std::make_unique<FilamentScene>(this, state, id, params,
                                                   flutterAssetsPath_);
  addListener(platformViewsContext_, id, &platform_view_listener_, this);
  SPDLOG_TRACE("--FilamentViewPlugin::FilamentViewPlugin");
}

FilamentViewPlugin::~FilamentViewPlugin() {
  removeListener_(platformViewsContext_, id_);
}

void FilamentViewPlugin::ChangeAnimationByIndex(
    int32_t /* index */,
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::ChangeDirectLightByIndex(
    int32_t index,
    std::string color,
    int32_t intensity,
    std::function<void(std::optional<FlutterError> reply)> /*result*/) {
  const auto sceneController = filamentScene_->getSceneController();
  sceneController->ChangeLightProperties(index, color, intensity);
}

//////////////////////////////////////////////////////////////////////////////////////////
void FilamentViewPlugin::ToggleShapesInScene(
    bool value,
    std::function<void(std::optional<FlutterError> reply)> /*result*/) {
  const auto sceneController = filamentScene_->getSceneController();
  sceneController->vToggleAllShapesInScene(value);
}

//////////////////////////////////////////////////////////////////////////////////////////
void FilamentViewPlugin::ToggleDebugCollidableViewsInScene(
    bool value,
    std::function<void(std::optional<FlutterError> reply)> /*result*/) {
  if (!value) {
    CollisionManager::Instance()->vTurnOffRenderingOfCollidables();
  } else {
    CollisionManager::Instance()->vTurnOnRenderingOfCollidables();
  }
}

//////////////////////////////////////////////////////////////////////////////////////////
void FilamentViewPlugin::ToggleCameraAutoRotate(
    bool value,
    std::function<void(std::optional<FlutterError> reply)> /*result*/) {
  const auto sceneController = filamentScene_->getSceneController();
  sceneController->getCameraManager()->togglePrimaryCameraFeatureMode(value);
}

//////////////////////////////////////////////////////////////////////////////////////////
void FilamentViewPlugin::SetCameraRotation(
    float fValue,
    std::function<void(std::optional<FlutterError> reply)> /*result*/) {
  const auto sceneController = filamentScene_->getSceneController();
  Camera* poCamera = sceneController->getCameraManager()->poGetPrimaryCamera();
  if (poCamera != nullptr) {
    poCamera->vSetCurrentCameraOrbitAngle(fValue);
  }
}

void FilamentViewPlugin::ChangeAnimationByName(
    std::string /* name */,
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::GetAnimationNames(
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::GetAnimationCount(
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::GetCurrentAnimationIndex(
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::GetAnimationNameByIndex(
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::ChangeSkyboxByAsset(
    std::string /* path */,
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::ChangeSkyboxByUrl(
    std::string /* url */,
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::ChangeSkyboxByHdrAsset(
    std::string /* path */,
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::ChangeSkyboxByHdrUrl(
    std::string /* url */,
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::ChangeSkyboxColor(
    std::string /* color */,
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::ChangeToTransparentSkybox(
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::ChangeLightByKtxAsset(
    std::string /* path */,
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::ChangeLightByKtxUrl(
    std::string /* url */,
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::ChangeLightByIndirectLight(
    std::string /* path */,
    double /* intensity */,
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::ChangeLightByHdrUrl(
    std::string /* path */,
    double /* intensity */,
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::ChangeToDefaultIndirectLight(
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

void FilamentViewPlugin::on_resize(double width, double height, void* data) {
  auto plugin = static_cast<FilamentViewPlugin*>(data);
  if (plugin && plugin->filamentScene_) {
    plugin->filamentScene_->getSceneController()->getModelViewer()->resize(
        width, height);
  }
}

void FilamentViewPlugin::on_set_direction(int32_t direction, void* data) {
  const auto plugin = static_cast<FilamentViewPlugin*>(data);
  if (plugin) {
    plugin->direction_ = direction;
  }
  SPDLOG_TRACE("SetDirection: {}", plugin->direction_);
}

void FilamentViewPlugin::on_set_offset(double left, double top, void* data) {
  auto plugin = static_cast<FilamentViewPlugin*>(data);
  if (plugin && plugin->filamentScene_) {
    auto sceneController = plugin->filamentScene_->getSceneController();
    if (sceneController) {
      sceneController->getModelViewer()->setOffset(left, top);
    }
  }
}

void FilamentViewPlugin::on_touch(int32_t action,
                                  int32_t point_count,
                                  size_t point_data_size,
                                  const double* point_data,
                                  void* data) {
  auto plugin = static_cast<FilamentViewPlugin*>(data);
  if (plugin && plugin->filamentScene_) {
    auto sceneController = plugin->filamentScene_->getSceneController();
    if (sceneController) {
      sceneController->onTouch(action, point_count, point_data_size,
                               point_data);
    }
  }
}

void FilamentViewPlugin::on_dispose(bool /* hybrid */, void* data) {
  auto plugin = static_cast<FilamentViewPlugin*>(data);
  if (plugin && plugin->filamentScene_) {
    plugin->filamentScene_.reset();
  }
}

const struct platform_view_listener
    FilamentViewPlugin::platform_view_listener_ = {
        .resize = on_resize,
        .set_direction = on_set_direction,
        .set_offset = on_set_offset,
        .on_touch = on_touch,
        .dispose = on_dispose};

}  // namespace plugin_filament_view