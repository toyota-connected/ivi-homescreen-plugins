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

#include "skybox_system.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <core/include/color.h>
#include <core/include/literals.h>
#include <core/systems/derived/filament_system.h>
#include <core/systems/ecsystems_manager.h>
#include <core/utils/hdr_loader.h>
#include <filament/IndirectLight.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <plugins/common/common.h>
#include <plugins/common/curl_client/curl_client.h>
#include <asio/post.hpp>

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////////////
/// TODO Need to look into destruction between here and scene deserializer
void SkyboxSystem::destroySkybox() {}

////////////////////////////////////////////////////////////////////////////////////
std::future<void> SkyboxSystem::Initialize() {
  const auto promise(std::make_shared<std::promise<void>>());
  auto future(promise->get_future());

  const asio::io_context::strand& strand_(
      *ECSystemManager::GetInstance()->GetStrand());

  post(strand_, [&, promise] {
    const auto filamentSystem =
        ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
            FilamentSystem::StaticGetTypeID(), "SKyboxManager::Init::Lambda");
    const auto engine = filamentSystem->getFilamentEngine();

    const auto whiteSkybox = filament::Skybox::Builder()
                                 .color({1.0f, 1.0f, 1.0f, 1.0f})
                                 .build(*engine);
    filamentSystem->getFilamentScene()->setSkybox(whiteSkybox);

    promise->set_value();
  });

  return future;
}

////////////////////////////////////////////////////////////////////////////////////
void SkyboxSystem::setDefaultSkybox() {
  SPDLOG_TRACE("++SkyboxManager::setDefaultSkybox");
  setTransparentSkybox();
  SPDLOG_TRACE("--SkyboxManager::setDefaultSkybox");
}

////////////////////////////////////////////////////////////////////////////////////
void SkyboxSystem::setTransparentSkybox() {
  const auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "setTransparentSkybox");

  filamentSystem->getFilamentScene()->setSkybox(nullptr);
}

////////////////////////////////////////////////////////////////////////////////////
std::future<Resource<std::string_view>> SkyboxSystem::setSkyboxFromHdrAsset(
    const std::string& path,
    bool showSun,
    bool shouldUpdateLight,
    float intensity) {
  SPDLOG_TRACE("++SkyboxManager::setSkyboxFromHdrAsset");
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto future(promise->get_future());

  const auto assetPath =
      ECSystemManager::GetInstance()->getConfigValue<std::string>(kAssetPath);

  std::filesystem::path asset_path = assetPath;

  asset_path /= path;
  if (path.empty() || !exists(asset_path)) {
    promise->set_value(
        Resource<std::string_view>::Error("Skybox Asset path is not valid"));
  }

  const asio::io_context::strand& strand_(
      *ECSystemManager::GetInstance()->GetStrand());

  post(strand_,
       [&, promise, asset_path, showSun, shouldUpdateLight, intensity] {
         promise->set_value(loadSkyboxFromHdrFile(
             asset_path, showSun, shouldUpdateLight, intensity));
       });

  SPDLOG_TRACE("--SkyboxManager::setSkyboxFromHdrAsset");
  return future;
}

////////////////////////////////////////////////////////////////////////////////////
std::future<Resource<std::string_view>> SkyboxSystem::setSkyboxFromHdrUrl(
    const std::string& url,
    bool showSun,
    bool shouldUpdateLight,
    float intensity) {
  SPDLOG_TRACE("++SkyboxManager::setSkyboxFromHdrUrl");
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto future(promise->get_future());

  if (url.empty()) {
    promise->set_value(Resource<std::string_view>::Error("URL is empty"));
    return future;
  }

  const asio::io_context::strand& strand_(
      *ECSystemManager::GetInstance()->GetStrand());

  SPDLOG_DEBUG("Skybox downloading HDR Asset: {}", url.c_str());
  post(strand_, [&, promise, url, showSun, shouldUpdateLight, intensity] {
    plugin_common_curl::CurlClient client;
    // todo client.Init(url, {}, {});
    const auto buffer = client.RetrieveContentAsVector();
    if (client.GetCode() != CURLE_OK) {
      promise->set_value(Resource<std::string_view>::Error(
          "Couldn't load skybox from " + url));
    }
    if (!buffer.empty()) {
      promise->set_value(loadSkyboxFromHdrBuffer(buffer, showSun,
                                                 shouldUpdateLight, intensity));
    } else {
      promise->set_value(Resource<std::string_view>::Error(
          "Couldn't load HDR file from " + url));
    }
  });
  SPDLOG_TRACE("--SkyboxManager::setSkyboxFromHdrUrl");
  return future;
}

