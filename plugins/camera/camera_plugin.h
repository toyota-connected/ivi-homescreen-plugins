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

#ifndef FLUTTER_PLUGIN_CAMERA_PLUGIN_H_
#define FLUTTER_PLUGIN_CAMERA_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar.h>

#include <libcamera/camera.h>

#include "camera_session.h"

#include "camera_context.h"

#include "event_channel.h"
//#include "messages.h"
#include "messages.g.h"
#include "plugins/common/common.h"

namespace camera_plugin {

class CameraPlugin final : public flutter::Plugin, public CameraApi {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarDesktop* registrar);

  CameraPlugin(flutter::PluginRegistrarDesktop* plugin_registrar,
               flutter::BinaryMessenger* messenger);

  ~CameraPlugin() override;

    // Returns the names of all of the available capture devices.
    ErrorOr<flutter::EncodableList> GetAvailableCameras() override;
    // Creates a camera instance for the given device name and settings.
    void Create(const std::string& camera_name,
                const PlatformMediaSettings& settings,
                std::function<void(ErrorOr<int64_t> reply)> result) override;
    // Initializes a camera, and returns the size of its preview.
    void Initialize(
        int64_t camera_id,
        std::function<void(ErrorOr<PlatformSize> reply)> result) override;
    // Disposes a camera that is no longer in use.
    virtual std::optional<FlutterError> Dispose(int64_t camera_id) override;
    // Takes a picture with the given camera, and returns the path to the
    // resulting file.
    void TakePicture(
        int64_t camera_id,
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
        int64_t camera_id,
        std::function<void(std::optional<FlutterError> reply)> result) override;
    // Resumes the preview stream for the given camera.
    void ResumePreview(
        int64_t camera_id,
        std::function<void(std::optional<FlutterError> reply)> result) override;

    /*
    // Creates a camera instance for the given device name and settings.
    void Create(const std::string& camera_name,
                const PlatformMediaSettings& settings,
                std::function<void(ErrorOr<int64_t> reply)> result) override;
    // Initializes a camera, and returns the size of its preview.
    void Initialize(
        int64_t camera_id,
        std::function<void(ErrorOr<PlatformSize> reply)> result) override;
    // Disposes a camera that is no longer in use.
    virtual std::optional<FlutterError> Dispose(int64_t camera_id) override;
    // Takes a picture with the given camera, and returns the path to the
    // resulting file.
    void TakePicture(
        int64_t camera_id,
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
        int64_t camera_id,
        std::function<void(std::optional<FlutterError> reply)> result) override;
    // Resumes the preview stream for the given camera.
    void ResumePreview(
        int64_t camera_id,
        std::function<void(std::optional<FlutterError> reply)> result) override;
*/
#if 0
  void availableCameras(
      std::function<void(ErrorOr<flutter::EncodableList> reply)> result)
      override;
  void create(const flutter::EncodableMap& args,
              std::function<void(ErrorOr<flutter::EncodableMap> reply)> result)
      override;
  void initialize(
      const flutter::EncodableMap& args,
      std::function<void(ErrorOr<std::string> reply)> result) override;
  void takePicture(
      const flutter::EncodableMap& args,
      std::function<void(ErrorOr<std::string> reply)> result) override;
  void startVideoRecording(
      const flutter::EncodableMap& args,
      std::function<void(std::optional<FlutterError> reply)> result) override;
  void pauseVideoRecording(
      const flutter::EncodableMap& args,
      std::function<void(std::optional<FlutterError> reply)> result) override;
  void resumeVideoRecording(
      const flutter::EncodableMap& args,
      std::function<void(std::optional<FlutterError> reply)> result) override;
  void stopVideoRecording(
      const flutter::EncodableMap& args,
      std::function<void(ErrorOr<std::string> reply)> result) override;
  void pausePreview(const flutter::EncodableMap& args,
                    std::function<void(ErrorOr<double> reply)> result) override;
  void resumePreview(
      const flutter::EncodableMap& args,
      std::function<void(ErrorOr<double> reply)> result) override;
  void lockCaptureOrientation(
      const flutter::EncodableMap& args,
      std::function<void(ErrorOr<std::string>)> result) override;
  void unlockCaptureOrientation(
      const flutter::EncodableMap& args,
      std::function<void(ErrorOr<std::string>)> result) override;
  void setFlashMode(
      const flutter::EncodableMap& args,
      std::function<void(std::optional<FlutterError> reply)> result) override;
  void setFocusMode(
      const flutter::EncodableMap& args,
      std::function<void(std::optional<FlutterError> reply)> result) override;
  void setExposureMode(
      const flutter::EncodableMap& args,
      std::function<void(std::optional<FlutterError> reply)> result) override;
  void getExposureOffsetStepSize(
      const flutter::EncodableMap& args,
      std::function<void(ErrorOr<double> reply)> result) override;
  void setExposurePoint(
      const flutter::EncodableMap& args,
      std::function<void(std::optional<FlutterError> reply)> result) override;
  void setFocusPoint(
      const flutter::EncodableMap& args,
      std::function<void(std::optional<FlutterError> reply)> result) override;
  void setExposureOffset(
      const flutter::EncodableMap& args,
      std::function<void(ErrorOr<double> reply)> result) override;
  void getMinExposureOffset(
      const flutter::EncodableMap& args,
      std::function<void(ErrorOr<double> reply)> result) override;
  void getMaxExposureOffset(
      const flutter::EncodableMap& args,
      std::function<void(ErrorOr<double> reply)> result) override;
  void getMaxZoomLevel(
      const flutter::EncodableMap& args,
      std::function<void(ErrorOr<double> reply)> result) override;
  void getMinZoomLevel(
      const flutter::EncodableMap& args,
      std::function<void(ErrorOr<double> reply)> result) override;
  void dispose(
      const flutter::EncodableMap& args,
      std::function<void(std::optional<FlutterError> reply)> result) override;
#endif
  // Disallow copy and assign.
  CameraPlugin(const CameraPlugin&) = delete;
  CameraPlugin& operator=(const CameraPlugin&) = delete;

 private:
  flutter::PluginRegistrarDesktop* registrar_{};
  flutter::BinaryMessenger* messenger_;
  std::map<std::string,
           std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>>>
      event_channels_;
  std::map<std::string, std::unique_ptr<flutter::StreamHandler<>>>
      stream_handlers_;

  std::map<std::string, int> CameraName_TextureId;
  std::thread thread_;
  std::unique_ptr<asio::io_context> io_context_;
  asio::executor_work_guard<decltype(io_context_->get_executor())> work_;
  std::unique_ptr<asio::io_context::strand> strand_;

  static void camera_added(const std::shared_ptr<libcamera::Camera>& cam);
  static void camera_removed(const std::shared_ptr<libcamera::Camera>& cam);

  static std::string get_camera_lens_facing(
      const std::shared_ptr<libcamera::Camera>& camera);

  static std::optional<std::string> GetFilePathForPicture();
  static std::optional<std::string> GetFilePathForVideo();

  std::string RegisterEventChannel(
      const std::string& prefix,
      const std::string& uid,
      std::unique_ptr<flutter::StreamHandler<flutter::EncodableValue>> handler);
};
}  // namespace camera_plugin

#endif  // FLUTTER_PLUGIN_CAMERA_PLUGIN_H_