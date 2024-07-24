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
#include "model_loader.h"

#include <algorithm>  // for max
#include <sstream>

#include <filament/DebugRegistry.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/TextureProvider.h>
#include <math/mat4.h>
#include <math/vec3.h>
#include <asio/post.hpp>

#include "gltfio/materials/uberarchive.h"

#include "core/include/file_utils.h"
#include "plugins/common/curl_client/curl_client.h"

namespace plugin_filament_view {

using ::filament::gltfio::AssetConfiguration;
using ::filament::gltfio::AssetLoader;
using ::filament::gltfio::ResourceConfiguration;
using ::filament::gltfio::ResourceLoader;

ModelLoader::ModelLoader()
{
  SPDLOG_TRACE("++ModelLoader::ModelLoader");

  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  ::filament::Engine* engine = modelViewer->getFilamentEngine();

  materialProvider_ = ::filament::gltfio::createUbershaderProvider(
      engine, UBERARCHIVE_DEFAULT_DATA,
      static_cast<size_t>(UBERARCHIVE_DEFAULT_SIZE));
  SPDLOG_DEBUG("UbershaderProvider MaterialsCount: {}",
               materialProvider_->getMaterialsCount());

  AssetConfiguration assetConfiguration{};
  assetConfiguration.engine = engine;
  assetConfiguration.materials = materialProvider_;
  assetLoader_ = AssetLoader::create(assetConfiguration);

  ResourceConfiguration resourceConfiguration{};
  resourceConfiguration.engine = engine;
  resourceConfiguration.normalizeSkinningWeights = true;
  resourceLoader_ = new ResourceLoader(resourceConfiguration);

  auto decoder = filament::gltfio::createStbProvider(engine);
  resourceLoader_->addTextureProvider("image/png", decoder);
  resourceLoader_->addTextureProvider("image/jpeg", decoder);

  SPDLOG_TRACE("--ModelLoader::ModelLoader");
}

ModelLoader::~ModelLoader() {
  destroyAllModels();
  delete resourceLoader_;
  resourceLoader_ = nullptr;

  if (assetLoader_) {
    AssetLoader::destroy(&assetLoader_);
  }
}

void ModelLoader::destroyAllModels() {
  for (auto* asset : assets_) {
    destroyModel(asset);
  }
  assets_.clear();
}

void ModelLoader::destroyModel(filament::gltfio::FilamentAsset* asset) {
  if (!asset) {
    return;
  }

  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  modelViewer->getFilamentScene()->removeEntities(asset->getEntities(),
                                                   asset->getEntityCount());
  assetLoader_->destroyAsset(asset);
}

filament::gltfio::FilamentAsset* ModelLoader::poFindAssetByName(const std::string& szName) 
{
  // To be implemented with m_mapszpoAssets
  return assets_[0];
}

void ModelLoader::loadModelGlb(const std::vector<uint8_t>& buffer,
                               const ::filament::float3* centerPosition,
                               float scale,
                               std::string assetName,
                               bool /*autoScaleEnabled*/) {
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  
  auto* asset = assetLoader_->createAsset(buffer.data(),
                                     static_cast<uint32_t>(buffer.size()));
  if (!asset) {
    // TODO THROW ERROR
    return;
  }

  assets_.push_back(asset);
  resourceLoader_->asyncBeginLoad(asset);
  modelViewer->setAnimator(asset->getInstance()->getAnimator());

  // TODO Append names or types / change list, something 
  // for loading the same model IN.
  // m_mapszpoAssets

  asset->releaseSourceData();


  // TODO is this needed?
  // if (autoScaleEnabled) {
  //   transformToUnitCube(asset, centerPosition, scale);
  // } else {
  //   clearRootTransform(asset);
  // }

  clearRootTransform(asset);
  SPDLOG_DEBUG("glb {}", scale);

  ::filament::Engine* engine = modelViewer->getFilamentEngine();

  auto& tm = engine->getTransformManager();
  auto ei = tm.getInstance(asset->getRoot());

  tm.setTransform(ei, ::filament::math::mat4f{ ::filament::math::mat3f(scale), *centerPosition } *
            tm.getWorldTransform(ei));
}

void ModelLoader::loadModelGltf(
    const std::vector<uint8_t>& buffer,
    const ::filament::float3* centerPosition,
    float scale,
    std::function<const ::filament::backend::BufferDescriptor&(
        std::string uri)>& /* callback */,
    bool transform) {
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);

