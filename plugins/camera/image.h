#ifndef PLUGINS_CAMERA_IMAGE_H_
#define PLUGINS_CAMERA_IMAGE_H_

#include <libcamera/libcamera.h>

namespace camera_plugin {

class Image {
 public:
  enum class MapMode {
    ReadOnly = 1 << 0,
    WriteOnly = 1 << 1,
    ReadWrite = ReadOnly | WriteOnly,
  };

  static std::unique_ptr<Image> fromFrameBuffer(
      const libcamera::FrameBuffer* buffer,
      MapMode mode);

  ~Image();

  [[nodiscard]] unsigned int numPlanes() const;

  libcamera::Span<uint8_t> data(unsigned int plane);
  [[nodiscard]] libcamera::Span<const uint8_t> data(unsigned int plane) const;

  Image(const Image&) = delete;
  Image& operator=(const Image&) = delete;

 private:
  Image();

  std::vector<libcamera::Span<uint8_t>> mMaps;
  std::vector<libcamera::Span<uint8_t>> mPlanes;
};

}  // namespace camera_plugin

namespace libcamera {
LIBCAMERA_FLAGS_ENABLE_OPERATORS(camera_plugin::Image::MapMode)
}

#endif  // PLUGINS_CAMERA_IMAGE_H_