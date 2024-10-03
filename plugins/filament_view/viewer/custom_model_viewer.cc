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

#include "custom_model_viewer.h"

#include <wayland-client.h>
#include <asio/post.hpp>
#include <utility>

#include "plugins/common/common.h"
#include "view/flutter_view.h"
#include "wayland/display.h"

#include <flutter/basic_message_channel.h>
#include <flutter/binary_messenger.h>
#include <flutter/encodable_value.h>
#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>

#include "core/include/literals.h"

using flutter::EncodableList;
using flutter::EncodableMap;
using flutter::EncodableValue;
using flutter::MessageReply;
using flutter::MethodCall;
using flutter::MethodResult;

class Display;

class FlutterView;

class FilamentViewPlugin;

namespace plugin_filament_view {

CustomModelViewer::CustomModelViewer(PlatformView* platformView,
                                     FlutterDesktopEngineState* state,
                                     std::string flutterAssetsPath)
    : state_(state),
      flutterAssetsPath_(std::move(flutterAssetsPath)),
      left_(platformView->GetOffset().first),
      top_(platformView->GetOffset().second),
      io_context_(std::make_unique<asio::io_context>(ASIO_CONCURRENCY_HINT_1)),
      work_(io_context_->get_executor()),
      strand_(std::make_unique<asio::io_context::strand>(*io_context_)),
      callback_(nullptr),
      fanimator_(nullptr),
      cameraManager_(nullptr),
      currentModelState_(ModelState::NONE),
      currentSkyboxState_(SceneState::NONE),
      currentLightState_(SceneState::NONE),
      currentShapesState_(ShapeState::NONE) {
  SPDLOG_TRACE("++{}::{}", __FILE__, __FUNCTION__);
  filament_api_thread_ = std::thread([&]() { io_context_->run(); });
  asio::post(*strand_, [&] {
    filament_api_thread_id_ = pthread_self();
    spdlog::debug("Filament API thread: 0x{:x}", filament_api_thread_id_);
  });

  /* Setup Wayland subsurface */
  setupWaylandSubsurface();

  const auto f = Initialize(platformView);
  f.wait();

  SPDLOG_TRACE("--{}::{}", __FILE__, __FUNCTION__);
}

CustomModelViewer::~CustomModelViewer() {
  SPDLOG_TRACE("++{}::{}", __FILE__, __FUNCTION__);

  if (callback_) {
    wl_callback_destroy(callback_);
    callback_ = nullptr;
  }

  cameraManager_->destroyCamera();

  fengine_->destroy(fscene_);
  fengine_->destroy(fview_);
  fengine_->destroy(fskybox_);
  fengine_->destroy(frenderer_);
  fengine_->destroy(fswapChain_);
  ::filament::Engine::destroy(&fengine_);

  modelLoader_.reset();

  if (subsurface_) {
    wl_subsurface_destroy(subsurface_);
    subsurface_ = nullptr;
  }

  if (surface_) {
    wl_surface_destroy(surface_);
    surface_ = nullptr;
  }
  SPDLOG_TRACE("--{}::{}", __FILE__, __FUNCTION__);
}

CustomModelViewer* CustomModelViewer::m_poInstance = nullptr;
CustomModelViewer* CustomModelViewer::Instance(const std::string& where) {
  if (m_poInstance == nullptr)
    SPDLOG_DEBUG("Instance is null {}", where.c_str());
  return m_poInstance;
}

void CustomModelViewer::setupMessageChannels(
    flutter::PluginRegistrar* plugin_registrar) {
  auto channel_name = std::string("plugin.filament_view.frame_view");

  frameViewCallback_ = std::make_unique<flutter::MethodChannel<>>(
      plugin_registrar->messenger(), channel_name,
      &flutter::StandardMethodCodec::GetInstance());
}

void CustomModelViewer::setupWaylandSubsurface() {
  // Ensure state_ is properly initialized
  if (!state_ || !state_->view_controller) {
    // Handle error: state_ or view_controller is not initialized
    spdlog::error("{}::{}::{}", __FILE__, __FUNCTION__, __LINE__);
    return;
  }

  const auto flutter_view = state_->view_controller->view;
  if (!flutter_view) {
    // Handle error: flutter_view is not initialized
    spdlog::error("{}::{}::{}", __FILE__, __FUNCTION__, __LINE__);
    return;
  }

  display_ = flutter_view->GetDisplay()->GetDisplay();
  if (!display_) {
    // Handle error: display is not initialized
    spdlog::error("{}::{}::{}", __FILE__, __FUNCTION__, __LINE__);
    return;
  }

  parent_surface_ = flutter_view->GetWindow()->GetBaseSurface();
  if (!parent_surface_) {
    // Handle error: parent_surface is not initialized
    spdlog::error("{}::{}::{}", __FILE__, __FUNCTION__, __LINE__);
    return;
  }

  surface_ =
      wl_compositor_create_surface(flutter_view->GetDisplay()->GetCompositor());
  if (!surface_) {
    // Handle error: failed to create surface
    spdlog::error("{}::{}::{}", __FILE__, __FUNCTION__, __LINE__);
    return;
  }

  subsurface_ = wl_subcompositor_get_subsurface(
      flutter_view->GetDisplay()->GetSubCompositor(), surface_,
      parent_surface_);
  if (!subsurface_) {
    // Handle error: failed to create subsurface
    spdlog::error("{}::{}::{}", __FILE__, __FUNCTION__, __LINE__);
    wl_surface_destroy(surface_);  // Clean up the surface
    return;
  }

  wl_subsurface_place_below(subsurface_, parent_surface_);
  wl_subsurface_set_desync(subsurface_);
}

std::future<bool> CustomModelViewer::Initialize(PlatformView* platformView) {
  SPDLOG_TRACE("++{}::{}", __FILE__, __FUNCTION__);

  m_poInstance = this;

  auto promise(std::make_shared<std::promise<bool>>());
  auto future(promise->get_future());

  asio::post(*strand_, [&, promise, platformView] {
    auto platform_view_size = platformView->GetSize();
    native_window_ = {
        .display = display_,
        .surface = surface_,
        .width = static_cast<uint32_t>(platform_view_size.first),
        .height = static_cast<uint32_t>(platform_view_size.second)};

    fengine_ = ::filament::Engine::create(::filament::Engine::Backend::VULKAN);
    fswapChain_ = fengine_->createSwapChain(&native_window_);
    frenderer_ = fengine_->createRenderer();

    fscene_ = fengine_->createScene();
    fview_ = fengine_->createView();

    setupView();

    modelLoader_ = std::make_unique<ModelManager>();

    promise->set_value(true);
  });
  SPDLOG_TRACE("--{}::{}", __FILE__, __FUNCTION__);
  return future;
}

void CustomModelViewer::setModelState(ModelState modelState) {
  currentModelState_ = modelState;
  SPDLOG_DEBUG("[FilamentView] setModelState: {}",
               getTextForModelState(currentModelState_));
}

void CustomModelViewer::setLightState(SceneState sceneState) {
  currentLightState_ = sceneState;
  SPDLOG_DEBUG("[FilamentView] setLightState: {}",
               getTextForSceneState(currentLightState_));
}

void CustomModelViewer::setSkyboxState(SceneState sceneState) {
  currentSkyboxState_ = sceneState;
  SPDLOG_DEBUG("[FilamentView] setSkyboxState: {}",
               getTextForSceneState(currentSkyboxState_));
}

void CustomModelViewer::destroyIndirectLight() {
  const auto scene = fview_->getScene();
  auto indirectLight = scene->getIndirectLight();
  if (indirectLight) {
    fengine_->destroy(indirectLight);
  }
}

void CustomModelViewer::destroySkybox() {
  auto scene = fview_->getScene();
  auto skybox = scene->getSkybox();
  if (skybox) {
    fengine_->destroy(skybox);
  }
}

void CustomModelViewer::setupView() {
  SPDLOG_TRACE("++{}::{}", __FILE__, __FUNCTION__);

  // on mobile, better use lower quality color buffer
  ::filament::View::RenderQuality renderQuality{};
  renderQuality.hdrColorBuffer = ::filament::View::QualityLevel::MEDIUM;
  fview_->setRenderQuality(renderQuality);

  // dynamic resolution often helps a lot
  fview_->setDynamicResolutionOptions(
      {.enabled = true, .quality = ::filament::View::QualityLevel::MEDIUM});

  // MSAA is needed with dynamic resolution MEDIUM
  fview_->setMultiSampleAntiAliasingOptions({.enabled = true});
  // fview_->setMultiSampleAntiAliasingOptions({.enabled = false});

  // FXAA is pretty economical and helps a lot
  fview_->setAntiAliasing(::filament::View::AntiAliasing::FXAA);
  // fview_->setAntiAliasing(filament::View::AntiAliasing::NONE);

  // ambient occlusion is the cheapest effect that adds a lot of quality
  fview_->setAmbientOcclusionOptions({.enabled = true});
  // fview_->setAmbientOcclusion(filament::View::AmbientOcclusion::NONE);

  // bloom is pretty expensive but adds a fair amount of realism
  // fview_->setBloomOptions({
  //     .enabled = false,
  // });

  fview_->setBloomOptions({
      .enabled = true,
  });

  // fview_->setShadowingEnabled(false);
  // fview_->setScreenSpaceRefractionEnabled(false);
  // fview_->setStencilBufferEnabled(false);
  // fview_->setDynamicLightingOptions(0.01, 1000.0f);

  SPDLOG_TRACE("--{}::{}", __FILE__, __FUNCTION__);
}

static uint32_t G_LastTime = 0;

void CustomModelViewer::SendFrameViewCallback(
    const std::string& methodName,
    std::initializer_list<std::pair<const char*, flutter::EncodableValue>>
        args) {
  if (frameViewCallback_ == nullptr) {
    return;
  }

  flutter::EncodableMap encodableMap;
  for (const auto& arg : args) {
    encodableMap[flutter::EncodableValue(arg.first)] = arg.second;
  }
  frameViewCallback_->InvokeMethod(methodName,
                                   std::make_unique<flutter::EncodableValue>(
                                       flutter::EncodableValue(encodableMap)));
}

/**
 * Renders the model and updates the Filament camera.
 *
 * @param time - timestamp of running program
 * rendered
 */
void CustomModelViewer::DrawFrame(uint32_t time) {
  asio::post(*strand_, [&, time]() {
    static bool bonce = true;
    if (bonce) {
      bonce = false;
      modelLoader_->updateAsyncAssetLoading();

      // will set the first frame of a cameras features.
      doCameraFeatures(0);
    }

    if (G_LastTime == 0) {
      G_LastTime = time;
    }

    // Frames from Native to dart, currently run in order of
    // - updateFrame - Called regardless if a frame is going to be drawn or not
    // - preRenderFrame - Called before native <features>, but we know we're
    // going to draw a frame
    // - renderFrame - Called after native <features>, right before drawing a
    // frame
    // - postRenderFrame - Called after we've drawn natively, right after
    // drawing a frame.

    SendFrameViewCallback(
        kUpdateFrame, {std::make_pair(kParam_ElapsedFrameTime,
                                      flutter::EncodableValue(G_LastTime))});

    // Render the scene, unless the renderer wants to skip the frame.
    if (frenderer_->beginFrame(fswapChain_, time)) {
      // Note you might want render time and gameplay time to be different
      // but for smooth animation you don't. (physics would be simulated w/o
      // render)
      //
      // Future tasking for making a more featured timing / frame info class.
      uint32_t deltaTimeMS = time - G_LastTime;
      float timeSinceLastRenderedSec =
          static_cast<float>(deltaTimeMS) / 1000.0f;  // convert to seconds
      if (timeSinceLastRenderedSec == 0.0f) {
        timeSinceLastRenderedSec += 1.0f;
      }
      float fps = 1.0f / timeSinceLastRenderedSec;  // calculate FPS

      SendFrameViewCallback(
          kPreRenderFrame,
          {std::make_pair(kParam_TimeSinceLastRenderedSec,
                          flutter::EncodableValue(timeSinceLastRenderedSec)),
           std::make_pair(kParam_FPS, flutter::EncodableValue(fps))});

      doCameraFeatures(timeSinceLastRenderedSec);

      SendFrameViewCallback(
          kRenderFrame,
          {std::make_pair(kParam_TimeSinceLastRenderedSec,
                          flutter::EncodableValue(timeSinceLastRenderedSec)),
           std::make_pair(kParam_FPS, flutter::EncodableValue(fps))});

      frenderer_->render(fview_);
      frenderer_->endFrame();

      SendFrameViewCallback(
          kPostRenderFrame,
          {std::make_pair(kParam_TimeSinceLastRenderedSec,
                          flutter::EncodableValue(timeSinceLastRenderedSec)),
           std::make_pair(kParam_FPS, flutter::EncodableValue(fps))});
    }

    G_LastTime = time;
  });
}

void CustomModelViewer::OnFrame(void* data,
                                wl_callback* callback,
                                const uint32_t time) {
  const auto obj = static_cast<CustomModelViewer*>(data);

  obj->callback_ = nullptr;

  if (callback) {
    wl_callback_destroy(callback);
  }

  obj->DrawFrame(time);

  obj->callback_ = wl_surface_frame(obj->surface_);
  wl_callback_add_listener(obj->callback_, &CustomModelViewer::frame_listener,
                           data);

  // Z-Order
  // These do not need <seem> to need to be called every frame.
  // wl_subsurface_place_below(obj->subsurface_, obj->parent_surface_);
  wl_subsurface_set_position(obj->subsurface_, obj->left_, obj->top_);

  wl_surface_commit(obj->surface_);
}

/////////////////////////////////////////////////////////////////////////
void CustomModelViewer::doCameraFeatures(float fDeltaTime) {
  cameraManager_->updateCamerasFeatures(fDeltaTime);
}

const wl_callback_listener CustomModelViewer::frame_listener = {.done =
                                                                    OnFrame};

void CustomModelViewer::setOffset(double left, double top) {
  left_ = static_cast<int32_t>(left);
  top_ = static_cast<int32_t>(top);
}

void CustomModelViewer::resize(double width, double height) {
  fview_->setViewport({left_, top_, static_cast<uint32_t>(width),
                       static_cast<uint32_t>(height)});
  cameraManager_->updateCameraOnResize(static_cast<uint32_t>(width),
                                       static_cast<uint32_t>(height));
}

}  // namespace plugin_filament_view
