
#ifndef PLUGINS_CAMERA_TEXTURE_SINK_H_
#define PLUGINS_CAMERA_TEXTURE_SINK_H_

#include "frame_sink.h"

#include "image.h"
#include "texture.h"

#include <texture_registrar.h>

namespace camera_plugin {
class TextureSink final : public FrameSink {
 public:
  explicit TextureSink(flutter::TextureRegistrar* texture_registrar);
  ~TextureSink() override;
  int configure(const libcamera::CameraConfiguration& config, unsigned int texture_id) override;
  void map_buffer(libcamera::FrameBuffer* buffer) override;
  int start() override;
  int stop() override;
  bool process_request(libcamera::Request* request) override;
  int take_picture(std::string filename) override;;

 private:
  flutter::TextureRegistrar* mTextureRegistrar;
  std::map<libcamera::FrameBuffer*, std::unique_ptr<Image>> mMappedBuffers;

  std::unique_ptr<Texture> mTexture;

  libcamera::Size mSize;
  std::shared_ptr<libcamera::Rectangle> mRect;
  bool mInit{};
  Image* mImage;
  libcamera::FrameBuffer* latestFrameBuffer;
  void render_buffer(libcamera::FrameBuffer* buffer);
};
}  // namespace camera_plugin

#endif  // PLUGINS_CAMERA_TEXTURE_SINK_H_