/*
 * Copyright 2020-2025 Toyota Connected North America
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

#ifndef PLUGINS_CAMERA_PIPEWIRE_CAMERA_PLUGIN_H
#define PLUGINS_CAMERA_PIPEWIRE_CAMERA_PLUGIN_H

#include <optional>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar.h>
#include <flutter/plugin_registrar_homescreen.h>

#include "CameraStream.h"
#include "event_channel.h"
#include "messages.g.h"
#include "plugins/common/common.h"

namespace camera_plugin {

class CameraPlugin final : public flutter::Plugin, public CameraApi {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarDesktop* registrar);
  void SendI420Frame(const uint8_t* y,
                     int y_stride,
                     const uint8_t* u,
                     int u_stride,
                     const uint8_t* v,
                     int v_stride,
                     int width,
                     int height) const;

  CameraPlugin(flutter::PluginRegistrarDesktop* plugin_registrar,
               flutter::BinaryMessenger* messenger);

  ~CameraPlugin() override;

  // Returns the names of all the available capture devices.
  ErrorOr<flutter::EncodableList> GetAvailableCameras() override;
  // Creates a camera instance for the given device name and settings.
  void Create(const std::string& camera_id,
              const PlatformMediaSettings& settings,
              std::function<void(ErrorOr<int64_t> reply)> result) override;
  // Initializes a camera and returns the size of its preview.
  void Initialize(
      int64_t texture_id,
      std::function<void(ErrorOr<PlatformSize> reply)> result) override;
  // Disposes a camera that is no longer in use.
  std::optional<FlutterError> Dispose(int64_t texture_id) override;
  // Takes a picture with the given camera and returns the path to the
  // resulting file.
  void TakePicture(
      int64_t texture_id,
      std::function<void(ErrorOr<std::string> reply)> result) override;
  // Starts recording video with the given camera.
  void StartVideoRecording(
      int64_t camera_id,
      std::function<void(std::optional<FlutterError> reply)> result) override;
  // Finishes recording video with the given camera, and returns the path to
  // the resulting file.
  void StopVideoRecording(
      int64_t camera_id,
      std::function<void(ErrorOr<std::string> reply)> result) override;
  // Starts the preview stream for the given camera.
  void PausePreview(
      int64_t texture_id,
      std::function<void(std::optional<FlutterError> reply)> result) override;
  // Resumes the preview stream for the given camera.
  void ResumePreview(
      int64_t texture_id,
      std::function<void(std::optional<FlutterError> reply)> result) override;
  void blit_fb(uint8_t const* pixels) const;

  // Disallow copy and assign.
  CameraPlugin(const CameraPlugin&) = delete;
  CameraPlugin& operator=(const CameraPlugin&) = delete;

 private:
  flutter::TextureRegistrar* texture_registrar_{};
  bool image_stream_active_ = false;

  struct preview {
    bool is_initialized{};

    // The internal Flutter event channel instance.
    flutter::EventChannel<flutter::EncodableValue>* event_channel_;

    // The internal Flutter event sink instance, used to send events to the Dart
    // side.
    std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> event_sink;

    GLuint textureId{};
    GLuint framebuffer{};
    GLuint program;
    GLsizei width, height;
    GLuint vertex_arr_id_{};

    // The Surface Descriptor sent to Flutter when a texture frame is available.
    std::unique_ptr<flutter::GpuSurfaceTexture> gpu_surface_texture;
    FlutterDesktopGpuSurfaceDescriptor descriptor{};
  } mPreview;

  flutter::PluginRegistrarDesktop* registrar_{};
  std::map<std::string,
           std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>>>
      event_channels_;
  std::map<std::string, std::unique_ptr<flutter::StreamHandler<>>>
      stream_handlers_;

  std::map<std::string, std::shared_ptr<CameraStream>> CameraId_CameraStream;
  std::map<GLuint, std::shared_ptr<CameraStream>> TextureId_CameraStream;

  std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>>
      image_channel_;
  std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> image_sink_;
  void StartImageStream();
  void StopImageStream();
};
}  // namespace camera_plugin

#endif  // PLUGINS_CAMERA_PIPEWIRE_CAMERA_PLUGIN_H
