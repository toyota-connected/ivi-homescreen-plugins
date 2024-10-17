
#include "texture_mjpg.h"

#include <csetjmp>

#include <jpeglib.h>

#include <plugins/common/common.h>

namespace camera_plugin {

struct JpegErrorManager : jpeg_error_mgr {
  JpegErrorManager() : jpeg_error_mgr() {
    SPDLOG_TRACE("[camera_plugin] JpegErrorManager::JpegErrorManager");
    jpeg_std_error(this);
    error_exit = errorExit;
    output_message = outputMessage;
  }

  static void errorExit(j_common_ptr cinfo) {
    SPDLOG_TRACE("[camera_plugin] JpegErrorManager::errorExit");
    const auto self = reinterpret_cast<JpegErrorManager*>(cinfo->err);
    longjmp(self->escape_, 1);
  }

  static void outputMessage([[maybe_unused]] j_common_ptr cinfo) {}

  jmp_buf escape_{};
};

TextureMJPG::TextureMJPG(flutter::TextureRegistrar* texture_registrar,
                         const GLuint texture_id,
                         const int width,
                         const int height,
                         const std::shared_ptr<libcamera::Rectangle>& rect)
    : Texture(texture_registrar,
              texture_id,
              width,
              height,
              rect,
              0,
              rect->width * 3),
      rgb_(nullptr) {
  SPDLOG_DEBUG(
      "[camera_plugin] TextureMJPG::TextureMJPG, stride: {}, height: {}, size: "
      "{}",
      stride_, rect->height, stride_ * rect->height);

  posix_memalign(&rgb_, 32, stride_ * rect->height);
}

int TextureMJPG::decompress(libcamera::Span<const uint8_t> data) const {
  SPDLOG_TRACE("[camera_plugin] TextureMJPG::decompress");

  jpeg_decompress_struct cinfo{};

  JpegErrorManager errorManager;
  if (setjmp(errorManager.escape_)) {
    // libjpeg found an error
    jpeg_destroy_decompress(&cinfo);
    spdlog::error("JPEG decompression error");
    return -EINVAL;
  }

  cinfo.err = &errorManager;
  jpeg_create_decompress(&cinfo);

  jpeg_mem_src(&cinfo, data.data(), data.size());

  jpeg_read_header(&cinfo, TRUE);

  jpeg_start_decompress(&cinfo);

  for (int i = 0; cinfo.output_scanline < cinfo.output_height; ++i) {
    JSAMPROW rowptr = static_cast<unsigned char*>(rgb_) + i * stride_;
    jpeg_read_scanlines(&cinfo, &rowptr, 1);
  }

  jpeg_finish_decompress(&cinfo);

  jpeg_destroy_decompress(&cinfo);

  return 0;
}

void TextureMJPG::update(
    const std::vector<libcamera::Span<const uint8_t>>& data) {
  (void)decompress(data[0]);
  blit_fb(static_cast<uint8_t const*>(rgb_));
}

}  // namespace camera_plugin