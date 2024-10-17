/*
 * Copyright 2023-2024 Toyota Connected North America
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
#ifndef FLUTTER_PLUGIN_CAMERA_CONTEXT_H_
#define FLUTTER_PLUGIN_CAMERA_CONTEXT_H_

#include <flutter/basic_message_channel.h>
#include <flutter/event_channel.h>

#include <libcamera/libcamera.h>

#include "engine.h"
#include "frame_sink.h"
#include "messages.g.h"
#include <fstream>

namespace camera_plugin {
class FlutterError;

class CameraSession : public flutter::Plugin {
 public:
  typedef enum {
    CAM_STATE_AVAILABLE,
    CAM_STATE_ACQUIRED,
    CAM_STATE_CONFIGURED,
    CAM_STATE_RUNNING,
    CAM_STATE_STOPPING,
  } CAM_STATE_T;

  CameraSession(flutter::PluginRegistrar* plugin_registrar,
                std::string cameraName,
                const PlatformMediaSettings& settings,
                std::shared_ptr<libcamera::Camera> camera,
                asio::io_context::strand* strand);

  ~CameraSession() override;

  void setCamera(std::shared_ptr<libcamera::Camera> camera);

  std::string Initialize(int64_t camera_id,
                         const std::string& image_format_group);

  [[nodiscard]] GLuint get_texture_id() const { return texture_id_; }

  [[nodiscard]] const std::string& get_libcamera_id() const { return libcamera_id_; }

  [[nodiscard]] CAM_STATE_T get_camera_state() const { return mCameraState; }

  static std::optional<std::string> GetFilePathForPicture();

  static std::optional<std::string> GetFilePathForVideo();

  std::string takePicture();

  double pausePreview();
  double resumePreview();

  std::optional<FlutterError> setFlashMode(const std::string& mode);
  std::optional<FlutterError> setFocusMode(const std::string& mode);

  void startVideoRecording(bool enableStream);
  void pauseVideoRecording();
  void resumeVideoRecording();
  std::string stopVideoRecording();

  [[nodiscard]] double getMinExposureOffset() const;
  [[nodiscard]] double getMaxExposureOffset() const;
  [[nodiscard]] double getExposureOffsetStepSize() const;
  double setExposureOffset(double offset);
  [[nodiscard]] bool getAutoExposureEnable() const;

  [[nodiscard]] double getMinZoomLevel() const;
  [[nodiscard]] double getMaxZoomLevel() const;

  libcamera::Signal<> captureDone;

  [[nodiscard]] PlatformSize getPlatformSize() const;


private:
  std::mutex camera_stop_mutex_;

  GLuint texture_id_{};
  std::string libcamera_id_;
  std::string camera_name_;
  asio::io_context::strand* strand_;
  std::shared_ptr<libcamera::Camera> camera_{};
  flutter::PluginRegistrar* plugin_registrar_{};
  const PlatformMediaSettings platform_media_settings_;
  uint64_t last_;

  GLsizei width_{};
  GLsizei height_{};

  std::unique_ptr<flutter::MethodChannel<>> camera_channel_;

  CAM_STATE_T mCameraState;
  std::unique_ptr<libcamera::CameraConfiguration> mConfig;

  // The internal Flutter event channel instance.
  std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>>
      event_channel_;

  // The internal Flutter event sink instance, used to send events to the Dart
  // side.
  std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> event_sink_;

  std::string mImageFormatGroup;
  std::string mVideoFilename;

  std::map<const libcamera::Stream*, std::string> mStreamNames;
  std::unique_ptr<FrameSink> mSink;

  unsigned int mQueueCount{};

  std::unique_ptr<libcamera::FrameBufferAllocator> mAllocator;
  std::vector<std::unique_ptr<libcamera::Request>> mRequests;

  flutter::MethodChannel<>* GetMethodChannel();

  int start();
  int start_capture();
  int queue_request(libcamera::Request* request);
  void request_complete(libcamera::Request* request);
  void process_request(libcamera::Request* request);
  void sink_release(libcamera::Request* request);

  static void print_platform_media_settins(const PlatformMediaSettings& settings);

};
}  // namespace camera_plugin
#endif  // FLUTTER_PLUGIN_CAMERA_CONTEXT_H_
