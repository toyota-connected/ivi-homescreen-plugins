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

#include <flutter/plugin_registrar_homescreen.h>

#include <memory>
#include <unordered_map>

#include <SDL2/SDL.h>
#include <glib/main_loop.h>
#include <jpeglib.h>
#include <pipewire/core.h>
#include <pipewire/pipewire.h>
#include <pipewire/properties.h>
#include <spa/param/param.h>
#include <spa/param/video/format-utils.h>
#include <iostream>
#include <string>
#include <vector>
#include "CameraManager.h"
#include "camera_plugin.h"
#include "plugins/common/common.h"

extern "C" {
#include <pipewire/pipewire.h>
}

struct CameraInfo {
  uint32_t id;
  std::string name;
};

// Global vector to store camera info
std::vector<CameraInfo> cameras;

// For streaming
static constexpr int WIDTH = 640;
static constexpr int HEIGHT = 480;

// Callback function for detecting cameras
void on_global(void* data,
               uint32_t id,
               uint32_t permissions,
               const char* type,
               uint32_t version,
               const struct spa_dict* props) {
  if (!props)
    return;

  const char* media_class = spa_dict_lookup(props, "media.class");
  const char* name = spa_dict_lookup(props, "node.description");

  if (media_class && std::string(media_class) == "Video/Source") {
    std::cout << "Found camera: " << (name ? name : "Unknown") << " (id: " << id
              << ")" << std::endl;
    cameras.push_back({id, name ? name : "Unknown"});
  }
}

using namespace plugin_common;

namespace camera_plugin {

// TODO static constexpr char kKeyMaxVideoDuration[] = "maxVideoDuration";

// TODO static constexpr char kResolutionPresetValueLow[] = "low";
// TODO static constexpr char kResolutionPresetValueMedium[] = "medium";
// TODO static constexpr char kResolutionPresetValueHigh[] = "high";
// TODO static constexpr char kResolutionPresetValueVeryHigh[] = "veryHigh";
// TODO static constexpr char kResolutionPresetValueUltraHigh[] = "ultraHigh";
// TODO static constexpr char kResolutionPresetValueMax[] = "max";

// static std::unique_ptr<libcamera::CameraManager> g_camera_manager;
//  static std::vector<std::shared_ptr<CameraContext>> g_cameras;
//  static std::unordered_map<unsigned int, std::shared_ptr<CameraSession>>
//      g_camera_sessions;

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
    : registrar_(plugin_registrar), messenger_(messenger) {
  if (!CameraManager::instance().initialize()) {
    std::cerr << "Failed to initialize PipeWire manager!\n";
  }
}

CameraPlugin::~CameraPlugin() {
  CameraManager::instance().shutdown();
}
/*
void CameraPlugin::camera_added(const std::shared_ptr<libcamera::Camera>& cam) {
  spdlog::debug("[camera_plugin] Camera added: {}", cam->id());
}

void CameraPlugin::camera_removed(
    const std::shared_ptr<libcamera::Camera>& cam) {
  spdlog::debug("[camera_plugin] Camera removed: {}", cam->id());
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
*/
ErrorOr<flutter::EncodableList> CameraPlugin::GetAvailableCameras() {
  flutter::EncodableList list;
  auto& mgr = CameraManager::instance();
  auto cameras = mgr.getAvailableCameras();
  for (const auto& [id, name] : cameras) {
    std::cout << "Detected camera: " << name << " (ID: " << id << ")\n";
    list.emplace_back(flutter::EncodableValue(std::move(std::to_string(id))));
  }
  return list;
}

void CameraPlugin::Create(
    const std::string& camera_name,
    const PlatformMediaSettings& settings,
    const std::function<void(ErrorOr<int64_t> reply)> result) {
  spdlog::debug("[camera_plugin] create:");

  spdlog::debug("\tname: {}", camera_name);

  if (CameraName_CameraStream.find(camera_name) ==
      CameraName_CameraStream.end()) {
    // The camera is not created before
    // CameraStream newCamera = CameraStream(registrar_, 640,480 );
    // CameraName_CameraStream.insert({camera_name, newCamera});
    auto new_camera =
        std::make_shared<CameraStream>(registrar_, camera_name, 640, 480);
    CameraName_CameraStream.insert({camera_name, new_camera});
    TextureId_CameraStream.insert({new_camera->texture_id(), new_camera});
    // result(new_camera->get_textureId());
  }
  std::cout << "textureID of " << camera_name
            << " is : " << CameraName_CameraStream[camera_name]->texture_id()
            << std::endl;
  result(CameraName_CameraStream[camera_name]->texture_id());
}
/******************************************************************************
 * decode_mjpeg
 ******************************************************************************/
int decode_mjpeg(const uint8_t* input,
                 size_t input_size,
                 uint8_t* output,
                 int out_width,
                 int out_height) {
  jpeg_decompress_struct cinfo;
  jpeg_error_mgr jerr;

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);

