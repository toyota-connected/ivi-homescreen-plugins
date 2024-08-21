
#include "core/scene/material/loader/texture_loader.h"
#include <imageio/ImageDecoder.h>
#include <memory>
#include "core/include/file_utils.h"
#include "plugins/common/curl_client/curl_client.h"

namespace plugin_filament_view {

TextureLoader::TextureLoader() = default;

inline ::filament::backend::TextureFormat internalFormat(
    Texture::TextureType type) {
  switch (type) {
    case Texture::TextureType::COLOR:
      return ::filament::backend::TextureFormat::SRGB8_A8;
    case Texture::TextureType::NORMAL:
    case Texture::TextureType::DATA:
      return ::filament::backend::TextureFormat::RGBA8;
  }
}

::filament::Texture* TextureLoader::createTextureFromImage(
    Texture::TextureType type,
    std::unique_ptr<image::LinearImage> image) {
  static constexpr int kNumChannelsForImage = 3;
  if (image->getChannels() != kNumChannelsForImage) {
    SPDLOG_ERROR("Channels != {} {} {}", kNumChannelsForImage, __FILE__,
                 __FUNCTION__);
    return nullptr;
  }

  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  ::filament::Engine* engine = modelViewer->getFilamentEngine();

  ::filament::Texture* texture =
      ::filament::Texture::Builder()
          .width(image->getWidth())
          .height(image->getHeight())
          .levels(0xff)
          .format(internalFormat(type))
          .sampler(::filament::Texture::Sampler::SAMPLER_2D)
          .build(*engine);

  if (!texture) {
    spdlog::error("Unable to create Filament Texture from image.");
    return nullptr;
  }

  ::filament::Texture::PixelBufferDescriptor::Callback freeCallback =
      [](void* /* buf */, size_t, void* userdata) {
        delete (image::LinearImage*)userdata;
      };

  ::filament::Texture::PixelBufferDescriptor pbd(
      (void const*)image->getPixelRef(),
      image->getWidth() * image->getHeight() * 3 * sizeof(float),
      ::filament::Texture::PixelBufferDescriptor::PixelDataFormat::RGB,
      ::filament::Texture::PixelBufferDescriptor::PixelDataType::FLOAT,
      freeCallback, image.get());

  (void)image.release();  // Release the ownership since filament engine has the
                          // responsibility to free the memory
  texture->setImage(*engine, 0, std::move(pbd));
  texture->generateMipmaps(*engine);
  return texture;
}

::filament::Texture* TextureLoader::loadTexture(Texture* texture) {
  if (!texture) {
    spdlog::error("Texture not found");
    return nullptr;
  }

  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);

  if (!texture->assetPath_.empty()) {
    auto file_path =
        getAbsolutePath(texture->assetPath_, modelViewer->getAssetPath());
    if (!isValidFilePath(file_path)) {
      spdlog::error("Texture Asset path is invalid: {}", file_path.c_str());
      return nullptr;
    }
    return loadTextureFromStream(new std::ifstream(file_path, std::ios::binary),
                                 texture->type_, texture->assetPath_);
  } else if (!texture->url_.empty()) {
    return loadTextureFromUrl(texture->url_, texture->type_);
  } else {
    spdlog::error("You must provide texture images asset path or url");
    return nullptr;
  }
}

::filament::Texture* TextureLoader::loadTextureFromStream(
    std::istream* ins,
    Texture::TextureType type,
    const std::string& name) {
  auto* image = new image::LinearImage(image::ImageDecoder::decode(*ins, name));
  return createTextureFromImage(type,
                                std::unique_ptr<image::LinearImage>(image));
}

::filament::Texture* TextureLoader::loadTextureFromUrl(
    const std::string& url,
    Texture::TextureType type) {
  plugin_common_curl::CurlClient client;
  client.Init(url, {}, {});
  std::vector<uint8_t> buffer = client.RetrieveContentAsVector();
  if (client.GetCode() != CURLE_OK) {
    spdlog::error("Failed to load texture from {}", url);
    return nullptr;
  }
  std::string str(buffer.begin(), buffer.end());
  return loadTextureFromStream(new std::istringstream(str), type, url);
}

}  // namespace plugin_filament_view
