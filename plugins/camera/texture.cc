
#include "texture.h"

#include <GLES3/gl3.h>
#include <plugins/common/common.h>
#include <texture_registrar.h>

#include <utility>

namespace camera_plugin {

Texture::Texture(flutter::TextureRegistrar* texture_registrar,
                 const GLuint texture_id,
                 const int width,
                 const int height,
                 std::shared_ptr<libcamera::Rectangle> rect,
                 const uint32_t pixelFormat,
                 const int stride)
    : texture_registrar_(texture_registrar),
      ptr_(nullptr),
      rect_(std::move(rect)),
      pixelFormat_(pixelFormat),
      stride_(stride),
      textureId_(texture_id),
      width_(width),
      height_(height) {
  SPDLOG_DEBUG(
      "[camera_plugin] Texture::Texture, width: {}, height: {}, rect: {}, "
      "stride: {}",
      width, height, rect ? rect->toString() : "nullptr", stride);
}

Texture::~Texture() {
  SPDLOG_DEBUG("[camera_plugin] Texture::~Texture");

  if (program_) {
    glDeleteProgram(program_);
  }
  if (framebuffer_) {
    glDeleteFramebuffers(1, &framebuffer_);
  }
}

int Texture::create() {
  SPDLOG_TRACE("[camera_plugin] Texture::create");

  /// Setup GL Texture 2D
  texture_registrar_->TextureMakeCurrent();
  //  glGenTextures(1, &textureId_);
  glGenFramebuffers(1, &framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, GL_NONE);

  texture_registrar_->TextureClearCurrent();

  descriptor_ = {
      .struct_size = sizeof(FlutterDesktopGpuSurfaceDescriptor),
      .handle = &textureId_,
      .width = static_cast<size_t>(width_),
      .height = static_cast<size_t>(height_),
      .visible_width = static_cast<size_t>(width_),
      .visible_height = static_cast<size_t>(height_),
      .format = kFlutterDesktopPixelFormatRGBA8888,
      .release_callback = [](void* /* release_context */) {},
      .release_context = this,
  };

  gpu_surface_texture_ = std::make_unique<flutter::GpuSurfaceTexture>(
      FlutterDesktopGpuSurfaceType::kFlutterDesktopGpuSurfaceTypeGlTexture2D,
      [&](size_t /* width */,
          size_t /* height */) -> const FlutterDesktopGpuSurfaceDescriptor* {
        return &descriptor_;
      });

  SPDLOG_DEBUG("[camera_plugin] Texture: {}", textureId_);
  SPDLOG_DEBUG("[camera_plugin] Framebuffer: {}", framebuffer_);

  flutter::TextureVariant texture = *gpu_surface_texture_;
  texture_registrar_->RegisterTexture(&texture);
  texture_registrar_->MarkTextureFrameAvailable(textureId_);

  return 0;
}

void Texture::update(const std::vector<libcamera::Span<const uint8_t>>& data) {
  SPDLOG_DEBUG("[camera_plugin] Texture::update");

  texture_registrar_->TextureMakeCurrent();
  glClearColor(0., 0., 0., 1.);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(0, 0, width_, height_);

  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer_);
  glViewport(0, 0, width_, height_);
  glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_,
                    GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);

  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  texture_registrar_->TextureClearCurrent();
  texture_registrar_->MarkTextureFrameAvailable(textureId_);
}

void Texture::blit_fb(uint8_t const* pixels) const {
  SPDLOG_TRACE("[camera_plugin] Texture::blit_fb");

  texture_registrar_->TextureMakeCurrent();

  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glViewport(0, 0, width_, height_);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, textureId_);
  glUniform1i(0, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  // The following call requires a 32-bit aligned source buffer
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width_, height_, 0, GL_RGB,
               GL_UNSIGNED_BYTE, pixels);
  glGenerateMipmap(GL_TEXTURE_2D);

  glBindFramebuffer(GL_FRAMEBUFFER, GL_NONE);
  texture_registrar_->TextureClearCurrent();
  texture_registrar_->MarkTextureFrameAvailable(textureId_);
}

}  // namespace camera_plugin