  auto* asset = assetLoader_->createAsset(buffer.data(),
                                     static_cast<uint32_t>(buffer.size()));
  if (!asset) {
    // TODO THROW ERROR
    return;
  }

  assets_.push_back(asset);

  auto uri_data = asset->getResourceUris();
  auto uris = std::vector<const char*>(
      uri_data, uri_data + asset->getResourceUriCount());
  for (const auto uri : uris) {
    SPDLOG_DEBUG("resource uri: {}", uri);
#if 0   // TODO
              auto resourceBuffer = callback(uri);
              if (!resourceBuffer) {
                  this->asset_ = nullptr;
                  return;
              }
              resourceLoader_->addResourceData(uri, resourceBuffer);
#endif  // TODO
  }
  resourceLoader_->asyncBeginLoad(asset);
  modelViewer->setAnimator(asset->getInstance()->getAnimator());
  asset->releaseSourceData();
  if (transform) {
    transformToUnitCube(asset, centerPosition, scale);
  }
}

void ModelLoader::transformToUnitCube(
    filament::gltfio::FilamentAsset* asset,
    const ::filament::float3* /* centerPoint */,
    float /* modelScale */) {
  if (!asset) {
    return;
  }
  auto aabb = asset->getBoundingBox();
  auto center = aabb.center();
  auto halfExtent = aabb.extent();
  auto maxExtent = max(halfExtent) * 2;
  auto scaleFactor = 2.0f / maxExtent;
  auto transform = ::filament::math::mat4f::scaling(scaleFactor) *
                   ::filament::math::mat4f::translation(-center);

  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  ::filament::Engine* engine = modelViewer->getFilamentEngine();

  auto& tm = engine->getTransformManager();
  tm.setTransform(tm.getInstance(asset->getRoot()), transform);
}

void ModelLoader::populateScene(::filament::gltfio::FilamentAsset* asset) {
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  ::filament::Engine* engine = modelViewer->getFilamentEngine();

  auto& rcm = engine->getRenderableManager();

  size_t count = asset->popRenderables(nullptr, 0);
  while (count) {
    asset->popRenderables(readyRenderables_, count);
    for (int i = 0; i < count; i++) {
      auto ri = rcm.getInstance(readyRenderables_[i]);
      // TODO move to setting
      rcm.setScreenSpaceContactShadows(ri, false);
    }
    modelViewer->getFilamentScene()->addEntities(readyRenderables_, count);
    count = asset->popRenderables(nullptr, 0);
  }
  auto lightEntities = asset->getLightEntities();
  if (lightEntities) {
    modelViewer->getFilamentScene()->addEntities(asset->getLightEntities(),
                                                  sizeof(*lightEntities));
  }
}

void ModelLoader::updateScene() {
  resourceLoader_->asyncUpdateLoad();

  for (auto* asset : assets_) {
    populateScene(asset);
  }
}

void ModelLoader::removeAsset(filament::gltfio::FilamentAsset* asset) {
  if (!asset) {
    return;
  }

  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  modelViewer->getFilamentScene()->removeEntities(asset->getEntities(),
                                                   asset->getEntityCount());
  asset = nullptr;
}

std::optional<::filament::math::mat4f> ModelLoader::getModelTransform(filament::gltfio::FilamentAsset* asset) {
  if (asset) {
    auto root = asset->getRoot();
    auto& tm = asset->getEngine()->getTransformManager();
    auto instance = tm.getInstance(root);
    return tm.getTransform(instance);
  }
  return std::nullopt;
}

