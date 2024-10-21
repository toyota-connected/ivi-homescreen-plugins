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

#pragma once

#include <core/scene/camera/camera.h>
#include <core/scene/camera/camera_manager.h>
#include <filament/Engine.h>
#include <flutter_desktop_plugin_registrar.h>
#include <gltfio/AssetLoader.h>
#include <viewer/Settings.h>
#include <asio/io_context_strand.hpp>
#include <cstdint>

namespace plugin_filament_view {

class Camera;
class CameraManager;

class ViewTarget {
 public:
  ViewTarget(int32_t top, int32_t left, FlutterDesktopEngineState* state);

  ~ViewTarget();

  enum ePredefinedQualitySettings { Lowest, Low, Medium, High, Ultra };

  // Disallow copy and assign.
  ViewTarget(const ViewTarget&) = delete;
  ViewTarget& operator=(const ViewTarget&) = delete;

  void setAnimator(filament::gltfio::Animator* animator) {
    fanimator_ = animator;
  }

  void setupMessageChannels(flutter::PluginRegistrar* plugin_registrar);

  filament::viewer::Settings& getSettings() { return settings_; }

  filament::gltfio::FilamentAsset* getAsset() { return asset_; }

  void setInitialized() {
    if (initialized_)
      return;

    initialized_ = true;
    OnFrame(this, nullptr, 0);
  }

  [[nodiscard]] ::filament::View* getFilamentView() const { return fview_; }

  void setOffset(double left, double top);

  void resize(double width, double height);

  void InitializeFilamentInternals(uint32_t width, uint32_t height);

  void vSetupCameraManagerWithDeserializedCamera(
      std::unique_ptr<Camera> camera) const;

  void vOnTouch(int32_t action,
                int32_t point_count,
                size_t point_data_size,
                const double* point_data) const;

  [[nodiscard]] CameraManager* getCameraManager() const {
    return cameraManager_.get();
  }

  void vChangeQualitySettings(ePredefinedQualitySettings qualitySettings) const;

 private:
  void setupWaylandSubsurface();

  [[maybe_unused]] FlutterDesktopEngineState* state_;
  filament::viewer::Settings settings_;
  filament::gltfio::FilamentAsset* asset_{};
  int32_t left_;
  int32_t top_;

  bool initialized_{};

  std::unique_ptr<flutter::MethodChannel<>> frameViewCallback_;

  wl_display* display_{};
  wl_surface* surface_{};
  wl_surface* parent_surface_{};
  wl_callback* callback_;
  wl_subsurface* subsurface_{};

  struct _native_window {
    struct wl_display* display;
    struct wl_surface* surface;
    uint32_t width;
    uint32_t height;
  } native_window_{};

  ::filament::SwapChain* fswapChain_{};
  ::filament::View* fview_{};

  // todo to be moved
  ::filament::gltfio::Animator* fanimator_;

  void SendFrameViewCallback(
      const std::string& methodName,
      std::initializer_list<std::pair<const char*, flutter::EncodableValue>>
          args) const;

  static void OnFrame(void* data, wl_callback* callback, uint32_t time);

  static const wl_callback_listener frame_listener;

  void DrawFrame(uint32_t time);

  void setupView(uint32_t width, uint32_t height);

  // elapsed time / deltatime needs to be moved to its own global namespace like
  // class similar to unitys, elapsedtime/total time etc.
  void doCameraFeatures(float fDeltaTime) const;

  uint32_t m_LastTime = 0;

  std::unique_ptr<CameraManager> cameraManager_;
};

}  // namespace plugin_filament_view
