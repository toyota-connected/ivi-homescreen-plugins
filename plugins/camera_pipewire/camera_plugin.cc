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

#include "camera_plugin.h"

#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <glib-2.0/glib.h>
};
#include <jpeglib.h>

#include <flutter/event_stream_handler_functions.h>
#include <flutter/plugin_registrar_homescreen.h>
#include <flutter/standard_method_codec.h>

#include "pipewire_graph.h"

#include "tools/encodable.h"
#include "tools/logging.h"

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
  SetUp(registrar->messenger(), plugin.get());
  registrar->AddPlugin(std::move(plugin));
}

CameraPlugin::CameraPlugin(flutter::PluginRegistrarDesktop* plugin_registrar,
                           flutter::BinaryMessenger* /*messenger*/)
    : mPreview(), registrar_(plugin_registrar) {
  if (!pipewire_graph::instance().initialize()) {
    spdlog::error("failed to initialize PipeWire manager!");
  }

  auto messenger = registrar_->messenger();
  image_channel_ =
      std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
          messenger, "camera_linux/image_stream",
          &flutter::StandardMethodCodec::GetInstance());

  auto handler = std::make_unique<
      flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
      /* on_listen */
      [this](
          const flutter::EncodableValue* /*args*/,
          std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&& sink)
          -> std::unique_ptr<
              flutter::StreamHandlerError<flutter::EncodableValue>> {
        spdlog::info("[camera_plugin] image_stream on_listen");
        image_sink_ = std::move(sink);
        StartImageStream();  // start PipeWire or a test generator
        return nullptr;
      },
      /* on_cancel */
      [this](const flutter::EncodableValue* /*args*/)
          -> std::unique_ptr<
              flutter::StreamHandlerError<flutter::EncodableValue>> {
        spdlog::info("[camera_plugin] image_stream on_cancel");
        StopImageStream();
        image_sink_.reset();
        return nullptr;
      });

  image_channel_->SetStreamHandler(std::move(handler));
}

CameraPlugin::~CameraPlugin() {
  pipewire_graph::instance().shutdown();
}

