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

#include <core/systems/derived/collision_system.h>
#include <core/systems/derived/debug_lines_system.h>
#include <core/systems/derived/filament_system.h>
#include <core/systems/derived/indirect_light_system.h>
#include <core/systems/derived/light_system.h>
#include <core/systems/derived/model_system.h>
#include <core/systems/derived/skybox_system.h>
#include <core/systems/derived/view_target_system.h>
#include <flutter/standard_message_codec.h>
#include <asio/post.hpp>

#include "filament_scene.h"
#include "messages.g.h"
#include "plugins/common/common.h"

#include "core/systems/ecsystems_manager.h"

class FlutterView;

class Display;

namespace plugin_filament_view {

FilamentScene* postSetupDeserializer = nullptr;

//////////////////////////////////////////////////////////////////////////////////////////
void RunOnceCheckAndInitializeECSystems() {
  auto ecsManager = ECSystemManager::GetInstance();

  if (ecsManager->getRunState() != ECSystemManager::RunState::NotInitialized) {
    return;
  }

  // Get the strand from the ECSystemManager
  const auto& strand = *ecsManager->GetStrand();

  std::promise<void> initPromise;
  std::future<void> initFuture = initPromise.get_future();

  // Post the initialization code to the strand
  asio::post(strand, [=, &initPromise]() mutable {
    // Add systems to the ECSystemManager
    ecsManager->vAddSystem(std::move(std::make_unique<FilamentSystem>()));
    ecsManager->vAddSystem(std::move(std::make_unique<DebugLinesSystem>()));
    ecsManager->vAddSystem(std::move(std::make_unique<CollisionSystem>()));
    ecsManager->vAddSystem(std::move(std::make_unique<ModelSystem>()));
    ecsManager->vAddSystem(std::move(std::make_unique<MaterialSystem>()));
    ecsManager->vAddSystem(std::move(std::make_unique<ShapeSystem>()));
    ecsManager->vAddSystem(std::move(std::make_unique<IndirectLightSystem>()));
    ecsManager->vAddSystem(std::move(std::make_unique<SkyboxSystem>()));
    ecsManager->vAddSystem(std::move(std::make_unique<LightSystem>()));
    ecsManager->vAddSystem(std::move(std::make_unique<ViewTargetSystem>()));

    ecsManager->vInitSystems();

    initPromise.set_value();
  });

  initFuture.wait();
}

//////////////////////////////////////////////////////////////////////////////////////////
void KickOffRenderingLoops() {
  ECSMessage viewTargetStartRendering;
  viewTargetStartRendering.addData(
      ECSMessageType::ViewTargetStartRenderingLoops, true);
  ECSystemManager::GetInstance()->vRouteMessage(viewTargetStartRendering);
}

//////////////////////////////////////////////////////////////////////////////////////////
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
    const std::string& assetDirectory,
    FlutterDesktopEngineRef engine,
    PlatformViewAddListener addListener,
    PlatformViewRemoveListener removeListener,
    void* platform_view_context) {
  pthread_setname_np(pthread_self(), "HomeScreenFilamentViewPlugin");

  auto ecsManager = ECSystemManager::GetInstance();
  ecsManager->setConfigValue(kAssetPath, assetDirectory);

  // Get the strand from the ECSystemManager
  const auto& strand = *ecsManager->GetStrand();

  /*bool bDebugAttached = false;
  int i = 0;
  while (!bDebugAttached) {
    int breakhere = 0;
    if (i++ == 10000000000) {
      bDebugAttached = true;
    }
  }*/

  // Safeguarded inside
  RunOnceCheckAndInitializeECSystems();

  // Every time this method is called, we should create a new view target
  ECSMessage viewTargetCreationRequest;
  viewTargetCreationRequest.addData(ECSMessageType::ViewTargetCreateRequest,
                                    engine);
  viewTargetCreationRequest.addData(ECSMessageType::ViewTargetCreateRequestTop,
                                    static_cast<int>(top));
  viewTargetCreationRequest.addData(ECSMessageType::ViewTargetCreateRequestLeft,
                                    static_cast<int>(left));
  viewTargetCreationRequest.addData(
      ECSMessageType::ViewTargetCreateRequestWidth,
      static_cast<uint32_t>(width));
  viewTargetCreationRequest.addData(
      ECSMessageType::ViewTargetCreateRequestHeight,
      static_cast<uint32_t>(height));
  ECSystemManager::GetInstance()->vRouteMessage(viewTargetCreationRequest);

  std::promise<void> initPromise;
  std::future<void> initFuture = initPromise.get_future();

  // Safeguarded to only be called once no matter how many times this method is
  // called.
  if (postSetupDeserializer == nullptr) {
    asio::post(strand, [=, &initPromise]() mutable {
      auto plugin = std::make_unique<FilamentViewPlugin>(
          id, std::move(viewType), direction, top, left, width, height, params,
          assetDirectory, addListener, removeListener, platform_view_context);

      // Set up message channels and APIs
      // TODO all these API's are not needed.
      FilamentViewApi::SetUp(registrar->messenger(), plugin.get(), id);
      ModelStateChannelApi::SetUp(registrar->messenger(), plugin.get(), id);
      SceneStateApi::SetUp(registrar->messenger(), plugin.get(), id);
      ShapeStateApi::SetUp(registrar->messenger(), plugin.get(), id);
      RendererChannelApi::SetUp(registrar->messenger(), plugin.get(), id);

      registrar->AddPlugin(std::move(plugin));

      // making sure this is only called once!
      postSetupDeserializer->getSceneController()->vRunPostSetupLoad();

      initPromise.set_value();
    });

    initFuture.wait();
  }

  // Ok to be called infinite times.
  ECSMessage setupMessageChannels;
  setupMessageChannels.addData(ECSMessageType::SetupMessageChannels, registrar);
  ECSystemManager::GetInstance()->vRouteMessage(setupMessageChannels);

  // Ok to be called infinite times.
  KickOffRenderingLoops();

  SPDLOG_TRACE("Initialization completed");
}

