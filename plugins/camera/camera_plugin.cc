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

#include "camera_plugin.h"

#include <memory>
#include <unordered_map>

#include <libcamera/libcamera.h>

#include "camera_session.h"

#include "plugins/common/common.h"

using namespace plugin_common;

namespace camera_plugin {

// TODO static constexpr char kKeyMaxVideoDuration[] = "maxVideoDuration";

// TODO static constexpr char kResolutionPresetValueLow[] = "low";
// TODO static constexpr char kResolutionPresetValueMedium[] = "medium";
// TODO static constexpr char kResolutionPresetValueHigh[] = "high";
// TODO static constexpr char kResolutionPresetValueVeryHigh[] = "veryHigh";
// TODO static constexpr char kResolutionPresetValueUltraHigh[] = "ultraHigh";
// TODO static constexpr char kResolutionPresetValueMax[] = "max";

static std::unique_ptr<libcamera::CameraManager> g_camera_manager;
//static std::vector<std::shared_ptr<CameraContext>> g_cameras;
static std::unordered_map<unsigned int, std::shared_ptr<CameraSession>>
    g_camera_sessions;

// static
void CameraPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarDesktop* registrar) {
  auto plugin =
      std::make_unique<CameraPlugin>(registrar, registrar->messenger());
  CameraPlugin::SetUp(registrar->messenger(), plugin.get());
  registrar->AddPlugin(std::move(plugin));
}

CameraPlugin::CameraPlugin(flutter::PluginRegistrarDesktop* plugin_registrar,
                           flutter::BinaryMessenger* messenger)
    : registrar_(plugin_registrar),
      messenger_(messenger),
      io_context_(std::make_unique<asio::io_context>(ASIO_CONCURRENCY_HINT_1)),
      work_(io_context_->get_executor()),
      strand_(std::make_unique<asio::io_context::strand>(*io_context_)) {
  thread_ = std::thread([&]() { io_context_->run(); });
  g_camera_manager = std::make_unique<libcamera::CameraManager>();
  g_camera_manager->cameraAdded.connect(this, &CameraPlugin::camera_added);
  g_camera_manager->cameraRemoved.connect(this, &CameraPlugin::camera_removed);

  spdlog::debug("[camera_plugin] libcamera {}", g_camera_manager->version());

  auto res = g_camera_manager->start();
  if (res != 0) {
    spdlog::critical("Failed to start camera manager: {}", strerror(-res));
  }
}

CameraPlugin::~CameraPlugin() {
  io_context_->run();
  thread_.join();

  g_camera_manager->stop();
  for (auto& [texture_id, camera] : g_camera_sessions) {
    camera.reset();
  }
}

void CameraPlugin::camera_added(const std::shared_ptr<libcamera::Camera>& cam) {
  spdlog::debug("[camera_plugin] Camera added: {}", cam->id());
}

void CameraPlugin::camera_removed(
    const std::shared_ptr<libcamera::Camera>& cam) {
  spdlog::debug("[camera_plugin] Camera removed: {}", cam->id());
  for (const auto& [texture_id, camera] : g_camera_sessions) {
    if (camera->get_libcamera_id() == cam->id()) {
      switch (camera->get_camera_state()) {
        case CameraSession::CAM_STATE_RUNNING:
          cam->stop();
        case CameraSession::CAM_STATE_ACQUIRED:
        case CameraSession::CAM_STATE_CONFIGURED:
          cam->release();
          break;
        default:
          break;
      }
    }
  }
}

std::string CameraPlugin::get_camera_lens_facing(
    const std::shared_ptr<libcamera::Camera>& camera) {
  const libcamera::ControlList& props = camera->properties();
  std::string lensFacing;

  // If location is specified use it, otherwise select external
  if (const auto& location = props.get(libcamera::properties::Location)) {
    switch (*location) {
      case libcamera::properties::CameraLocationFront:
        lensFacing = "front";
        break;
      case libcamera::properties::CameraLocationBack:
        lensFacing = "back";
        break;
      case libcamera::properties::CameraLocationExternal:
        lensFacing = "external";
        break;
      default:;
    }
  } else {
    lensFacing = "external";
  }
  return std::move(lensFacing);
}