ErrorOr<flutter::EncodableList> CameraPlugin::GetAvailableCameras() {
  flutter::EncodableList list;
  const pipewire_graph& mgr = pipewire_graph::instance();

  const auto cameras = mgr.getCameraNodes();
  for (auto camera : cameras) {
    spdlog::debug("[camera_plugin] detected camera:  (camera_id: {})",
                  camera.id);
    list.emplace_back(std::in_place_type<std::string>,
                      std::to_string(camera.id));
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
        std::make_shared<camera_stream>(registrar_, camera_id, 640, 480);

    new_camera->on_image_frame =
        [this](const uint8_t* y, const int ys, const uint8_t* u_or_uv,
               const int us, const uint8_t* v, const int vs, const int w,
               const int h, const char* raw) {
          if (!image_stream_active_ || !image_sink_)
            return;

          struct Payload {
            CameraPlugin* self;
            std::vector<uint8_t> y, u, v;
            int ys, us, vs, w, h;
            std::string raw;
          };
          auto up = std::make_unique<Payload>(Payload{
              this,
              std::vector<uint8_t>(y, y + ys * h),
              std::vector<uint8_t>(u_or_uv, u_or_uv + us * ((h + 1) / 2)),
              (v ? std::vector<uint8_t>(v, v + vs * ((h + 1) / 2))
                 : std::vector<uint8_t>()),
              ys,
              us,
              vs,
              w,
              h,
              raw ? std::string(raw) : std::string("I420"),
          });

          // hand off to GLib; after release(), up no longer owns it
          Payload* p = up.release();

          g_main_context_invoke_full(
              nullptr, G_PRIORITY_DEFAULT,
              +[](gpointer data) -> gboolean {  // '+' forces C-function pointer
                auto* P = static_cast<Payload*>(data);
                if (P->self->image_sink_ && P->raw == "I420") {
                  P->self->SendI420Frame(P->y.data(), P->ys, P->u.data(), P->us,
                                         P->v.data(), P->vs, P->w, P->h);
                }
                return G_SOURCE_REMOVE;
              },
              p,
              +[](gpointer data) {  // exact GDestroyNotify
                delete static_cast<Payload*>(data);
              });
        };

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
 * parse_props_param: Dump all properties from an SPA_TYPE_OBJECT_Props param
 *
 * This function attempts to read each property key (like SPA_PROP_brightness)
 * from the param, then prints its value type. Real code might do more detailed
 * checks or convert to a known range.
 ******************************************************************************/

void save_image_to_jpeg(const std::string& filename,
                        const unsigned char* image_data,
                        const int width,
                        const int height,
                        const int channels,
                        const int quality) {
  jpeg_compress_struct cinfo{};
  jpeg_error_mgr jerr{};

  // Setup error handling
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);

  // Open a file for writing
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

  // Write scan lines
  JSAMPROW row_pointer;
  while (cinfo.next_scanline < cinfo.image_height) {
    row_pointer = const_cast<JSAMPROW>(
        &image_data[cinfo.next_scanline * width * channels]);
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

  result(ErrorOr(PlatformSize(camera_stream->camera_width(),
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
  result(ErrorOr(camera_stream->takePicture()));
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

void CameraPlugin::StartImageStream() {
  image_stream_active_ = true;
}

void CameraPlugin::StopImageStream() {
  image_stream_active_ = false;
}

void CameraPlugin::SendI420Frame(const uint8_t* y,
                                 int y_stride,
                                 const uint8_t* u,
                                 int u_stride,
                                 const uint8_t* v,
                                 int v_stride,
                                 int width,
                                 int height) const {
  if (!image_sink_) {
    return;
  }

  using flutter::EncodableList;
  using flutter::EncodableMap;
  using flutter::EncodableValue;

  // size math as size_t
  const auto y_size =
      static_cast<std::size_t>(y_stride) * static_cast<std::size_t>(height);
  const auto u_size =
      static_cast<std::size_t>(u_stride) * static_cast<std::size_t>(height / 2);
  const auto v_size =
      static_cast<std::size_t>(v_stride) * static_cast<std::size_t>(height / 2);

  auto yv = std::vector<std::uint8_t>(y, y + y_size);
  auto uvv = std::vector<std::uint8_t>(u, u + u_size);
  auto vv = std::vector<std::uint8_t>(v, v + v_size);

  // EncodableValue number arm often expects 32- or 64-bit; use int32_t here.
  const auto w32 = static_cast<std::int32_t>(width);
  const auto h32 = static_cast<std::int32_t>(height);
  const auto ys32 = static_cast<std::int32_t>(y_stride);
  const auto us32 = static_cast<std::int32_t>(u_stride);
  const auto vs32 = static_cast<std::int32_t>(v_stride);
  constexpr std::int32_t kBytesPerPixel = 1;

  EncodableList planes;

  // Plane 0 (Y)
  {
    EncodableMap m;
    m.emplace(EncodableValue(std::in_place_type<std::string>, "bytes"),
              EncodableValue(std::in_place_type<std::vector<std::uint8_t>>,
                             std::move(yv)));
    m.emplace(EncodableValue(std::in_place_type<std::string>, "bytesPerRow"),
              EncodableValue(std::in_place_type<std::int32_t>, ys32));
    m.emplace(EncodableValue(std::in_place_type<std::string>, "bytesPerPixel"),
              EncodableValue(std::in_place_type<std::int32_t>, kBytesPerPixel));
    planes.emplace_back(std::in_place_type<EncodableMap>, std::move(m));
  }

  // Plane 1 (U)
  {
    EncodableMap m;
    m.emplace(EncodableValue(std::in_place_type<std::string>, "bytes"),
              EncodableValue(std::in_place_type<std::vector<std::uint8_t>>,
                             std::move(uvv)));
    m.emplace(EncodableValue(std::in_place_type<std::string>, "bytesPerRow"),
              EncodableValue(std::in_place_type<std::int32_t>, us32));
    m.emplace(EncodableValue(std::in_place_type<std::string>, "bytesPerPixel"),
              EncodableValue(std::in_place_type<std::int32_t>, kBytesPerPixel));
    planes.emplace_back(std::in_place_type<EncodableMap>, std::move(m));
  }

  // Plane 2 (V)
  {
    EncodableMap m;
    m.emplace(EncodableValue(std::in_place_type<std::string>, "bytes"),
              EncodableValue(std::in_place_type<std::vector<std::uint8_t>>,
                             std::move(vv)));
    m.emplace(EncodableValue(std::in_place_type<std::string>, "bytesPerRow"),
              EncodableValue(std::in_place_type<std::int32_t>, vs32));
    m.emplace(EncodableValue(std::in_place_type<std::string>, "bytesPerPixel"),
              EncodableValue(std::in_place_type<std::int32_t>, kBytesPerPixel));
    planes.emplace_back(std::in_place_type<EncodableMap>, std::move(m));
  }

  EncodableMap event;
  event.emplace(EncodableValue(std::in_place_type<std::string>, "width"),
                EncodableValue(std::in_place_type<std::int32_t>, w32));
  event.emplace(EncodableValue(std::in_place_type<std::string>, "height"),
                EncodableValue(std::in_place_type<std::int32_t>, h32));
  event.emplace(EncodableValue(std::in_place_type<std::string>, "formatGroup"),
                EncodableValue(std::in_place_type<std::string>, "yuv420"));
  event.emplace(EncodableValue(std::in_place_type<std::string>, "raw"),
                EncodableValue(std::in_place_type<std::string>, "I420"));
  event.emplace(
      EncodableValue(std::in_place_type<std::string>, "planes"),
      EncodableValue(std::in_place_type<EncodableList>, std::move(planes)));

  image_sink_->Success(
      EncodableValue(std::in_place_type<EncodableMap>, std::move(event)));
}

}  // namespace camera_plugin