////////////////////////////////////////////////////////////////////////////////////
std::future<Resource<std::string_view>> SkyboxSystem::setSkyboxFromKTXAsset(
    const std::string& path) {
  SPDLOG_TRACE("++SkyboxManager::setSkyboxFromKTXAsset");
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto future(promise->get_future());

  const auto assetPath =
      ECSystemManager::GetInstance()->getConfigValue<std::string>(kAssetPath);

  std::filesystem::path asset_path = assetPath;
  asset_path /= path;
  if (path.empty() || !exists(asset_path)) {
    promise->set_value(
        Resource<std::string_view>::Error("KTX Asset path is not valid"));
  }
  const asio::io_context::strand& strand_(
      *ECSystemManager::GetInstance()->GetStrand());

  SPDLOG_DEBUG("Skybox loading KTX Asset: {}", asset_path.c_str());
  post(strand_, [&, promise, asset_path] {
    std::ifstream stream(asset_path, std::ios::in | std::ios::binary);
    std::vector<uint8_t> buffer((std::istreambuf_iterator(stream)),
                                std::istreambuf_iterator<char>());
    if (!buffer.empty()) {
#if 0  // TODO
                auto skybox = KTX1Loader.createSkybox(*engine, buffer);
                modelViewer_->destroySkybox();
                modelViewer_->getFilamentScene()->setSkybox(skybox);
                modelViewer_->setSkyboxState(SceneState::LOADED);
#endif
      std::stringstream ss;
      ss << "Loaded environment successfully from " << asset_path;
      promise->set_value(Resource<std::string_view>::Success(ss.str()));
    } else {
      std::stringstream ss;
      ss << "Couldn't change environment from " << asset_path;
      promise->set_value(Resource<std::string_view>::Error(ss.str()));
    }
  });
  SPDLOG_TRACE("--SkyboxManager::setSkyboxFromKTXAsset");
  return future;
}

////////////////////////////////////////////////////////////////////////////////////
std::future<Resource<std::string_view>> SkyboxSystem::setSkyboxFromKTXUrl(
    const std::string& url) {
  SPDLOG_TRACE("++SkyboxManager::setSkyboxFromKTXUrl");
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto future(promise->get_future());
  if (url.empty()) {
    promise->set_value(Resource<std::string_view>::Error("URL is empty"));
    return future;
  }

  const asio::io_context::strand& strand_(
      *ECSystemManager::GetInstance()->GetStrand());
  post(strand_, [&, promise, url] {
    plugin_common_curl::CurlClient client;

    // TODO client.Init(url, {}, {});
    auto buffer = client.RetrieveContentAsVector();
    if (client.GetCode() != CURLE_OK) {
      std::stringstream ss;
      ss << "Couldn't load skybox from " << url;
      promise->set_value(Resource<std::string_view>::Error(ss.str()));
    }

    if (!buffer.empty()) {
#if 0  // TODO
                auto skybox = KTX1Loader.createSkybox(*engine, buffer);
                modelViewer_->destroySkybox();
                modelViewer_->getFilamentScene()->setSkybox(skybox);
                modelViewer_->setSkyboxState(SceneState::LOADED);
#endif
      std::stringstream ss;
      ss << "Loaded skybox successfully from " << url;
      promise->set_value(Resource<std::string_view>::Success(ss.str()));
    } else {
      std::stringstream ss;
      ss << "Couldn't load skybox from " << url;
      promise->set_value(Resource<std::string_view>::Error(ss.str()));
    }
  });

  SPDLOG_TRACE("--SkyboxManager::setSkyboxFromKTXUrl");
  return future;
}

////////////////////////////////////////////////////////////////////////////////////
std::future<Resource<std::string_view>> SkyboxSystem::setSkyboxFromColor(
    const std::string& color) {
  SPDLOG_TRACE("++SkyboxManager::setSkyboxFromColor");
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto future(promise->get_future());

  if (color.empty()) {
    promise->set_value(Resource<std::string_view>::Error("Color is Invalid"));
    return future;
  }
  const asio::io_context::strand& strand_(
      *ECSystemManager::GetInstance()->GetStrand());

  post(strand_, [&, promise, color] {
    const auto filamentSystem =
        ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
            FilamentSystem::StaticGetTypeID(), "setSkyboxFromColor");
    const auto engine = filamentSystem->getFilamentEngine();

    const auto colorArray = colorOf(color);
    const auto skybox =
        filament::Skybox::Builder().color(colorArray).build(*engine);
    filamentSystem->getFilamentScene()->setSkybox(skybox);
    promise->set_value(Resource<std::string_view>::Success(
        "Loaded environment successfully from color"));
  });

  SPDLOG_TRACE("--SkyboxManager::setSkyboxFromColor");
  return future;
}