ErrorOr<flutter::EncodableList> CameraPlugin::GetAvailableCameras() {
  spdlog::debug("[camera_plugin] availableCameras:");

  const auto cameras = g_camera_manager->cameras();
  flutter::EncodableList list;
  for (auto const& camera : cameras) {
    std::string id = camera->id();
    //   std::string lensFacing = get_camera_lens_facing(camera);
    //   int64_t sensorOrientation = 0;
    spdlog::debug("\tid: {}", id);
    //    spdlog::debug("\tlensFacing: {}", lensFacing);
    //    spdlog::debug("\tsensorOrientation: {}", sensorOrientation);
    list.emplace_back(flutter::EncodableValue(std::move(id)));
  }
  return list;
}

void CameraPlugin::Create(
    const std::string& camera_name,
    const PlatformMediaSettings& settings,
    const std::function<void(ErrorOr<int64_t> reply)> result) {
  spdlog::debug("[camera_plugin] create:");

  if(CameraName_TextureId.find(camera_name)==CameraName_TextureId.end()) {
    auto camera = std::make_shared<CameraSession>(
    registrar_, camera_name.c_str(), settings,
    g_camera_manager->get(camera_name), strand_.get());

    const auto texture_id = camera->get_texture_id();
    g_camera_sessions[texture_id] = std::move(camera);
    CameraName_TextureId.insert({camera_name, texture_id});
    spdlog::debug("2. size of g_camera_sessions={}",g_camera_sessions.size());
    spdlog::debug("!!!!! camera_id: {}",g_camera_sessions[texture_id]->get_libcamera_id());

    result(texture_id);
  }
  else {
    result(CameraName_TextureId[camera_name]);
  }
}

void CameraPlugin::Initialize(
    const int64_t camera_id,
    const std::function<void(ErrorOr<PlatformSize> reply)> result) {
  if (g_camera_sessions.find(camera_id) == g_camera_sessions.end()) {
    result(FlutterError("Invalid camera_id"));
    return;
  }

  const auto camera = g_camera_sessions[camera_id];
  camera->setCamera(g_camera_manager->get(camera->get_libcamera_id()));
  const auto channel_name = camera->Initialize(camera_id, "JPEG");
  result(PlatformSize(camera->getPlatformSize()));
}

std::optional<FlutterError> CameraPlugin::Dispose(const int64_t camera_id) {
  auto camera = g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  camera.reset();
  SPDLOG_DEBUG("[camera_plugin] dispose: {}", camera_id);
  return {};
}

void CameraPlugin::TakePicture(
    const int64_t camera_id,
    const std::function<void(ErrorOr<std::string> reply)> result) {

  //const auto camera =
  //    g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  const auto camera = g_camera_sessions[camera_id];
  //camera->pausePreview();
/*
  SPDLOG_DEBUG("[camera_plugin] pause the camera: {}");

  libcamera::StreamRole stream_roles = { libcamera::StreamRole::StillCapture };
  //std::unique_ptr<libcamera::CameraConfiguration> config =
    //camera->generateConfiguration(stream_roles);
  //camera->
  camera->resumePreview();
*/
  result(camera->takePicture());
}

void CameraPlugin::StartVideoRecording(
    const int64_t camera_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  bool enable_stream{};

  const auto camera =
      g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  camera->startVideoRecording(enable_stream);

  result({});
}

void CameraPlugin::StopVideoRecording(
    const int64_t camera_id,
    const std::function<void(ErrorOr<std::string> reply)> result) {
  const auto camera =
      g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  result(camera->stopVideoRecording());
}

void CameraPlugin::PausePreview(
    const int64_t camera_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  //const auto camera =
  //    g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  const auto camera = g_camera_sessions[camera_id];
  if(camera) {
    SPDLOG_DEBUG("[camera_plugin] texture_id: {}", camera->get_texture_id());
    camera->pausePreview();
  }
  else
    SPDLOG_DEBUG("[camera_plugin] no camera session was found!!");

  result({});
}

