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

#include "indirect_light_system.h"

#include <filesystem>
#include <memory>

#include <core/include/literals.h>
#include <core/systems/derived/filament_system.h>
#include <core/systems/ecsystems_manager.h>
#include <filament/Texture.h>
#include <asio/post.hpp>
#include <utility>

#include "core/utils/hdr_loader.h"
#include "plugins/common/common.h"

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////////////
void IndirectLightSystem::setDefaultIndirectLight() {
  SPDLOG_TRACE("++IndirectLightSystem::setDefaultIndirectLight");
  indirect_light_ = std::make_unique<DefaultIndirectLight>();
  setIndirectLight(indirect_light_.get());
}

////////////////////////////////////////////////////////////////////////////////////
IndirectLightSystem::~IndirectLightSystem() = default;

////////////////////////////////////////////////////////////////////////////////////
std::future<Resource<std::string_view>> IndirectLightSystem::setIndirectLight(
    DefaultIndirectLight* indirectLight) {
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto future(promise->get_future());

  // Note: LightState to custom model viewer was done here.
  // todo copy values to internal var.

  if (!indirectLight) {
    promise->set_value(Resource<std::string_view>::Error("Light is null"));
    return future;
  }

  const asio::io_context::strand& strand_(
      *ECSystemManager::GetInstance()->GetStrand());

  asio::post(strand_, [&, promise, indirectLight] {
    auto builder = ::filament::IndirectLight::Builder();
    builder.intensity(indirectLight->getIntensity());
    builder.radiance(static_cast<uint8_t>(indirectLight->radiance_.size()),
                     indirectLight->radiance_.data());
    builder.irradiance(static_cast<uint8_t>(indirectLight->irradiance_.size()),
                       indirectLight->irradiance_.data());
    if (indirectLight->rotation_.has_value()) {
      builder.rotation(indirectLight->rotation_.value());
    }

    auto filamentSystem =
        ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
            FilamentSystem::StaticGetTypeID(), "setIndirectLight");
    const auto engine = filamentSystem->getFilamentEngine();

    builder.build(*engine);

    promise->set_value(
        Resource<std::string_view>::Success("changed Light successfully"));
  });
  return future;
}

////////////////////////////////////////////////////////////////////////////////////
std::future<Resource<std::string_view>>
IndirectLightSystem::setIndirectLightFromKtxAsset(std::string path,
                                                  double /*intensity*/) {
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto future(promise->get_future());

  const asio::io_context::strand& strand_(
      *ECSystemManager::GetInstance()->GetStrand());

  asio::post(strand_, [&, promise, path = std::move(path) /*, intensity*/] {
    promise->set_value(Resource<std::string_view>::Error("Not implemented"));
  });
  return future;
}

////////////////////////////////////////////////////////////////////////////////////
std::future<Resource<std::string_view>>
IndirectLightSystem::setIndirectLightFromKtxUrl(std::string url,
                                                double /*intensity*/) {
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto future(promise->get_future());

  const asio::io_context::strand& strand_(
      *ECSystemManager::GetInstance()->GetStrand());

  asio::post(strand_, [&, promise, url = std::move(url) /*, intensity*/] {
    promise->set_value(Resource<std::string_view>::Error("Not implemented"));
  });
  return future;
}

////////////////////////////////////////////////////////////////////////////////////
Resource<std::string_view> IndirectLightSystem::loadIndirectLightHdrFromFile(
    const std::string& asset_path,
    double intensity) {
  auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "loadIndirectLightHdrFromFile");
  const auto engine = filamentSystem->getFilamentEngine();

  ::filament::Texture* texture;
  try {
    texture = HDRLoader::createTexture(engine, asset_path);
  } catch (...) {
    return Resource<std::string_view>::Error("Could not decode HDR file");
  }
  auto skyboxTexture =
      filamentSystem->getIBLProfiler()->createCubeMapTexture(texture);
  engine->destroy(texture);

  auto reflections =
      filamentSystem->getIBLProfiler()->getLightReflection(skyboxTexture);

  auto ibl = ::filament::IndirectLight::Builder()
                 .reflections(reflections)
                 .intensity(static_cast<float>(intensity))
                 .build(*engine);

  auto prevIndirectLight =
      filamentSystem->getFilamentScene()->getIndirectLight();
  if (prevIndirectLight) {
    engine->destroy(prevIndirectLight);
  }

  filamentSystem->getFilamentScene()->setIndirectLight(ibl);

  return Resource<std::string_view>::Success(
      "loaded Indirect light successfully");
}

////////////////////////////////////////////////////////////////////////////////////
std::future<Resource<std::string_view>>
IndirectLightSystem::setIndirectLightFromHdrAsset(std::string path,
                                                  double intensity) {
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto future(promise->get_future());

  const asio::io_context::strand& strand_(
      *ECSystemManager::GetInstance()->GetStrand());
  const auto assetPath =
      ECSystemManager::GetInstance()->getConfigValue<std::string>(kAssetPath);

  asio::post(
      strand_, [&, promise, path = std::move(path), intensity, assetPath] {
        std::filesystem::path asset_path(assetPath);
        asset_path /= path;

        if (path.empty() || !std::filesystem::exists(asset_path)) {
          promise->set_value(
              Resource<std::string_view>::Error("Asset path not valid"));
        }
        try {
          promise->set_value(
              loadIndirectLightHdrFromFile(asset_path.c_str(), intensity));
        } catch (...) {
          promise->set_value(Resource<std::string_view>::Error(
              "Couldn't changed Light from asset"));
        }
      });
  return future;
}

////////////////////////////////////////////////////////////////////////////////////
std::future<Resource<std::string_view>>
IndirectLightSystem::setIndirectLightFromHdrUrl(std::string url,
                                                double /*intensity*/) {
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());

  const asio::io_context::strand& strand_(
      *ECSystemManager::GetInstance()->GetStrand());

  auto future(promise->get_future());
  asio::post(strand_, [&, promise, url = std::move(url) /*, intensity*/] {
    promise->set_value(Resource<std::string_view>::Error("Not implemented"));
  });
  return future;
}

////////////////////////////////////////////////////////////////////////////////////
void IndirectLightSystem::vInitSystem() {
  setDefaultIndirectLight();
}

////////////////////////////////////////////////////////////////////////////////////
void IndirectLightSystem::vUpdate(float /*fElapsedTime*/) {}

////////////////////////////////////////////////////////////////////////////////////
void IndirectLightSystem::vShutdownSystem() {
  auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "setIndirectLight");
  const auto engine = filamentSystem->getFilamentEngine();

  auto prevIndirectLight =
      filamentSystem->getFilamentScene()->getIndirectLight();
  if (prevIndirectLight) {
    engine->destroy(prevIndirectLight);
  }

  indirect_light_.reset();
}

////////////////////////////////////////////////////////////////////////////////////
void IndirectLightSystem::DebugPrint() {
  spdlog::debug("{}::{}", __FILE__, __FUNCTION__);
}

}  // namespace plugin_filament_view