  jpeg_mem_src(&cinfo, input, input_size);
  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
    std::cerr << "[decode_mjpeg] Failed to read JPEG header.\n";
    jpeg_destroy_decompress(&cinfo);
    return -1;
  }

  jpeg_start_decompress(&cinfo);
  if (cinfo.output_width != static_cast<uint32_t>(out_width) ||
      cinfo.output_height != static_cast<uint32_t>(out_height) ||
      cinfo.output_components != 3) {
    std::cerr << "[decode_mjpeg] Unexpected size/components.\n";
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return -1;
  }

  const int row_stride = cinfo.output_width * cinfo.output_components;
  while (cinfo.output_scanline < cinfo.output_height) {
    JSAMPROW row[1];
    row[0] = &output[cinfo.output_scanline * row_stride];
    jpeg_read_scanlines(&cinfo, row, 1);
  }

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  return 0;
}
/******************************************************************************
 * parse_props_param: Dump all properties from a SPA_TYPE_OBJECT_Props param
 *
 * This function attempts to read each property key (like SPA_PROP_brightness)
 * from the param, then prints its value type. Real code might do more detailed
 * checks or convert to a known range.
 ******************************************************************************/
#define IMAGE_WIDTH 640
#define IMAGE_HEIGHT 480
#define IMAGE_CHANNELS 3  // RGB format

void save_image_to_jpeg(const std::string& filename,
                        const unsigned char* image_data,
                        int width,
                        int height,
                        int channels,
                        int quality) {
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;

  // Setup error handling
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);

  // Open file for writing
  FILE* outfile = fopen(filename.c_str(), "wb");
  if (!outfile) {
    std::cerr << "Error: Unable to open file " << filename << " for writing!"
              << std::endl;
    return;
  }

  jpeg_stdio_dest(&cinfo, outfile);

  // Set image properties
  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = channels;
  cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, quality, TRUE);

  // Start compression
  jpeg_start_compress(&cinfo, TRUE);

  // Write scanlines
  JSAMPROW row_pointer;
  while (cinfo.next_scanline < cinfo.image_height) {
    row_pointer = (JSAMPROW)&image_data[cinfo.next_scanline * width * channels];
    jpeg_write_scanlines(&cinfo, &row_pointer, 1);
  }

  // Finish compression
  jpeg_finish_compress(&cinfo);
  fclose(outfile);
  jpeg_destroy_compress(&cinfo);

  std::cout << "Image saved to " << filename << std::endl;
}

void CameraPlugin::Initialize(
    const int64_t camera_id,
    const std::function<void(ErrorOr<PlatformSize> reply)> result) {
  std::cout << "CameraPlugin::Initialize: " << camera_id << std::endl;
  // GLuint textureID= camera_id;
  if (TextureId_CameraStream.find(camera_id) == TextureId_CameraStream.end()) {
    return;  // means, the camera_id is not found.
  }
  auto camera_stream = TextureId_CameraStream[camera_id];

  // auto cameraStream = CameraName_CameraStream
  result(PlatformSize(camera_stream->camera_width(),
                      camera_stream->camera_height()));
  // std::string nodeID=camera_stream->camera_name();

  std::cout << camera_stream->camera_name() << std::endl;
  camera_stream->Start(camera_stream->camera_name());
}
void CameraPlugin::blit_fb(uint8_t const* pixels) const {
  SPDLOG_TRACE("[camera_plugin] Texture::blit_fb");
  texture_registrar_->TextureClearCurrent();
  glBindFramebuffer(GL_FRAMEBUFFER, mPreview.framebuffer);
  glViewport(0, 0, mPreview.width, mPreview.height);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, mPreview.textureId);
  glUniform1i(0, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  // The following call requires a 32-bit aligned source buffer
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, mPreview.width, mPreview.height, 0,
               GL_RGB, GL_UNSIGNED_BYTE, pixels);
  glGenerateMipmap(GL_TEXTURE_2D);

  glBindFramebuffer(GL_FRAMEBUFFER, GL_NONE);
  texture_registrar_->TextureClearCurrent();
  texture_registrar_->MarkTextureFrameAvailable(mPreview.textureId);

  // glBindFramebuffer(GL_FRAMEBUFFER, mPreview.framebuffer);
  // texture_registrar->MarkTextureFrameAvailable(mPreview.textureId);
}

std::optional<FlutterError> CameraPlugin::Dispose(const int64_t camera_id) {
  // auto camera = g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  // camera.reset();
  SPDLOG_DEBUG("[camera_plugin] dispose: {}", camera_id);
  auto camera_stream = TextureId_CameraStream[camera_id];
  camera_stream->Stop();
  return {};
}

void CameraPlugin::TakePicture(
    const int64_t camera_id,
    const std::function<void(ErrorOr<std::string> reply)> result) {
  SPDLOG_DEBUG("[camera_plugin] Take Picture: {}", camera_id);
  auto camera_stream = TextureId_CameraStream[camera_id];

  // std::string str = "Take Picture ";
  result(camera_stream->takePicture());
}

void CameraPlugin::StartVideoRecording(
    const int64_t camera_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  bool enable_stream{};
  /*
    const auto camera =
        g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
    camera->startVideoRecording(enable_stream);
  */
  result({});
}

void CameraPlugin::StopVideoRecording(
    const int64_t camera_id,
    const std::function<void(ErrorOr<std::string> reply)> result) {
  /*
  const auto camera =
      g_camera_sessions[static_cast<unsigned long>(camera_id - 1)];
  result(camera->stopVideoRecording());
  */
}

void CameraPlugin::PausePreview(
    const int64_t camera_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  SPDLOG_DEBUG("[camera_plugin] PausePreview");
  auto camera_stream = TextureId_CameraStream[camera_id];
  camera_stream->PauseStream();
  result({});
}

void CameraPlugin::ResumePreview(
    const int64_t camera_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  SPDLOG_DEBUG("[camera_plugin] ResumePreview");
  auto camera_stream = TextureId_CameraStream[camera_id];
  camera_stream->ResumeStream();
  result({});
}
}  // namespace camera_plugin
