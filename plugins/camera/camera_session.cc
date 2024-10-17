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

#include "camera_session.h"

#include <optional>
#include <utility>

#include <flutter/event_channel.h>
#include <flutter/event_stream_handler_functions.h>
#include <libcamera/property_ids.h>
#include <asio/post.hpp>

#include <plugins/common/common.h>

#include "messages.g.h"
#include "texture_sink.h"
#include "file_sink.h"

namespace camera_plugin {

static constexpr char kPictureCaptureExtension[] = "jpeg";
static constexpr char kVideoCaptureExtension[] = "mp4";

using namespace plugin_common;

static void printType(const std::string& name,
                      const libcamera::ControlType& type,
                      const libcamera::ControlValue& value) {
  if (type == libcamera::ControlTypeNone) {
    SPDLOG_DEBUG("\t[{}] (None)", name);
  } else if (type == libcamera::ControlTypeBool) {
    auto bool_ = value.get<bool>();
    SPDLOG_DEBUG("\t[{}] (Bool) {}", name, bool_);
  } else if (type == libcamera::ControlTypeByte) {
    auto byte_ = value.get<uint8_t>();
    SPDLOG_DEBUG("\t[{}] (Byte) 0x{:02X}", name, byte_);
  } else if (type == libcamera::ControlTypeInteger32) {
    auto int32_ = value.get<int32_t>();
    SPDLOG_DEBUG("\t[{}] (Integer32) {}", name, int32_);
  } else if (type == libcamera::ControlTypeInteger64) {
    const auto int64_ = reinterpret_cast<const int64_t*>(value.data().data());
    SPDLOG_DEBUG("\t[{}] (Integer64) {}", name, *int64_);
  } else if (type == libcamera::ControlTypeFloat) {
    auto float_ = value.get<float>();
    SPDLOG_DEBUG("\t[{}] (Float) {}", name, float_);
  } else if (type == libcamera::ControlTypeString) {
    auto string_ = value.get<std::string>();
    SPDLOG_DEBUG("\t[{}] (String) {}", name, string_);
  } else if (type == libcamera::ControlTypeRectangle) {
    const auto rectangle =
        reinterpret_cast<const libcamera::Rectangle*>(value.data().data());
    SPDLOG_DEBUG("\t[{}] (Rectangle) [{},{}] {}x{}", name, rectangle->x,
                 rectangle->y, rectangle->width, rectangle->height);
  } else if (type == libcamera::ControlTypeSize) {
    auto size = value.get<libcamera::Size>();
    SPDLOG_DEBUG("\t[{}] (Size) {}x{}", name, size.width, size.height);
  }
}

CameraSession::CameraSession(flutter::PluginRegistrar* plugin_registrar,
                             std::string cameraName,
                             const PlatformMediaSettings& settings,
                             std::shared_ptr<libcamera::Camera> camera,
                             asio::io_context::strand* strand)
    : camera_name_(std::move(cameraName)),
      strand_(strand),
      camera_(std::move(camera)),
      plugin_registrar_(plugin_registrar),
      platform_media_settings_(
          PlatformMediaSettings(settings.resolution_preset(),
                                settings.frames_per_second(),
                                settings.video_bitrate(),
                                settings.audio_bitrate(),
                                settings.enable_audio())),
      last_(0) {
  spdlog::debug("[camera_plugin]");
  spdlog::debug("\tcameraName: [{}]", camera_name_);

  // Camera ID
  libcamera_id_ = camera_->id();

  // Generate Texture ID
  const auto texture_registrar = plugin_registrar_->texture_registrar();
  texture_registrar->TextureMakeCurrent();
  glGenTextures(1, &texture_id_);
  texture_registrar->TextureClearCurrent();

  print_platform_media_settins(settings);
  mCameraState = CAM_STATE_AVAILABLE;
  int res;
  if ((res = camera_->acquire()) == 0) {
    if (mCameraState == CAM_STATE_AVAILABLE) {
      mCameraState = CAM_STATE_ACQUIRED;
    }
  } else {
    spdlog::error("[camera_plugin] Failed to acquire camera: {}", res);
  }

  spdlog::debug("[camera_plugin] Controls:");
  for (const auto& [id, info] : camera_->controls()) {
    // SPDLOG_DEBUG("\t[{}] {}", id->name(), info.toString());
    printType(id->name() + ": min", info.min().type(), info.min());
    printType(id->name() + ": max", info.max().type(), info.max());
    printType(id->name() + ": default", info.def().type(), info.def());
  }

  spdlog::debug("[camera_plugin] Properties:");
  for (const auto& [key, value] : camera_->properties()) {
    const auto id = libcamera::properties::properties.at(key);
    // SPDLOG_DEBUG("\t[{}] {}", id->name(), value.toString());
    printType(id->name(), value.type(), value);
  }
}

CameraSession::~CameraSession() {
  SPDLOG_DEBUG("[camera_plugin] CameraSession::~CameraSession()");
  camera_->release();
  mCameraState = CAM_STATE_AVAILABLE;

  const auto texture_registrar = plugin_registrar_->texture_registrar();
  texture_registrar->TextureMakeCurrent();
  if (texture_id_) {
    glDeleteTextures(1, &texture_id_);
  }
  texture_registrar->TextureClearCurrent();
}

void CameraSession::setCamera(std::shared_ptr<libcamera::Camera> camera) {
  camera_ = std::move(camera);
}

PlatformSize CameraSession::getPlatformSize() const {
  return PlatformSize(width_, height_);
}

std::string CameraSession::Initialize(int64_t camera_id,
                                      const std::string& image_format_group) {
  std::string channel_name =
      std::string("plugins.flutter.io/camera_linux/camera") +
      std::to_string(camera_id);

  camera_channel_ = std::make_unique<flutter::MethodChannel<>>(
      plugin_registrar_->messenger(), channel_name,
      &flutter::StandardMethodCodec::GetInstance());

  mImageFormatGroup.assign(image_format_group);

  spdlog::debug(
      "[camera_plugin] Initialize: cameraId: {}, imageFormatGroup: [{}]",
      camera_id, mImageFormatGroup);

  //std::vector roles = {libcamera::StreamRole::Viewfinder, libcamera::StreamRole::StillCapture};
  //std::vector roles = {libcamera::StreamRole::Raw};
  std::vector roles = {libcamera::StreamRole::Viewfinder};
  std::unique_ptr<libcamera::CameraConfiguration> config =
      camera_->generateConfiguration(roles);

  //libcamera::Size size(2304, 1563);

  //config->at(0).size.width = 1280;
  //config->at(0).size.height = 720;
  //config
  //config->at(0).orientation = libcamera::Orientation::Rotate180;
  libcamera::StreamConfiguration &cfg = config->at(0);
  spdlog::debug("width: {}, height: {}", cfg.size.width, cfg.size.height);

/*
  roles = {libcamera::StreamRole::VideoRecording};
  config = camera_->generateConfiguration(roles);
  cfg = config->at(0);
  spdlog::debug("VideoRecording: width: {}, height: {}", cfg.size.width, cfg.size.height);


  roles = {libcamera::StreamRole::Raw};
  config = camera_->generateConfiguration(roles);
  cfg = config->at(0);
  spdlog::debug("Raw: width: {}, height: {}", cfg.size.width, cfg.size.height);

  roles = {libcamera::StreamRole::StillCapture};
  config = camera_->generateConfiguration(roles);
  cfg = config->at(0);
  spdlog::debug("StillCapture: width: {}, height: {}", cfg.size.width, cfg.size.height);
  //spdlog::debug("bitDepth: {}, binningX: {}, binningY: {}", config->sensorConfig->bitDepth, config->sensorConfig->binning.binX, config->sensorConfig->binning.binY);
  //spdlog::debug("bitDepth: {}, size: {}", config->sensorConfig->bitDepth, config->size());
*/
  //config->orientation();
  //config->sensorConfig->bitDepth;
  //config->sensorConfig->binning.binX;
  //config->sensorConfig->binning.binY;


  if (!config || config->size() != roles.size()) {
    spdlog::error("Failed to get default stream configuration");
    return "";
  }

  //  width_ = 0;
  //  height_ = 0;
  for (const auto& [key, value] : camera_->properties()) {
    if (libcamera::properties::properties.at(key)->name() == "PixelArraySize" &&
        value.type() == libcamera::ControlTypeSize) {
      auto const size = value.get<libcamera::Size>();
      width_ = static_cast<GLsizei>(size.width);
      height_ = static_cast<GLsizei>(size.height);
      break;
    }
  }

  const std::string exposureMode("auto");
  const std::string focusMode("locked");
  bool exposurePointSupported{};
  bool focusPointSupported{};

  mConfig = std::move(config);

  start();

  camera_channel_->InvokeMethod(
      "initialized",
      std::make_unique<flutter::EncodableValue>(
          flutter::EncodableValue(flutter::EncodableMap(
              {{flutter::EncodableValue("cameraId"),
                flutter::EncodableValue(camera_id)},
               {flutter::EncodableValue("previewWidth"),
                flutter::EncodableValue(static_cast<double>(width_))},
               {flutter::EncodableValue("previewHeight"),
                flutter::EncodableValue(static_cast<double>(height_))},
               {flutter::EncodableValue("exposureMode"),
                flutter::EncodableValue(exposureMode.c_str())},
               {flutter::EncodableValue("exposurePointSupported"),
                flutter::EncodableValue(exposurePointSupported)},
               {flutter::EncodableValue("focusMode"),
                flutter::EncodableValue(focusMode.c_str())},
               {flutter::EncodableValue("focusPointSupported"),
                flutter::EncodableValue(focusPointSupported)}}))));

  return channel_name;
}

std::optional<std::string> CameraSession::GetFilePathForPicture() {
  std::ostringstream oss;
  oss << "xdg-user-dir PICTURES";
  std::string picture_path;
  if (!Command::Execute(oss.str().c_str(), picture_path)) {
    return std::nullopt;
  }
  std::filesystem::path path(StringTools::trim(picture_path, "\n"));
  path /= "PhotoCapture_" + TimeTools::GetCurrentTimeString() + "." +
          kPictureCaptureExtension;
  return path;
}

std::optional<std::string> CameraSession::GetFilePathForVideo() {
  std::ostringstream oss;
  oss << "xdg-user-dir VIDEOS";
  std::string video_path;
  if (!Command::Execute(oss.str().c_str(), video_path)) {
    return std::nullopt;
  }
  std::filesystem::path path(StringTools::trim(video_path, "\n"));
  path /= "VideoCapture_" + TimeTools::GetCurrentTimeString() + "." +
          kVideoCaptureExtension;
  return path;
}

std::string CameraSession::takePicture() {
  auto filename = GetFilePathForPicture();
  //int ret;
  //std::unique_ptr<FrameSink> mSink2=std::make_unique<FileSink>((camera_.get(), streamNames_);

  /*
  if (filename.has_value()) {
    if(mSink) {
      mSink->take_picture(filename.value());
    }
    return filename.value();
  }
*/
  return {};
}

double CameraSession::pausePreview() {
  SPDLOG_DEBUG("[camera_plugin] pausePreview");
  //camera_->requestCompleted.disconnect(this, &CameraSession::request_complete);
  if(camera_) {
    camera_->stop();
    camera_->requestCompleted.disconnect(this, &CameraSession::request_complete);
    mRequests.clear();
  }
  return 1;
}

double CameraSession::resumePreview() {
  SPDLOG_DEBUG("[camera_plugin] resumePreview");

  mQueueCount = 0;

  int ret = camera_->configure(mConfig.get());
  if (ret < 0) {
    spdlog::error("[camera_plugin] Failed to configure camera");
    return ret;
  }

  mStreamNames.clear();
  for (unsigned int index = 0; index < mConfig->size(); ++index) {
    libcamera::StreamConfiguration& cfg = mConfig->at(index);
    mStreamNames[cfg.stream()] =
        "cam" + libcamera_id_ + "-stream" + std::to_string(index);
  }

  camera_->requestCompleted.connect(this, &CameraSession::request_complete);

  mSink = std::make_unique<TextureSink>(plugin_registrar_->texture_registrar());
  if (mSink) {
    ret = mSink->configure(*mConfig, texture_id_);
    if (ret < 0) {
      spdlog::error("[camera_plugin] Failed to configure frame sink");
      return ret;
    }

    mSink->request_processed.connect(this, &CameraSession::sink_release);
  }

  mAllocator = std::make_unique<libcamera::FrameBufferAllocator>(camera_);

  // Identify the stream with the least number of buffers
  unsigned int nbuffers = UINT_MAX;
  for (libcamera::StreamConfiguration& cfg : *mConfig) {
    ret = mAllocator->allocate(cfg.stream());
    if (ret < 0) {
      spdlog::error("Can't allocate buffers");
      return -ENOMEM;
    }

    unsigned int allocated = mAllocator->buffers(cfg.stream()).size();
    nbuffers = std::min(nbuffers, allocated);
  }

  /// TODO - make cam tool smarter to support still capture by for example
  /// pushing a button. For now run all streams all the time.

  for (unsigned int i = 0; i < nbuffers; i++) {
    std::unique_ptr<libcamera::Request> request = camera_->createRequest();
    if (!request) {
      spdlog::error("Can't create request");
      return -ENOMEM;
    }

    for (libcamera::StreamConfiguration& cfg : *mConfig) {
      libcamera::Stream* stream = cfg.stream();
      const auto& buffers = mAllocator->buffers(stream);
      const auto& buffer = buffers[i];

      ret = request->addBuffer(stream, buffer.get());
      if (ret < 0) {
        spdlog::error("Can't set buffer for request");
        return ret;
      }

      if (mSink) {
        mSink->map_buffer(buffer.get());
      }
    }

    mRequests.push_back(std::move(request));
  }

  if (mSink) {
    ret = mSink->start();
    if (ret) {
      spdlog::error("[camera_plugin] Failed to start frame sink");
      return ret;
    }
  }

  ret = camera_->start();
  if (ret) {
    spdlog::error("Failed to start capture");
    if (mSink)
      mSink->stop();
    return ret;
  }

  for (std::unique_ptr<libcamera::Request>& request : mRequests) {
    ret = queue_request(request.get());
    spdlog::debug("request: {}",request.get()->toString());
    if (ret < 0) {
      spdlog::error("Can't queue request");
      camera_->stop();
      if (mSink)
        mSink->stop();
      return ret;
    }
  }

  return 1;
}

std::optional<FlutterError> CameraSession::setFlashMode(
    const std::string& mode) {
  SPDLOG_DEBUG("[camera_plugin] setFlashMode: mode: {}", mode);
  return std::nullopt;
}

std::optional<FlutterError> CameraSession::setFocusMode(
    const std::string& mode) {
  SPDLOG_DEBUG("[camera_plugin] setFocusMode: mode: {}", mode);
  return std::nullopt;
}

void CameraSession::startVideoRecording(bool /* enableStream */) {
  auto filename = GetFilePathForVideo();
  if (filename.has_value()) {
    mVideoFilename = filename.value();
    SPDLOG_DEBUG("[camera_plugin] startVideoRecording: file: {}",
                 mVideoFilename);
    // TODO start recording
  } else {
    mVideoFilename.clear();
  }
}

void CameraSession::pauseVideoRecording() {
  SPDLOG_DEBUG("[camera_plugin] pauseVideoRecording");
  // TODO
}

void CameraSession::resumeVideoRecording() {
  SPDLOG_DEBUG("[camera_plugin] resumeVideoRecording");
  // TODO
}

std::string CameraSession::stopVideoRecording() {
  SPDLOG_DEBUG("[camera_plugin] stopVideoRecording: filename: [{}]",
               mVideoFilename);
  // TODO
  return mVideoFilename;
}

double CameraSession::getMinExposureOffset() const {
  double result = 0;
#if 0
  for (const auto& [id, info] : mCamera->controls()) {
    if ("ExposureTime" == id->name() &&
        id->type() == libcamera::ControlTypeInteger32) {
      result = info.min().get<int32_t>();
      break;
    }
  }
#endif
  SPDLOG_DEBUG("[camera_plugin] getMinExposureOffset: offset: {}", result);
  return result;
}

double CameraSession::getMaxExposureOffset() const {
  double result = 0;
  for (const auto& [id, info] : camera_->controls()) {
    if ("ExposureTime" == id->name() &&
        id->type() == libcamera::ControlTypeInteger32) {
      result = info.max().get<int32_t>();
      break;
    }
  }
  SPDLOG_DEBUG("[camera_plugin] getMaxExposureOffset: offset: {}", result);
  return result;
}

double CameraSession::getExposureOffsetStepSize() const {
  double step = 2;
  SPDLOG_DEBUG("[camera_plugin] getExposureOffsetStepSize: step: {}", step);
  return (step);
}

double CameraSession::setExposureOffset(double offset) {
  SPDLOG_DEBUG("[camera_plugin] setExposureOffset: offset: {}", offset);
  return offset;
}

bool CameraSession::getAutoExposureEnable() const {
  bool result = false;
  for (const auto& [id, info] : camera_->controls()) {
    if ("AeEnable" == id->name() && id->type() == libcamera::ControlTypeBool) {
      result = info.def().get<bool>();
      break;
    }
  }
  return result;
}

double CameraSession::getMinZoomLevel() const {
  double level = 0;
  SPDLOG_DEBUG("[camera_plugin] getMinZoomLevel: level: {}", level);
  return level;
}

double CameraSession::getMaxZoomLevel() const {
  double level = 0;
  SPDLOG_DEBUG("[camera_plugin] getMaxZoomLevel: level {}", level);
  return level;
}

int CameraSession::start() {
  SPDLOG_DEBUG("[camera_plugin] CameraSession: start");

  mQueueCount = 0;

  int ret = camera_->configure(mConfig.get());
  if (ret < 0) {
    spdlog::error("[camera_plugin] Failed to configure camera");
    return ret;
  }

  mStreamNames.clear();
  for (unsigned int index = 0; index < mConfig->size(); ++index) {
    libcamera::StreamConfiguration& cfg = mConfig->at(index);
    mStreamNames[cfg.stream()] =
        "cam" + libcamera_id_ + "-stream" + std::to_string(index);
  }

  camera_->requestCompleted.connect(this, &CameraSession::request_complete);

  mSink = std::make_unique<TextureSink>(plugin_registrar_->texture_registrar());
  if (mSink) {
    ret = mSink->configure(*mConfig, texture_id_);
    if (ret < 0) {
      spdlog::error("[camera_plugin] Failed to configure frame sink");
      return ret;
    }

    mSink->request_processed.connect(this, &CameraSession::sink_release);
  }

  mAllocator = std::make_unique<libcamera::FrameBufferAllocator>(camera_);

  return start_capture();
}

int CameraSession::start_capture() {
  int ret;

  // Identify the stream with the least number of buffers
  unsigned int nbuffers = UINT_MAX;
  for (libcamera::StreamConfiguration& cfg : *mConfig) {
    ret = mAllocator->allocate(cfg.stream());
    if (ret < 0) {
      spdlog::error("Can't allocate buffers");
      return -ENOMEM;
    }

    unsigned int allocated = mAllocator->buffers(cfg.stream()).size();
    nbuffers = std::min(nbuffers, allocated);
  }

  /// TODO - make cam tool smarter to support still capture by for example
  /// pushing a button. For now run all streams all the time.

  for (unsigned int i = 0; i < nbuffers; i++) {
    std::unique_ptr<libcamera::Request> request = camera_->createRequest();
    if (!request) {
      spdlog::error("Can't create request");
      return -ENOMEM;
    }

    for (libcamera::StreamConfiguration& cfg : *mConfig) {
      libcamera::Stream* stream = cfg.stream();
      const auto& buffers = mAllocator->buffers(stream);
      const auto& buffer = buffers[i];

      ret = request->addBuffer(stream, buffer.get());
      if (ret < 0) {
        spdlog::error("Can't set buffer for request");
        return ret;
      }

      if (mSink) {
        mSink->map_buffer(buffer.get());
      }
    }

    mRequests.push_back(std::move(request));
  }

  if (mSink) {
    ret = mSink->start();
    if (ret) {
      spdlog::error("[camera_plugin] Failed to start frame sink");
      return ret;
    }
  }

  ret = camera_->start();
  if (ret) {
    spdlog::error("Failed to start capture");
    if (mSink)
      mSink->stop();
    return ret;
  }

  for (std::unique_ptr<libcamera::Request>& request : mRequests) {
    ret = queue_request(request.get());
    spdlog::debug("request: {}",request.get()->toString());
    if (ret < 0) {
      spdlog::error("Can't queue request");
      camera_->stop();
      if (mSink)
        mSink->stop();
      return ret;
    }
  }

  return 0;
}

int CameraSession::queue_request(libcamera::Request* request) {
  mQueueCount++;

  return camera_->queueRequest(request);
}

void CameraSession::request_complete(libcamera::Request* request) {
  if (request->status() == libcamera::Request::RequestCancelled)
    return;

  // Avoid blocking the camera manager thread
  post(*strand_, [this, request] { process_request(request); });
}

void CameraSession::process_request(libcamera::Request* request) {
  const libcamera::Request::BufferMap& buffers = request->buffers();

  // Compute the frame rate. The timestamp is arbitrarily retrieved from the
  // first buffer, as all buffers should have matching timestamps.
  const auto ts = buffers.begin()->second->metadata().timestamp;
  double fps = ts - last_;
  fps = last_ != 0 && fps ? 1000000000.0 / fps : 0.0;
  last_ = ts;

  bool requeue = true;

  std::stringstream info;
  info << "(" << std::fixed << std::setprecision(2) << fps << " fps)";

  for (const auto& [stream, buffer] : buffers) {
    const libcamera::FrameMetadata& metadata = buffer->metadata();

    info << " " << mStreamNames[stream] << " seq: " << std::setw(6)
         << std::setfill('0') << metadata.sequence << " bytes used: ";

    unsigned int nplane = 0;
    for (const libcamera::FrameMetadata::Plane& plane : metadata.planes()) {
      info << plane.bytesused;
      if (++nplane < metadata.planes().size())
        info << "/";
    }
  }

  if (mSink) {
    if (!mSink->process_request(request))
      requeue = false;
  }

  spdlog::info("{}", info.str());

  // If the frame sink holds on the request, we'll requeue it later in the
  // complete handler.
  if (!requeue) {
    return;
  }

  request->reuse(libcamera::Request::ReuseBuffers);
  queue_request(request);
}

void CameraSession::sink_release(libcamera::Request* request) {
  request->reuse(libcamera::Request::ReuseBuffers);
  queue_request(request);
}

void CameraSession::print_platform_media_settins(
    const PlatformMediaSettings& settings) {
  spdlog::info("resolution_preset: {}",
               static_cast<int>(settings.resolution_preset()));
  if (settings.frames_per_second()) {
    spdlog::info("frames_per_second: {}", *settings.frames_per_second());
  }
  if (settings.audio_bitrate()) {
    spdlog::info("audio_bitrate: {}", *settings.audio_bitrate());
  }
  if (settings.video_bitrate()) {
    spdlog::info("video_bitrate: {}", *settings.video_bitrate());
  }
  spdlog::info("enable_audio: {}", settings.enable_audio());
}

}  // namespace camera_plugin