//////////////////////////////////////////////////////////////////////////////////////////
FilamentViewPlugin::FilamentViewPlugin(
    int32_t id,
    std::string viewType,
    int32_t direction,
    double top,
    double left,
    double width,
    double height,
    const std::vector<uint8_t>& params,
    const std::string& assetDirectory,
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
      removeListener_(removeListener) {
  SPDLOG_TRACE("++FilamentViewPlugin::FilamentViewPlugin");
  filamentScene_ = std::make_unique<FilamentScene>(id, params, assetDirectory);
  addListener(platformViewsContext_, id, &platform_view_listener_, this);

  postSetupDeserializer = filamentScene_.get();
  SPDLOG_TRACE("--FilamentViewPlugin::FilamentViewPlugin");
}

//////////////////////////////////////////////////////////////////////////////////////////
FilamentViewPlugin::~FilamentViewPlugin() {
  removeListener_(platformViewsContext_, id_);

  ECSystemManager::GetInstance()->vShutdownSystems();
  ECSystemManager::GetInstance()->vRemoveAllSystems();
  // wait for thread to stop running. (Should be relatively quick)
  while (ECSystemManager::GetInstance()->bIsCompletedStopping() == false) {
  }
}

//////////////////////////////////////////////////////////////////////////////////////////
void FilamentViewPlugin::ChangeAnimationByIndex(
    int32_t /* index */,
    std::function<void(std::optional<FlutterError> reply)> /* result */) {}

//////////////////////////////////////////////////////////////////////////////////////////
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
    ECSMessage toggleMessage;
    toggleMessage.addData(ECSMessageType::ToggleShapesInScene, value);
    ECSystemManager::GetInstance()->vRouteMessage(toggleMessage);
}

