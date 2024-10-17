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
#include "hdr_loader.h"

#include <fstream>
#include <sstream>

#include <imageio/ImageDecoder.h>
#include <plugins/common/common.h>

namespace plugin_filament_view {

using namespace filament;
using namespace image;
using namespace utils;

////////////////////////////////////////////////////////////////////////////
Texture* HDRLoader::deleteImageAndLogError(LinearImage* image) {
  spdlog::error("Unable to create Filament Texture from HDR image.");
  delete image;
  return nullptr;
}

////////////////////////////////////////////////////////////////////////////
Texture* HDRLoader::createTextureFromImage(Engine* engine, LinearImage* image) {
  if (image->getChannels() != 3) {
    return deleteImageAndLogError(image);
  }

  Texture* texture = Texture::Builder()
                         .width(image->getWidth())
                         .height(image->getHeight())
                         .levels(0xff)
                         .format(Texture::InternalFormat::R11F_G11F_B10F)
                         .sampler(Texture::Sampler::SAMPLER_2D)
                         .build(*engine);

  if (!texture) {
    return deleteImageAndLogError(image);
  }

  const Texture::PixelBufferDescriptor::Callback freeCallback =
      [](void* /* buf */, size_t, void* userdata) {
        delete static_cast<LinearImage*>(userdata);
      };

  Texture::PixelBufferDescriptor pbd(
      (image->getPixelRef()),
      image->getWidth() * image->getHeight() * 3 * sizeof(float),
      Texture::PixelBufferDescriptor::PixelDataFormat::RGB,
      Texture::PixelBufferDescriptor::PixelDataType::FLOAT, freeCallback,
      image);

  texture->setImage(*engine, 0, std::move(pbd));
  texture->generateMipmaps(*engine);
  return texture;
}

////////////////////////////////////////////////////////////////////////////
Texture* HDRLoader::createTexture(Engine* engine,
                                  const std::string& asset_path,
                                  const std::string& name) {
  SPDLOG_DEBUG("Loading {}", asset_path.c_str());
  std::ifstream ins(asset_path, std::ios::binary);
  auto* image = new LinearImage(ImageDecoder::decode(ins, name));
  return createTextureFromImage(engine, image);
}

////////////////////////////////////////////////////////////////////////////
Texture* HDRLoader::createTexture(Engine* engine,
                                  const std::vector<uint8_t>& buffer,
                                  const std::string& name) {
  const std::string str(buffer.begin(), buffer.end());
  std::istringstream ins(str);
  auto* image = new LinearImage(ImageDecoder::decode(ins, name));
  return createTextureFromImage(engine, image);
}
}  // namespace plugin_filament_view