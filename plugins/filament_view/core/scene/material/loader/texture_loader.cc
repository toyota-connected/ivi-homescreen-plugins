
#include "core/scene/material/loader/texture_loader.h"
#include <imageio/ImageDecoder.h>
#include <memory>
#include "core/include/file_utils.h"
#include "plugins/common/curl_client/curl_client.h"
#include <stb_image.h>

namespace plugin_filament_view {

TextureLoader::TextureLoader() = default;

inline ::filament::backend::TextureFormat internalFormat(
    Texture::TextureType type) {
  return ::filament::backend::TextureFormat::RGBA8;

  switch (type) {
    case Texture::TextureType::COLOR:
    SPDLOG_ERROR("Returning color");
      return ::filament::backend::TextureFormat::SRGB8_A8;
    case Texture::TextureType::NORMAL:
    case Texture::TextureType::DATA:
    SPDLOG_ERROR("Returning data/normal");
      return ::filament::backend::TextureFormat::RGBA8;
  }

  throw std::runtime_error("Invalid texture type");
}

::filament::Texture* TextureLoader::createTextureFromImage(
  const std::string& file_path,
    Texture::TextureType type) {
#if 0
  static constexpr int kNumChannelsForImage = 3;
  SPDLOG_ERROR("createTextureFromImage 1");

  if (image->getChannels() != kNumChannelsForImage) {
    SPDLOG_ERROR("Channels != {} {} {}", kNumChannelsForImage, __FILE__,
                 __FUNCTION__);
    return nullptr;
  }

  SPDLOG_WARN("Width {} Height {} Channels {} ", image->getWidth()
    , image->getHeight(), kNumChannelsForImage);

  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  ::filament::Engine* engine = modelViewer->getFilamentEngine();

  ::filament::Texture* texture =
      ::filament::Texture::Builder()
          .width(image->getWidth())
          .height(image->getHeight())
          .levels(1) // TODO should be param
          .format(internalFormat(type))
          .sampler(::filament::Texture::Sampler::SAMPLER_2D)
          .build(*engine);

  if (!texture) {
    spdlog::error("Unable to create Filament Texture from image.");
    return nullptr;
  }

  //Path path = FilamentApp::getRootAssetsPath() + "textures/Moss_01/Moss_01_Color.png";
  //unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);

  ::filament::Texture::PixelBufferDescriptor::Callback freeCallback =
      [](void* /* buf */, size_t, void* userdata) {
        delete (image::LinearImage*)userdata;
      };

  ::filament::Texture::PixelBufferDescriptor pbd(
      (void const*)image->getPixelRef(),
      size_t(image->getWidth() * image->getHeight() * 3),
      ::filament::Texture::PixelBufferDescriptor::PixelDataFormat::RGB,
      ::filament::Texture::PixelBufferDescriptor::PixelDataType::UBYTE,
      freeCallback, image.get());

  texture->setImage(*engine, 0, std::move(pbd));
  texture->generateMipmaps(*engine);

  auto releasedImage =
    image.release();  // Release the ownership since filament engine has the
  // responsibility to free the memory
  (void)releasedImage;

  // TODO REOMVE
  sleep(1);

  return texture;
#else
  int w, h, n;
  unsigned char* data = stbi_load(file_path.c_str(), &w, &h, &n, 4);

  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  ::filament::Engine* engine = modelViewer->getFilamentEngine();

  ::filament::Texture* texture =
      ::filament::Texture::Builder()
  .width(uint32_t(w))
        .height(uint32_t(h))
          .levels(1) // TODO should be param
          .format(internalFormat(type))
          .sampler(::filament::Texture::Sampler::SAMPLER_2D)
          .build(*engine);

  if (!texture) {
    spdlog::error("Unable to create Filament Texture from image.");
    return nullptr;
  }

  ::filament::Texture::PixelBufferDescriptor pbd(
      data,
      size_t(w * h * 4),
      ::filament::Texture::PixelBufferDescriptor::PixelDataFormat::RGBA,
      ::filament::Texture::PixelBufferDescriptor::PixelDataType::UBYTE,
      (::filament::Texture::PixelBufferDescriptor::Callback) &stbi_image_free);

  texture->setImage(*engine, 0, std::move(pbd));
  texture->generateMipmaps(*engine);

  // TODO REOMVE
  //sleep(1);

  return texture;
#endif

}

Resource<::filament::Texture*> TextureLoader::loadTexture(Texture* texture) {
  if (!texture) {
    spdlog::error("Texture not found");
    return Resource<::filament::Texture*>::Error(
        "Invalid filament_view texture passed into into loadTexture.");
  }

  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);

  if (!texture->assetPath_.empty()) {
    auto file_path =
        getAbsolutePath(texture->assetPath_, modelViewer->getAssetPath());
    if (!isValidFilePath(file_path)) {
      spdlog::error("Texture Asset path is invalid: {}", file_path.c_str());
      return Resource<::filament::Texture*>::Error(
         "Could not load texture from asset.");
    }
    //new std::ifstream(file_path, std::ios::binary)
    auto loadedTexture = loadTextureFromStream(file_path,
                                 texture->type_, texture->assetPath_);
    if(!loadedTexture) {
      return Resource<::filament::Texture*>::Error(
        "Could not load texture from asset on disk.");
    }
    return Resource<::filament::Texture*>::Success(loadedTexture);
  }

  if (!texture->url_.empty()) {
    return Resource<::filament::Texture*>::Error("URL Not implemented.");
    /*auto loadedTexture = loadTextureFromUrl(texture->url_, texture->type_);
    if(!loadedTexture) {
      return Resource<::filament::Texture*>::Error(
        "Could not load texture asset from url.");
    }
    return Resource<::filament::Texture*>::Success(loadedTexture);*/
  }
  SPDLOG_ERROR("Load Texture 7");

  spdlog::error("You must provide texture images asset path or url");
  return Resource<::filament::Texture*>::Error(
         "You must provide texture images asset path or url.");
}

::filament::Texture* TextureLoader::loadTextureFromStream(
    const std::string& file_path,
    Texture::TextureType type,
    const std::string& name) {


SPDLOG_ERROR("FILE NAME {}", file_path.c_str());
  //auto* image = new image::LinearImage(image::ImageDecoder::decode(*ins, name));
  return createTextureFromImage(file_path, type);
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
  return loadTextureFromStream(str, type, url);
}

}  // namespace plugin_filament_view