//////////////////////////////////////////////////////////////////////////////////////////
void FilamentViewPlugin::ToggleDebugCollidableViewsInScene(
    bool value,
    std::function<void(std::optional<FlutterError> reply)> /*result*/) {
    ECSMessage toggleMessage;
    toggleMessage.addData(ECSMessageType::ToggleDebugCollidableViewsInScene, value);
    ECSystemManager::GetInstance()->vRouteMessage(toggleMessage);
}

//////////////////////////////////////////////////////////////////////////////////////////
void FilamentViewPlugin::ChangeCameraMode(
    std::string szValue,
    std::function<void(std::optional<FlutterError> reply)> /*result*/) {
  auto viewTargetSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<ViewTargetSystem>(
          ViewTargetSystem::StaticGetTypeID(), __FUNCTION__);

  viewTargetSystem->vChangePrimaryCameraMode(0, szValue);
}

//////////////////////////////////////////////////////////////////////////////////////////
void FilamentViewPlugin::vResetInertiaCameraToDefaultValues(
    std::function<void(std::optional<FlutterError> reply)> /*result*/) {
  auto viewTargetSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<ViewTargetSystem>(
          ViewTargetSystem::StaticGetTypeID(), __FUNCTION__);

  viewTargetSystem->vResetInertiaCameraToDefaultValues(0);
}

//////////////////////////////////////////////////////////////////////////////////////////
void FilamentViewPlugin::SetCameraRotation(
    float fValue,
    std::function<void(std::optional<FlutterError> reply)> /*result*/) {
  // TODO
  auto viewTargetSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<ViewTargetSystem>(
          ViewTargetSystem::StaticGetTypeID(), __FUNCTION__);

  viewTargetSystem->vSetCurrentCameraOrbitAngle(0, fValue);
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

// TODO this function will need to change to say 'which' view is being changed.
void FilamentViewPlugin::on_resize(double width, double height, void* data) {
  auto plugin = static_cast<FilamentViewPlugin*>(data);
  if (plugin && plugin->filamentScene_) {
    auto viewTargetSystem =
        ECSystemManager::GetInstance()->poGetSystemAs<ViewTargetSystem>(
            ViewTargetSystem::StaticGetTypeID(),
            "FilamentViewPlugin::on_resize");

    viewTargetSystem->vResizeViewTarget(0, width, height);
  }
}

void FilamentViewPlugin::on_set_direction(int32_t direction, void* data) {
  const auto plugin = static_cast<FilamentViewPlugin*>(data);
  if (plugin) {
    plugin->direction_ = direction;
  }
  SPDLOG_TRACE("SetDirection: {}", plugin->direction_);
}

// TODO this function will need to change to say 'which' view is being changed.
void FilamentViewPlugin::on_set_offset(double left, double top, void* data) {
  auto plugin = static_cast<FilamentViewPlugin*>(data);
  if (plugin && plugin->filamentScene_) {
    auto viewTargetSystem =
        ECSystemManager::GetInstance()->poGetSystemAs<ViewTargetSystem>(
            ViewTargetSystem::StaticGetTypeID(),
            "FilamentViewPlugin::on_resize");

    viewTargetSystem->vSetViewTargetOffSet(0, left, top);
  }
}

// TODO this function will need to change to say 'which' view is being changed.
void FilamentViewPlugin::on_touch(int32_t action,
                                  int32_t point_count,
                                  size_t point_data_size,
                                  const double* point_data,
                                  void* data) {
  auto plugin = static_cast<FilamentViewPlugin*>(data);
  if (plugin && plugin->filamentScene_) {
    auto viewTargetSystem =
        ECSystemManager::GetInstance()->poGetSystemAs<ViewTargetSystem>(
            ViewTargetSystem::StaticGetTypeID(),
            "FilamentViewPlugin::on_touch");

    // has to be changed to 'which' on touch was hit
    viewTargetSystem->vOnTouch(0, action, point_count, point_data_size,
                               point_data);
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