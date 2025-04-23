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
#include <flutter/plugin_registrar_homescreen.h>
#include <jpeglib.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "CameraManager.h"
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

// Callback function for detecting cameras
void on_global(void* /*data*/,
               uint32_t id,
               uint32_t /*permissions*/,
               const char* /*type*/,
               uint32_t /*version*/,
               const struct spa_dict* props) {
  if (!props)
    return;

  const char* media_class = spa_dict_lookup(props, "media.class");
  const char* name = spa_dict_lookup(props, "node.description");

  if (media_class && std::string(media_class) == "Video/Source") {
    spdlog::debug("found camera: {} (id: {})", name, id);
    cameras.push_back({id, name ? name : "Unknown"});
  }
}

using namespace plugin_common;

namespace camera_plugin {

void CameraPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarDesktop* registrar) {
  auto plugin =
      std::make_unique<CameraPlugin>(registrar, registrar->messenger());
  CameraPlugin::SetUp(registrar->messenger(), plugin.get());
  registrar->AddPlugin(std::move(plugin));
}

CameraPlugin::CameraPlugin(flutter::PluginRegistrarDesktop* plugin_registrar,
                           flutter::BinaryMessenger* /*messenger*/)
    : registrar_(plugin_registrar) {
  if (!CameraManager::instance().initialize()) {
    spdlog::error("failed to initialize PipeWire manager!");
  }
}

CameraPlugin::~CameraPlugin() {
  CameraManager::instance().shutdown();
}

ErrorOr<flutter::EncodableList> CameraPlugin::GetAvailableCameras() {
  flutter::EncodableList list;
  auto& mgr = CameraManager::instance();
  auto cameras = mgr.getAvailableCameras();
  for (const auto& [id, name] : cameras) {
    spdlog::debug("[camera_plugin] detected camera:  {} (camera_id: {})", name,
                  id);
    list.emplace_back(std::to_string(id));
  }
  return ErrorOr<flutter::EncodableList>(std::move(list));
}

void CameraPlugin::Create(
    const std::string& camera_id,
    const PlatformMediaSettings& /*settings*/,
    const std::function<void(ErrorOr<int64_t> reply)> result) {
  spdlog::debug("[camera_plugin] create camera_id: {}", camera_id);
  if (CameraId_CameraStream.find(camera_id) == CameraId_CameraStream.end()) {
    auto new_camera =
        std::make_shared<CameraStream>(registrar_, camera_id, 640, 480);
    CameraId_CameraStream.insert({camera_id, new_camera});
    TextureId_CameraStream.insert({new_camera->texture_id(), new_camera});
  }
  int64_t texture_id = CameraId_CameraStream[camera_id]->texture_id();
  spdlog::debug("[camera_plugin] camera_id {}'s texture_id: {}", camera_id,
                texture_id);
  result(ErrorOr<int64_t>(texture_id));
}
/******************************************************************************
 * decode_mjpeg
 ******************************************************************************/
int decode_mjpeg(const uint8_t* input,
                 size_t input_size,
                 uint8_t* output,
                 int out_width,
                 int out_height) {
  jpeg_decompress_struct cinfo{};
  jpeg_error_mgr jerr{};

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);

  jpeg_mem_src(&cinfo, input, input_size);
  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
    spdlog::error("[decode_mjpeg] failed to read JPEG header.");
    jpeg_destroy_decompress(&cinfo);
    return -1;
  }

  jpeg_start_decompress(&cinfo);
  if (cinfo.output_width != static_cast<uint32_t>(out_width) ||
      cinfo.output_height != static_cast<uint32_t>(out_height) ||
      cinfo.output_components != 3) {
    spdlog::error("[decode_mjpeg] unexpected size/components.");
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return -1;
  }

  const unsigned int row_stride = cinfo.output_width * cinfo.output_components;
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

void save_image_to_jpeg(const std::string& filename,
                        const unsigned char* image_data,
                        int width,
                        int height,
                        int channels,
                        int quality) {
  struct jpeg_compress_struct cinfo {};
  struct jpeg_error_mgr jerr {};

  // Setup error handling
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);

  // Open file for writing
  FILE* outfile = fopen(filename.c_str(), "wb");
  if (!outfile) {
    spdlog::error("error: unable to open file {} for writing!", filename);
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
  spdlog::debug("image saved to {}", filename);
}

void CameraPlugin::Initialize(
    const int64_t texture_id,
    const std::function<void(ErrorOr<PlatformSize> reply)> result) {
  if (TextureId_CameraStream.find(texture_id) == TextureId_CameraStream.end()) {
    return;  // means, the texture_id is not found.
  }
  const auto camera_stream = TextureId_CameraStream[texture_id];

  result(ErrorOr<PlatformSize>(PlatformSize(camera_stream->camera_width(),
                                            camera_stream->camera_height())));
  spdlog::debug("[camera_plugin] start the stream for camera_id: {}",
                camera_stream->camera_id());
  camera_stream->Start(camera_stream->camera_id());
}
void CameraPlugin::blit_fb(uint8_t const* pixels) const {
  spdlog::debug("[camera_plugin] blit_fb");
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
}

std::optional<FlutterError> CameraPlugin::Dispose(const int64_t texture_id) {
  spdlog::debug("[camera_plugin] dispose texture_id: {}", texture_id);
  const auto camera_stream = TextureId_CameraStream[texture_id];
  camera_stream->Stop();
  return {};
}

void CameraPlugin::TakePicture(
    const int64_t texture_id,
    const std::function<void(ErrorOr<std::string> reply)> result) {
  spdlog::debug("[camera_plugin] take picture for texture_id: {}", texture_id);
  const auto camera_stream = TextureId_CameraStream[texture_id];
  result(ErrorOr<std::string>(camera_stream->takePicture()));
}

void CameraPlugin::StartVideoRecording(
    const int64_t /*camera_id*/,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  result({});
}

void CameraPlugin::StopVideoRecording(
    const int64_t /*camera_id*/,
    const std::function<void(ErrorOr<std::string> reply)> /*result*/) {}

void CameraPlugin::PausePreview(
    const int64_t texture_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  spdlog::debug("[camera_plugin] pause preview texture_id: {}", texture_id);
  const auto camera_stream = TextureId_CameraStream[texture_id];
  camera_stream->PauseStream();
  result({});
}

void CameraPlugin::ResumePreview(
    const int64_t texture_id,
    const std::function<void(std::optional<FlutterError> reply)> result) {
  spdlog::debug("[camera_plugin] resume preview");
  const auto camera_stream = TextureId_CameraStream[texture_id];
  camera_stream->ResumeStream();
  result({});
}
}  // namespace camera_plugin