////////////////////////////////////////////////////////////////////////////////////
Resource<std::string_view> SkyboxSystem::loadSkyboxFromHdrFile(
    const std::string& assetPath,
    const bool showSun,
    const bool shouldUpdateLight,
    const float intensity) {
  filament::Texture* texture;

  const auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "loadSkyboxFromHdrFile");
  const auto engine = filamentSystem->getFilamentEngine();

  try {
    texture = HDRLoader::createTexture(engine, assetPath);
  } catch (...) {
    return Resource<std::string_view>::Error("Could not decode HDR buffer");
  }
  if (texture) {
    const auto skyboxTexture =
        filamentSystem->getIBLProfiler()->createCubeMapTexture(texture);
    engine->destroy(texture);

    if (skyboxTexture) {
      const auto sky = filament::Skybox::Builder()
                           .environment(skyboxTexture)
                           .showSun(showSun)
                           .build(*engine);

      // updates scene light with skybox when loaded with the same hdr file
      if (shouldUpdateLight) {
        const auto reflections =
            filamentSystem->getIBLProfiler()->getLightReflection(skyboxTexture);
        const auto ibl = filament::IndirectLight::Builder()
                             .reflections(reflections)
                             .intensity(intensity)
                             .build(*engine);
        // destroy the previous IBl
        const auto indirectLight =
            filamentSystem->getFilamentScene()->getIndirectLight();
        engine->destroy(indirectLight);
        filamentSystem->getFilamentScene()->setIndirectLight(ibl);
      }

      if (const auto prevSkybox =
              filamentSystem->getFilamentScene()->getSkybox()) {
        engine->destroy(prevSkybox);
      }

      filamentSystem->getFilamentScene()->setSkybox(sky);
    }
    return Resource<std::string_view>::Success(
        "Loaded hdr skybox successfully");
  }

  return Resource<std::string_view>::Error("Could not decode HDR file");
}

////////////////////////////////////////////////////////////////////////////////////
Resource<std::string_view> SkyboxSystem::loadSkyboxFromHdrBuffer(
    const std::vector<uint8_t>& buffer,
    const bool showSun,
    const bool shouldUpdateLight,
    const float intensity) {
  filament::Texture* texture;

  const auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "loadSkyboxFromHdrBuffer");
  const auto engine = filamentSystem->getFilamentEngine();

  try {
    texture = HDRLoader::createTexture(engine, buffer);
  } catch (...) {
    return Resource<std::string_view>::Error("Could not decode HDR buffer");
  }
  if (texture) {
    const auto skyboxTexture =
        filamentSystem->getIBLProfiler()->createCubeMapTexture(texture);
    engine->destroy(texture);

    if (skyboxTexture) {
      const auto sky = filament::Skybox::Builder()
                           .environment(skyboxTexture)
                           .showSun(showSun)
                           .build(*engine);

      // updates scene light with skybox when loaded with the same hdr file
      if (shouldUpdateLight) {
        const auto reflections =
            filamentSystem->getIBLProfiler()->getLightReflection(skyboxTexture);
        const auto ibl = filament::IndirectLight::Builder()
                             .reflections(reflections)
                             .intensity(intensity)
                             .build(*engine);
        // destroy the previous IBl
        const auto indirectLight =
            filamentSystem->getFilamentScene()->getIndirectLight();
        engine->destroy(indirectLight);
        filamentSystem->getFilamentScene()->setIndirectLight(ibl);
      }

      if (const auto prevSkybox =
              filamentSystem->getFilamentScene()->getSkybox()) {
        engine->destroy(prevSkybox);
      }

      filamentSystem->getFilamentScene()->setSkybox(sky);
    }
    return Resource<std::string_view>::Success(
        "Loaded hdr skybox successfully");
  }

  return Resource<std::string_view>::Error("Could not decode HDR file");
}

////////////////////////////////////////////////////////////////////////////////////
void SkyboxSystem::vInitSystem() {
  Initialize();
}

////////////////////////////////////////////////////////////////////////////////////
void SkyboxSystem::vUpdate(float /*fElapsedTime*/) {}

////////////////////////////////////////////////////////////////////////////////////
void SkyboxSystem::vShutdownSystem() {
  const auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "loadSkyboxFromHdrBuffer");
  const auto engine = filamentSystem->getFilamentEngine();

  if (const auto prevSkybox = filamentSystem->getFilamentScene()->getSkybox()) {
    engine->destroy(prevSkybox);
  }
}

////////////////////////////////////////////////////////////////////////////////////
void SkyboxSystem::DebugPrint() {
  spdlog::debug("{}::{}", __FILE__, __FUNCTION__);
}

}  // namespace plugin_filament_view