void CameraPlugin::ResumePreview(
    const int64_t camera_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  //const auto camera =
  //g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  const auto camera = g_camera_sessions[camera_id];
  camera->resumePreview();
  result({});
}
/*
void CameraPlugin::Create(
    const std::string& camera_name,
    const PlatformMediaSettings& settings,
    const std::function<void(ErrorOr<int64_t> reply)> result) {
  spdlog::debug("[camera_plugin] create:");

  if(CameraName_TextureId.find(camera_name)==CameraName_TextureId.end()) {
    auto camera = std::make_shared<CameraSession>(
    registrar_, camera_name.c_str(), settings,
    g_camera_manager->get(camera_name), strand_.get());

    const auto texture_id = camera->get_texture_id();
    g_camera_sessions[texture_id] = std::move(camera);
    CameraName_TextureId.insert({camera_name, texture_id});
    spdlog::debug("2. size of g_camera_sessions={}",g_camera_sessions.size());
    spdlog::debug("!!!!! camera_id: {}",g_camera_sessions[texture_id]->get_libcamera_id());

    result(texture_id);
  }
  else {
    result(CameraName_TextureId[camera_name]);
  }
}

void CameraPlugin::Initialize(
    const int64_t camera_id,
    const std::function<void(ErrorOr<PlatformSize> reply)> result) {
  if (g_camera_sessions.find(camera_id) == g_camera_sessions.end()) {
    result(FlutterError("Invalid camera_id"));
    return;
  }

  const auto camera = g_camera_sessions[camera_id];
  camera->setCamera(g_camera_manager->get(camera->get_libcamera_id()));
  const auto channel_name = camera->Initialize(camera_id, "JPEG");
  result(PlatformSize(camera->getPlatformSize()));
}

std::optional<FlutterError> CameraPlugin::Dispose(const int64_t camera_id) {
  auto camera = g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  camera.reset();
  SPDLOG_DEBUG("[camera_plugin] dispose: {}", camera_id);
  return {};
}

void CameraPlugin::TakePicture(
    const int64_t camera_id,
    const std::function<void(ErrorOr<std::string> reply)> result) {

  //const auto camera =
  //    g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  const auto camera = g_camera_sessions[camera_id];
  //camera->pausePreview();
  result(camera->takePicture());
}

void CameraPlugin::StartVideoRecording(
    const int64_t camera_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  bool enable_stream{};

  const auto camera =
      g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  camera->startVideoRecording(enable_stream);

  result({});
}

void CameraPlugin::StopVideoRecording(
    const int64_t camera_id,
    const std::function<void(ErrorOr<std::string> reply)> result) {
  const auto camera =
      g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  result(camera->stopVideoRecording());
}

void CameraPlugin::PausePreview(
    const int64_t camera_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  //const auto camera =
  //    g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  const auto camera = g_camera_sessions[camera_id];
  if(camera) {
    SPDLOG_DEBUG("[camera_plugin] texture_id: {}", camera->get_texture_id());
    camera->pausePreview();
  }
  else
    SPDLOG_DEBUG("[camera_plugin] no camera session was found!!");

  result({});
}

void CameraPlugin::ResumePreview(
    const int64_t camera_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  //const auto camera =
      //g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  const auto camera = g_camera_sessions[camera_id];
  camera->resumePreview();
  result({});
}
*/
/*
void CameraPlugin::availableCameras(
    const std::function<void(ErrorOr<flutter::EncodableList> reply)> result) {
  spdlog::debug("[camera_plugin] availableCameras:");

  const auto cameras = g_camera_manager->cameras();
  flutter::EncodableList list;
  for (auto const& camera : cameras) {
    std::string id = camera->id();
    std::string lensFacing = get_camera_lens_facing(camera);
    int64_t sensorOrientation = 0;
    spdlog::debug("\tid: {}", id);
    spdlog::debug("\tlensFacing: {}", lensFacing);
    spdlog::debug("\tsensorOrientation: {}", sensorOrientation);
    list.emplace_back(
        flutter::EncodableMap{{flutter::EncodableValue("name"),
                               flutter::EncodableValue(std::move(id))},
                              {flutter::EncodableValue("lensFacing"),
                               flutter::EncodableValue(std::move(lensFacing))},
                              {flutter::EncodableValue("sensorOrientation"),
                               flutter::EncodableValue(sensorOrientation)}});
  }
  result(ErrorOr(list));
}

void CameraPlugin::create(
    const flutter::EncodableMap& args,
    const std::function<void(ErrorOr<flutter::EncodableMap> reply)> result) {
  spdlog::debug("[camera_plugin] create:");
  Encodable::PrintFlutterEncodableMap("create", args);

  // method arguments
  std::string cameraName;
  std::string resolutionPreset;
  int64_t fps = 0;
  int64_t videoBitrate = 0;
  int64_t audioBitrate = 0;
  bool enableAudio{};

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraName" && std::holds_alternative<std::string>(snd)) {
      cameraName = std::get<std::string>(snd);
    } else if (key == "resolutionPreset" &&
               std::holds_alternative<std::string>(snd)) {
      resolutionPreset = std::get<std::string>(snd);
    } else if (key == "fps" && std::holds_alternative<int64_t>(snd)) {
      fps = std::get<int64_t>(snd);
    } else if (key == "videoBitrate" && std::holds_alternative<int64_t>(snd)) {
      videoBitrate = std::get<int64_t>(snd);
    } else if (key == "audioBitrate" && std::holds_alternative<int64_t>(snd)) {
      audioBitrate = std::get<int64_t>(snd);
    } else if (key == "enableAudio" && std::holds_alternative<bool>(snd)) {
      enableAudio = std::get<bool>(snd);
    }
  }
  // Create Camera instance
  auto camera = std::make_shared<CameraContext>(
      cameraName, resolutionPreset, fps, videoBitrate, audioBitrate,
      enableAudio, g_camera_manager->get(cameraName));
  g_cameras.emplace_back(std::move(camera));

  auto map = flutter::EncodableMap();
  map[flutter::EncodableValue("cameraId")] =
      static_cast<int64_t>(g_cameras.size());
  result(ErrorOr(map));
}

void CameraPlugin::initialize(
    const flutter::EncodableMap& args,
    const std::function<void(ErrorOr<std::string> reply)> result) {
  // method arguments
  int32_t cameraId = 0;
  std::string imageFormatGroup;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    } else if (key == "imageFormatGroup" &&
               std::holds_alternative<std::string>(snd)) {
      imageFormatGroup = std::get<std::string>(snd);
    }
  }

  // Initialize Camera
  if (cameraId - 1 < g_cameras.size()) {
    const auto& camera = g_cameras[static_cast<unsigned long>(cameraId - 1)];
    if (!camera) {
      spdlog::error("Invalid cameraId");
      result(ErrorOr<std::string>("Invalid cameraId"));
      return;
    }
    const auto id = camera->getCameraId();

    camera->setCamera(g_camera_manager->get(id));
    const auto channel_name =
        camera->Initialize(registrar_, cameraId, imageFormatGroup);
    result(ErrorOr(channel_name));
  }
}

void CameraPlugin::takePicture(
    const flutter::EncodableMap& args,
    const std::function<void(ErrorOr<std::string> reply)> result) {
  // method arguments
  int32_t cameraId = 0;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    }
  }

  g_cameras[static_cast<unsigned long>(cameraId - 1)];
  result(ErrorOr(CameraContext::takePicture()));
}

void CameraPlugin::startVideoRecording(
    const flutter::EncodableMap& args,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  Encodable::PrintFlutterEncodableMap("startVideoRecording", args);
  // method arguments
  int32_t cameraId = 0;
  bool enableStream{};

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    } else if (key == "enableStream" && std::holds_alternative<bool>(snd)) {
      enableStream = std::get<bool>(snd);
    }
  }

  const auto& camera = g_cameras[static_cast<unsigned long>(cameraId - 1)];
  camera->startVideoRecording(enableStream);

  result(std::nullopt);
}

void CameraPlugin::pauseVideoRecording(
    const flutter::EncodableMap& args,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  Encodable::PrintFlutterEncodableMap("pauseVideoRecording", args);
  // method arguments
  int32_t cameraId = 0;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    }
  }

  const auto& camera = g_cameras[static_cast<unsigned long>(cameraId - 1)];
  camera->pauseVideoRecording();

  result(std::nullopt);
}

void CameraPlugin::resumeVideoRecording(
    const flutter::EncodableMap& args,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  Encodable::PrintFlutterEncodableMap("resumeVideoRecording", args);
  // method arguments
  int32_t cameraId = 0;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    }
  }

  const auto& camera = g_cameras[static_cast<unsigned long>(cameraId - 1)];
  camera->resumeVideoRecording();

  result(std::nullopt);
}

void CameraPlugin::stopVideoRecording(
    const flutter::EncodableMap& args,
    const std::function<void(ErrorOr<std::string> reply)> result) {
  Encodable::PrintFlutterEncodableMap("stopVideoRecording", args);
  // method arguments
  int32_t cameraId = 0;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    }
  }

  const auto& camera = g_cameras[static_cast<unsigned long>(cameraId - 1)];
  result(ErrorOr(camera->stopVideoRecording()));
}

void CameraPlugin::pausePreview(
    const flutter::EncodableMap& args,
    const std::function<void(ErrorOr<double> reply)> result) {
  // method arguments
  int32_t cameraId = 0;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    }
  }
  (void)cameraId;

  SPDLOG_DEBUG("[camera_plugin] pausePreview: camera_id: {}", cameraId);
  result(ErrorOr<double>(1));
}

void CameraPlugin::resumePreview(
    const flutter::EncodableMap& args,
    const std::function<void(ErrorOr<double> reply)> result) {
  // method arguments
  int32_t cameraId = 0;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    }
  }

  SPDLOG_DEBUG("[camera_plugin] resumePreview: camera_id: {}", cameraId);
  result(ErrorOr<double>(cameraId));
}

void CameraPlugin::lockCaptureOrientation(
    const flutter::EncodableMap& args,
    const std::function<void(ErrorOr<std::string>)> result) {
  // method arguments
  int32_t cameraId = 0;
  std::string orientation;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    } else if (key == "orientation" &&
               std::holds_alternative<std::string>(snd)) {
      orientation = std::get<std::string>(snd);
    }
  }
  (void)cameraId;
  SPDLOG_DEBUG(
      "[camera_plugin] lockCaptureOrientation: camera_id: {}, orientation: {}",
      cameraId, orientation);
  result(ErrorOr(orientation));
}

void CameraPlugin::unlockCaptureOrientation(
    const flutter::EncodableMap& args,
    const std::function<void(ErrorOr<std::string>)> result) {
  // method arguments
  int32_t cameraId = 0;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    }
  }
  (void)cameraId;
  SPDLOG_DEBUG("[camera_plugin] unlockCaptureOrientation: camera_id: {}",
               cameraId);
  const std::string res;
  result(ErrorOr(res));
}

void CameraPlugin::setFlashMode(
    const flutter::EncodableMap& args,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  // method arguments
  int32_t cameraId = 0;
  std::string mode;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    } else if (key == "mode" && std::holds_alternative<std::string>(snd)) {
      mode = std::get<std::string>(snd);
    }
  }
  (void)cameraId;
  (void)mode;
  SPDLOG_DEBUG("[camera_plugin] setFlashMode: camera_id: {}, orientation: {}",
               cameraId, mode);

  result(std::nullopt);
}

void CameraPlugin::setFocusMode(
    const flutter::EncodableMap& args,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  // method arguments
  int32_t cameraId = 0;
  std::string mode;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    } else if (key == "mode" && std::holds_alternative<std::string>(snd)) {
      mode = std::get<std::string>(snd);
    }
  }
  (void)cameraId;
  (void)mode;
  SPDLOG_DEBUG("[camera_plugin] setFocusMode: camera_id: {}, mode: {}",
               cameraId, mode);

  result(std::nullopt);
}

void CameraPlugin::setExposureMode(
    const flutter::EncodableMap& args,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  Encodable::PrintFlutterEncodableMap("setExposureMode", args);

  result(std::nullopt);
}

void CameraPlugin::setExposurePoint(
    const flutter::EncodableMap& args,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  Encodable::PrintFlutterEncodableMap("setExposurePoint", args);

  result(std::nullopt);
}

void CameraPlugin::setFocusPoint(
    const flutter::EncodableMap& args,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  Encodable::PrintFlutterEncodableMap("setFocusPoint", args);
  result(std::nullopt);
}

void CameraPlugin::setExposureOffset(
    const flutter::EncodableMap& args,
    const std::function<void(ErrorOr<double> reply)> result) {
  // method arguments
  int32_t cameraId = 0;
  double offset;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    } else if (key == "offset" && std::holds_alternative<double>(snd)) {
      offset = std::get<double>(snd);
    }
  }
  (void)cameraId;
  SPDLOG_DEBUG("[camera_plugin] setExposureOffset: camera_id: {}, offset: {}",
               cameraId, offset);
  result(ErrorOr(offset));
}

void CameraPlugin::getExposureOffsetStepSize(
    const flutter::EncodableMap& args,
    const std::function<void(ErrorOr<double> reply)> result) {
  // method arguments
  int32_t cameraId = 0;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    }
  }
  (void)cameraId;
  SPDLOG_DEBUG("[camera_plugin] getExposureOffsetStepSize: camera_id: {}",
               cameraId);

  result(ErrorOr<double>(2));
}

void CameraPlugin::getMinExposureOffset(
    const flutter::EncodableMap& args,
    const std::function<void(ErrorOr<double> reply)> result) {
  // method arguments
  int32_t cameraId = 0;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    }
  }
  (void)cameraId;
  SPDLOG_DEBUG("[camera_plugin] getMinExposureOffset: camera_id: {}", cameraId);
  result(ErrorOr<double>(0));
}

void CameraPlugin::getMaxExposureOffset(
    const flutter::EncodableMap& args,
    const std::function<void(ErrorOr<double> reply)> result) {
  // method arguments
  int32_t cameraId = 0;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    }
  }
  (void)cameraId;
  SPDLOG_DEBUG("[camera_plugin] getMaxExposureOffset: camera_id: {}", cameraId);
  result(ErrorOr<double>(64));
}

void CameraPlugin::getMaxZoomLevel(
    const flutter::EncodableMap& args,
    const std::function<void(ErrorOr<double> reply)> result) {
  // method arguments
  int32_t cameraId = 0;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    }
  }
  (void)cameraId;
  SPDLOG_DEBUG("[camera_plugin] getMaxZoomLevel: camera_id: {}", cameraId);
  result(ErrorOr<double>(32));
}

void CameraPlugin::getMinZoomLevel(
    const flutter::EncodableMap& args,
    const std::function<void(ErrorOr<double> reply)> result) {
  // method arguments
  int32_t cameraId = 0;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    }
  }
  (void)cameraId;
  SPDLOG_DEBUG("[camera_plugin] getMinZoomLevel: camera_id: {}", cameraId);

  result(ErrorOr<double>(0));
}

void CameraPlugin::dispose(
    const flutter::EncodableMap& args,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  // method arguments
  int32_t cameraId = 0;

  for (const auto& [fst, snd] : args) {
    if (auto key = std::get<std::string>(fst);
        key == "cameraId" && std::holds_alternative<int32_t>(snd)) {
      cameraId = std::get<int32_t>(snd);
    }
  }
  auto camera = g_cameras[static_cast<unsigned long>(cameraId - 1)];
  camera.reset();
  SPDLOG_DEBUG("[camera_plugin] dispose: {}", cameraId);
  result(std::nullopt);
}
*/
}  // namespace camera_plugin
