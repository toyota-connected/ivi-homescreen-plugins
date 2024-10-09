/*
 * Copyright 2020-2024 Toyota Connected North America
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
#include "core/scene/material/loader/texture_loader.h"
#include <core/include/literals.h>
#include <core/systems/derived/filament_system.h>
#include <core/systems/ecsystems_manager.h>
#include <imageio/ImageDecoder.h>
#include <stb_image.h>
#include <memory>

#include "core/include/file_utils.h"
#include "plugins/common/curl_client/curl_client.h"

namespace plugin_filament_view {

TextureLoader::TextureLoader() = default;

inline ::filament::backend::TextureFormat internalFormat(
    const TextureDefinitions::TextureType type) {
  switch (type) {
    case TextureDefinitions::TextureType::COLOR:
      return ::filament::backend::TextureFormat::SRGB8_A8;
    case TextureDefinitions::TextureType::NORMAL:
    case TextureDefinitions::TextureType::DATA:
      return ::filament::backend::TextureFormat::RGBA8;
  }

  throw std::runtime_error("Invalid texture type");
}

::filament::Texture* TextureLoader::createTextureFromImage(
    const std::string& file_path,
    const TextureDefinitions::TextureType type) {
  int w, h, n;
  unsigned char* data = stbi_load(file_path.c_str(), &w, &h, &n, 4);

  auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "createTextureFromImage");
  const auto engine = filamentSystem->getFilamentEngine();

  ::filament::Texture* texture =
      ::filament::Texture::Builder()
          .width(uint32_t(w))
          .height(uint32_t(h))
          .levels(1)  // TODO should be param, backlogged
          .format(internalFormat(type))
          .sampler(::filament::Texture::Sampler::SAMPLER_2D)
          .build(*engine);

  if (!texture) {
    spdlog::error("Unable to create Filament Texture from image.");
    return nullptr;
  }

  ::filament::Texture::PixelBufferDescriptor pbd(
      data, size_t(w * h * 4),
      ::filament::Texture::PixelBufferDescriptor::PixelDataFormat::RGBA,
      ::filament::Texture::PixelBufferDescriptor::PixelDataType::UBYTE,
      (::filament::Texture::PixelBufferDescriptor::Callback)&stbi_image_free);

  texture->setImage(*engine, 0, std::move(pbd));
  texture->generateMipmaps(*engine);

  return texture;
}

Resource<::filament::Texture*> TextureLoader::loadTexture(
    TextureDefinitions* texture) {
  if (!texture) {
    spdlog::error("Texture not found");
    return Resource<::filament::Texture*>::Error(
        "Invalid filament_view texture passed into into loadTexture.");
  }

  if (!texture->assetPath_.empty()) {
    const auto assetPath =
        ECSystemManager::GetInstance()->getConfigValue<std::string>(kAssetPath);

    auto file_path = getAbsolutePath(texture->assetPath_, assetPath);
    if (!isValidFilePath(file_path)) {
      spdlog::error("Texture Asset path is invalid: {}", file_path.c_str());
      return Resource<::filament::Texture*>::Error(
          "Could not load texture from asset.");
    }
    auto loadedTexture = loadTextureFromStream(file_path, texture->type_);
    if (!loadedTexture) {
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

  spdlog::error("You must provide texture images asset path or url");
  return Resource<::filament::Texture*>::Error(
      "You must provide texture images asset path or url.");
}

::filament::Texture* TextureLoader::loadTextureFromStream(
    const std::string& file_path,
    const TextureDefinitions::TextureType type) {
  return createTextureFromImage(file_path, type);
}

::filament::Texture* TextureLoader::loadTextureFromUrl(
    const std::string& url,
    const TextureDefinitions::TextureType type) {
  plugin_common_curl::CurlClient client;
  client.Init(url, {}, {});
  std::vector<uint8_t> buffer = client.RetrieveContentAsVector();
  if (client.GetCode() != CURLE_OK) {
    spdlog::error("Failed to load texture from {}", url);
    return nullptr;
  }
  std::string str(buffer.begin(), buffer.end());
  return loadTextureFromStream(str, type);
}

}  // namespace plugin_filament_view
