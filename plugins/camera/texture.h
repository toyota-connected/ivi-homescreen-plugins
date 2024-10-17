
#ifndef PLUGINS_CAMERA_TEXTURE_H_
#define PLUGINS_CAMERA_TEXTURE_H_

#include <GLES2/gl2.h>
#include <libcamera/libcamera.h>

#include <event_channel.h>
#include <event_sink.h>
#include <texture_registrar.h>

namespace camera_plugin {

class Texture {
 public:
  Texture(flutter::TextureRegistrar* texture_registrar,
          GLuint texture_id,
          int width,
          int height,
          std::shared_ptr<libcamera::Rectangle> rect,
          uint32_t pixelFormat,
          int stride);
  virtual ~Texture();
  int create();
  virtual void update(
      const std::vector<libcamera::Span<const uint8_t>>& data) = 0;
  [[nodiscard]] Texture* get() const { return ptr_; }

  void blit_fb(uint8_t const* pixels) const;

 protected:
  flutter::TextureRegistrar* texture_registrar_;
  Texture* ptr_;
  std::shared_ptr<libcamera::Rectangle> rect_;
  const uint32_t pixelFormat_;
  const int stride_;

 private:
  bool is_initialized{};

  // The internal Flutter event channel instance.
  flutter::EventChannel<>* event_channel_{};

  // The internal Flutter event sink instance, used to send events to the Dart
  // side.
  std::unique_ptr<flutter::EventSink<>> event_sink;

  GLuint textureId_{};
  GLuint framebuffer_{};
  GLuint program_{};
  GLsizei width_, height_;
  GLuint vertex_arr_id_{};

  // The Surface Descriptor sent to Flutter when a texture frame is available.
  std::unique_ptr<flutter::GpuSurfaceTexture> gpu_surface_texture_;
  FlutterDesktopGpuSurfaceDescriptor descriptor_{};
};

}  // namespace camera_plugin

#endif  // PLUGINS_CAMERA_TEXTURE_H_