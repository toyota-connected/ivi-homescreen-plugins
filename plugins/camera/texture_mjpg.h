
#ifndef PLUGINS_CAMERA_TEXTURE_MJPG_H_
#define PLUGINS_CAMERA_TEXTURE_MJPG_H_

#include <memory>

#include "texture.h"

#include <libcamera/libcamera.h>

#include <texture_registrar.h>

namespace camera_plugin {

class TextureMJPG final : public Texture {
 public:
  explicit TextureMJPG(flutter::TextureRegistrar* texture_registrar,
                       GLuint texture_id,
                       int width,
                       int height,
                       const std::shared_ptr<libcamera::Rectangle>& rect);

  void update(const std::vector<libcamera::Span<const uint8_t>>& data) override;

 private:
  [[nodiscard]] int decompress(libcamera::Span<const uint8_t> data) const;

  void* rgb_;
};
}  // namespace camera_plugin

#endif  // PLUGINS_CAMERA_TEXTURE_MJPG_H_