void ModelLoader::clearRootTransform(filament::gltfio::FilamentAsset* asset) {
  if (!asset) {
    return;
  }

  auto root = asset->getRoot();
  auto& tm = asset->getEngine()->getTransformManager();
  auto instance = tm.getInstance(root);
  tm.setTransform(instance, ::filament::mat4f{});
}

std::future<Resource<std::string_view>> ModelLoader::loadGlbFromAsset(
    const std::string& path,
    float scale,
    const ::filament::math::float3* centerPosition,
    bool isFallback) {
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto promise_future(promise->get_future());
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  modelViewer->setModelState(ModelState::LOADING);

  try
  {
    const asio::io_context::strand& strand_(modelViewer->getStrandContext());
    const std::string assetPath = modelViewer->getAssetPath();

    asio::post(strand_, [&, promise, path, scale, centerPosition, isFallback, assetPath] {
      try
      {
        auto buffer = readBinaryFile(path, assetPath);
        handleFile(buffer, path, scale, centerPosition, isFallback, promise);
      }
      catch(const std::exception& e)
      {
        std::cerr << "Lambda Exception " << e.what() << '\n';
        promise->set_exception(std::make_exception_ptr(e));
      }
      catch(...)
      {
        std::cerr << "Unknown Exception in lambda" << '\n';
      }
    });
  }
  catch(const std::exception& e)
  {
    std::cerr << "Total Exception: " << e.what() << '\n';
    promise->set_exception(std::make_exception_ptr(e));
  }
  return promise_future;

}

std::future<Resource<std::string_view>> ModelLoader::loadGlbFromUrl(
    std::string url,
    float scale,
    const ::filament::math::float3* centerPosition,
    bool isFallback) {
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto promise_future(promise->get_future());
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  modelViewer->setModelState(ModelState::LOADING);
  asio::post(modelViewer->getStrandContext(), [&, promise, url = std::move(url), scale, centerPosition,
                       isFallback] {
    plugin_common_curl::CurlClient client;
    auto buffer = client.RetrieveContentAsVector();
    if (client.GetCode() != CURLE_OK) {
      modelViewer->setModelState(ModelState::ERROR);
      promise->set_value(
          Resource<std::string_view>::Error("Couldn't load Glb from " + url));
    }
    handleFile(buffer, url, scale, centerPosition, isFallback, promise);
  });
  return promise_future;
}

void ModelLoader::handleFile(
    const std::vector<uint8_t>& buffer,
    const std::string& fileSource,
    float scale,
    const ::filament::math::float3* centerPosition,
    bool isFallback,
    const std::shared_ptr<std::promise<Resource<std::string_view>>>& promise) {

  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  if (!buffer.empty()) {
    loadModelGlb(buffer, centerPosition, scale,fileSource,  true);
    modelViewer->setModelState(isFallback ? ModelState::FALLBACK_LOADED
                                           : ModelState::LOADED);
    promise->set_value(Resource<std::string_view>::Success(
        "Loaded glb model successfully from " + fileSource));
  } else {
    modelViewer->setModelState(ModelState::ERROR);
    promise->set_value(Resource<std::string_view>::Error(
        "Couldn't load glb model from " + fileSource));
  }
}

std::future<Resource<std::string_view>> ModelLoader::loadGltfFromAsset(
    const std::string& /* path */,
    const std::string& /* pre_path */,
    const std::string& /* post_path */,
    float /* scale */,
    const ::filament::math::float3* /* centerPosition */,
    bool /* isFallback */) {
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto future(promise->get_future());
  promise->set_value(Resource<std::string_view>::Error("Not implemented yet"));
  return future;
}

std::future<Resource<std::string_view>> ModelLoader::loadGltfFromUrl(
    const std::string& /* url */,
    float /* scale */,
    const ::filament::math::float3* /* centerPosition */,
    bool /* isFallback */) {
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto future(promise->get_future());
  promise->set_value(Resource<std::string_view>::Error("Not implemented yet"));
  return future;
}

}  // namespace plugin_filament_view
