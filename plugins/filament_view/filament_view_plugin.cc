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
#include <core/systems/derived/skybox_system.h>
#include <flutter/standard_message_codec.h>
#include <asio/post.hpp>

#include "filament_scene.h"
#include "messages.g.h"
#include "plugins/common/common.h"

#include "core/systems/ecsystems_manager.h"

class FlutterView;

class Display;

namespace plugin_filament_view {
FilamentScene* weakPtr;
FilamentViewPlugin* viewPluginWeakPtr;

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
    std::string assetDirectory,
    FlutterDesktopEngineRef engine,
    PlatformViewAddListener addListener,
    PlatformViewRemoveListener removeListener,
    void* platform_view_context) {
  spdlog::debug("1 Global Filament API thread: 0x{:x}", pthread_self());

  pthread_setname_np(pthread_self(), "HomeScreenFilamentViewPlugin");

  // Create the ECSystemManager instance
  auto ecsManager = ECSystemManager::GetInstance();

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

  // Create a promise and future to synchronize initialization
  std::promise<void> initPromise;
  std::future<void> initFuture = initPromise.get_future();

  // Post the initialization code to the strand
  asio::post(strand, [=, &initPromise]() mutable {
    spdlog::debug("Initialization on Filament API thread: 0x{:x}",
                  pthread_self());

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

    ecsManager->vInitSystems();

    initPromise.set_value();
  });

  initFuture.wait();

  std::promise<void> initPromise2;
  std::future<void> initFuture2 = initPromise2.get_future();

  asio::post(strand, [=, &initPromise2]() mutable {
    // Continue with plugin initialization
    auto plugin = std::make_unique<FilamentViewPlugin>(
        id, std::move(viewType), direction, top, left, width, height, params,
        std::move(assetDirectory), engine, addListener, removeListener,
        platform_view_context);

    // Set up message channels and APIs
    FilamentViewApi::SetUp(registrar->messenger(), plugin.get(), id);
    ModelStateChannelApi::SetUp(registrar->messenger(), plugin.get(), id);
    SceneStateApi::SetUp(registrar->messenger(), plugin.get(), id);
    ShapeStateApi::SetUp(registrar->messenger(), plugin.get(), id);
    RendererChannelApi::SetUp(registrar->messenger(), plugin.get(), id);

    CustomModelViewer::Instance("RegisterWithRegistrar")
        ->setupMessageChannels(registrar);

    // Set up collision system message channels if needed
    auto collisionSystem = ecsManager->poGetSystemAs<CollisionSystem>(
        CollisionSystem::StaticGetTypeID(),
        "Filament ViewPlugin :: Second Lambda");
    if (collisionSystem != nullptr) {
      collisionSystem->setupMessageChannels(registrar);
    }

    registrar->AddPlugin(std::move(plugin));

    // Signal that initialization is complete
    initPromise2.set_value();
  });

  // Wait for the initialization to complete
  initFuture2.wait();

  // asio::post(strand, [=]() mutable {
  // Todo - to be changed.
  auto t = weakPtr->getSceneController()->getModelViewer()->Initialize();
  t.wait();
  //});

  // This should eventually get moved to a system loading process.
  std::promise<void> initPromise3;
  std::future<void> initFuture3 = initPromise3.get_future();

  asio::post(strand, [=, &initPromise3]() mutable {
    weakPtr->getSceneController()->vRunPostSetupLoad();
    initPromise3.set_value();
  });

  initFuture3.wait();

  ecsManager->DebugPrint();
  ecsManager->StartRunLoop();

  spdlog::debug("Initialization completed");
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

  weakPtr = filamentScene_.get();
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
  const auto sceneController = filamentScene_->getSceneController();
  sceneController->vToggleAllShapesInScene(value);
}

//////////////////////////////////////////////////////////////////////////////////////////
void FilamentViewPlugin::ToggleDebugCollidableViewsInScene(
    bool value,
    std::function<void(std::optional<FlutterError> reply)> /*result*/) {
  auto collisionSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<CollisionSystem>(
          CollisionSystem::StaticGetTypeID(),
          "ToggleDebugCollidableViewsInScene");
  if (collisionSystem == nullptr) {
    spdlog::warn("Unable to toggle collision on/off, system is null");
    return;
  }

  // Note this can probably become a message in the future, backlogged.
  if (!value) {
    collisionSystem->vTurnOffRenderingOfCollidables();
  } else {
    collisionSystem->vTurnOnRenderingOfCollidables();
  }
}

//////////////////////////////////////////////////////////////////////////////////////////
void FilamentViewPlugin::ChangeCameraMode(
    std::string value,
    std::function<void(std::optional<FlutterError> reply)> /*result*/) {
  const auto sceneController = filamentScene_->getSceneController();
  sceneController->getCameraManager()->ChangePrimaryCameraMode(value);
}

//////////////////////////////////////////////////////////////////////////////////////////
void FilamentViewPlugin::vResetInertiaCameraToDefaultValues(
    std::function<void(std::optional<FlutterError> reply)> /*result*/) {
  const auto sceneController = filamentScene_->getSceneController();
  sceneController->getCameraManager()->vResetInertiaCameraToDefaultValues();
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