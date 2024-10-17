#include "texture_sink.h"

#include <plugins/common/common.h>

#include "texture_mjpg.h"

namespace camera_plugin {

TextureSink::TextureSink(flutter::TextureRegistrar* texture_registrar)
    : mTextureRegistrar(texture_registrar) {
  SPDLOG_DEBUG("[camera_plugin] TextureSink::TextureSink");
}

TextureSink::~TextureSink() {
  SPDLOG_DEBUG("[camera_plugin] TextureSink::~TextureSink");
  stop();
}

int TextureSink::configure(const libcamera::CameraConfiguration& config,
                           const unsigned int texture_id) {
  SPDLOG_DEBUG("[camera_plugin] TextureSink::configure");
  int ret = FrameSink::configure(config, texture_id);
  if (ret < 0) {
    return ret;
  }

  if (config.size() > 1) {
    spdlog::error(
        "[camera_plugin] sink only supports one camera stream at present, "
        "streaming first "
        "camera stream");
  } else if (config.empty()) {
    spdlog::error(
        "[camera_plugin] Require at least one camera stream to process");
    return -EINVAL;
  }

  const auto& cfg = config.at(0);

  mSize = libcamera::Size(cfg.size.width, cfg.size.height);
  mRect = std::make_shared<libcamera::Rectangle>(0, 0, mSize);
  SPDLOG_DEBUG("[camera_plugin] [{},{}] {}x{}", mRect->x, mRect->y,
               mRect->width, mRect->height);

  switch (cfg.pixelFormat) {
    case libcamera::formats::MJPEG:
      mTexture =
          std::make_unique<TextureMJPG>(mTextureRegistrar, texture_id,
                                        cfg.size.width, cfg.size.height, mRect);
      break;
    default:
      spdlog::error("[camera_plugin] Unsupported pixel format {}",
                    cfg.pixelFormat.toString());
      return -EINVAL;
  };

  return 0;
}

int TextureSink::start() {
  SPDLOG_DEBUG("[camera_plugin] TextureSink::start");

  mInit = true;

  return mTexture->create();
}

int TextureSink::stop() {
  SPDLOG_DEBUG("[camera_plugin] TextureSink::stop");

  mTexture.reset();

  if (mInit) {
    mInit = false;
  }

  return FrameSink::stop();
}

void TextureSink::map_buffer(libcamera::FrameBuffer* buffer) {
  SPDLOG_TRACE("[camera_plugin] TextureSink::map_buffer");

  auto image = Image::fromFrameBuffer(buffer, Image::MapMode::ReadOnly);
  assert(image != nullptr);

  mMappedBuffers[buffer] = std::move(image);
}

bool TextureSink::process_request(libcamera::Request* request) {
  for (auto [stream, buffer] : request->buffers()) {
    SPDLOG_TRACE("[camera_plugin] process_request: {}",
                 stream->configuration().toString());
    render_buffer(buffer);
    break;
  }

  return true;
}

int TextureSink::take_picture(std::string filename) {
  SPDLOG_TRACE("[camera_plugin] TextureSink::take_picture");
  SPDLOG_DEBUG("[camera_plugin] takePicture!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");

  SPDLOG_DEBUG("filename: {}", filename);
  if(mImage) {
      int fd, ret = 0;
      size_t pos;
      fd = open(filename.c_str(), O_CREAT | O_WRONLY |
                (pos == std::string::npos ? O_APPEND : O_TRUNC),
                S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
      if (fd == -1) {
        ret = -errno;
        std::cerr << "failed to open file " << filename << ": "
                  << strerror(-ret) << std::endl;
        return 1;
      }

      Image* image = mMappedBuffers[latestFrameBuffer].get();
      std::vector<libcamera::Span<const uint8_t>> planes;
      unsigned int i = 0;

      for (const libcamera::FrameMetadata::Plane& meta :
           latestFrameBuffer->metadata().planes()) {
        libcamera::Span<uint8_t> data = image->data(i);
        if (meta.bytesused > data.size())
          spdlog::error("payload size {} larger than plane size {}", meta.bytesused,
                        data.size());
        const unsigned int length = std::min<unsigned int>(meta.bytesused, data.size());
        ret = ::write(fd, data.data(), length);
        if (ret < 0) {
          ret = -errno;
          std::cerr << "write error: " << strerror(-ret)
                    << std::endl;
          break;
        } else if (ret != (int)length) {
          std::cerr << "write error: only " << ret
                    << " bytes written instead of "
                    << length << std::endl;
          break;
        }
        planes.emplace_back(data);
        i++;
      }

      close(fd);


  }
  return 0;
}


void TextureSink::render_buffer(libcamera::FrameBuffer* buffer) {
  latestFrameBuffer = buffer;
  SPDLOG_TRACE("[camera_plugin] TextureSink::render_buffer");
  //Image* image = mMappedBuffers[buffer].get();
  mImage = mMappedBuffers[buffer].get();

  std::vector<libcamera::Span<const uint8_t>> planes;
  unsigned int i = 0;

  planes.reserve(buffer->metadata().planes().size());

  for (const libcamera::FrameMetadata::Plane& meta :
       buffer->metadata().planes()) {
    libcamera::Span<uint8_t> data = mImage->data(i);
    if (meta.bytesused > data.size())
      spdlog::error("payload size {} larger than plane size {}", meta.bytesused,
                    data.size());
    planes.emplace_back(data);
    i++;
  }

  mTexture->update(planes);

#if 0
  SDL_RenderClear(renderer_);
  SDL_RenderCopy(renderer_, texture_->get(), nullptr, nullptr);
  SDL_RenderPresent(renderer_);
#endif
}

}  // namespace camera_plugin