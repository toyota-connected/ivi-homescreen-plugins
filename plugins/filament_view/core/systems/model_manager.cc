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
#include "model_manager.h"

#include <algorithm>  // for max
#include <sstream>

#include <core/components/collidable.h>
#include <filament/filament/DebugRegistry.h>
#include <filament/filament/RenderableManager.h>
#include <filament/filament/TransformManager.h>
#include <filament/gltfio/ResourceLoader.h>
#include <filament/gltfio/TextureProvider.h>
#include <filament/math/mat4.h>
#include <filament/utils/Slice.h>
#include <asio/post.hpp>
#include "core/utils/entitytransforms.h"

#include "collision_manager.h"
#include "filament/gltfio/materials/uberarchive.h"

#include <curl_client/curl_client.h>
#include "core/include/file_utils.h"

namespace plugin_filament_view {

using ::filament::gltfio::AssetConfiguration;
using ::filament::gltfio::AssetLoader;
using ::filament::gltfio::ResourceConfiguration;
using ::filament::gltfio::ResourceLoader;

////////////////////////////////////////////////////////////////////////////////////
ModelManager::ModelManager() {
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

////////////////////////////////////////////////////////////////////////////////////
ModelManager::~ModelManager() {
  destroyAllAssetsOnModels();
  delete resourceLoader_;
  resourceLoader_ = nullptr;

  if (assetLoader_) {
    AssetLoader::destroy(&assetLoader_);
    assetLoader_ = nullptr;
  }
}

////////////////////////////////////////////////////////////////////////////////////
void ModelManager::destroyAllAssetsOnModels() {
  for (const auto& model : m_mapszpoAssets) {
    destroyAsset(model.second->getAsset());
    delete model.second;
  }
  m_mapszpoAssets.clear();
}

////////////////////////////////////////////////////////////////////////////////////
void ModelManager::destroyAsset(filament::gltfio::FilamentAsset* asset) {
  if (!asset) {
    return;
  }

  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  modelViewer->getFilamentScene()->removeEntities(asset->getEntities(),
                                                  asset->getEntityCount());
  assetLoader_->destroyAsset(asset);
}

////////////////////////////////////////////////////////////////////////////////////
filament::gltfio::FilamentAsset* ModelManager::poFindAssetByGuid(
    const std::string& szGUID) {
  auto iter = m_mapszpoAssets.find(szGUID);
  if (iter == m_mapszpoAssets.end()) {
    return nullptr;
  }

  return iter->second->getAsset();
}

////////////////////////////////////////////////////////////////////////////////////
void ModelManager::loadModelGlb(Model* poOurModel,
                                const std::vector<uint8_t>& buffer,
                                const std::string& /*assetName*/) {
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);

  auto* asset = assetLoader_->createAsset(buffer.data(),
                                          static_cast<uint32_t>(buffer.size()));
  if (!asset) {
    spdlog::error("Failed to loadModelGlb->createasset from buffered data.");
    return;
  }

  resourceLoader_->asyncBeginLoad(asset);

  // This will move to be on the model itself.
  modelViewer->setAnimator(asset->getInstance()->getAnimator());

  // NOTE if this is a prefab/instance you will NOT Want to do this.
  asset->releaseSourceData();

  ::filament::Engine* engine = modelViewer->getFilamentEngine();
  auto& rcm = engine->getRenderableManager();

  utils::Slice<Entity> const listOfRenderables{
      asset->getRenderableEntities(), asset->getRenderableEntityCount()};

  for (auto entity : listOfRenderables) {
    auto ri = rcm.getInstance(entity);
    rcm.setCastShadows(
        ri, poOurModel->GetCommonRenderable()->IsCastShadowsEnabled());
    rcm.setReceiveShadows(
        ri, poOurModel->GetCommonRenderable()->IsReceiveShadowsEnabled());
    // Investigate this more before making it a property on common renderable
    // component.
    rcm.setScreenSpaceContactShadows(ri, false);
  }

  poOurModel->setAsset(asset);
  m_mapszpoAssets.insert(std::pair(poOurModel->GetGlobalGuid(), poOurModel));
}

////////////////////////////////////////////////////////////////////////////////////
void ModelManager::loadModelGltf(
    Model* poOurModel,
    const std::vector<uint8_t>& buffer,
    std::function<const ::filament::backend::BufferDescriptor&(
        std::string uri)>& /* callback */) {
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);

  auto* asset = assetLoader_->createAsset(buffer.data(),
                                          static_cast<uint32_t>(buffer.size()));
  if (!asset) {
    spdlog::error("Failed to loadModelGltf->createasset from buffered data.");
    return;
  }

  auto uri_data = asset->getResourceUris();
  auto uris = std::vector(uri_data, uri_data + asset->getResourceUriCount());
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

  ::filament::Engine* engine = modelViewer->getFilamentEngine();
  auto& rcm = engine->getRenderableManager();

  utils::Slice<Entity> const listOfRenderables{
      asset->getRenderableEntities(), asset->getRenderableEntityCount()};

  for (auto entity : listOfRenderables) {
    auto ri = rcm.getInstance(entity);
    rcm.setCastShadows(
        ri, poOurModel->GetCommonRenderable()->IsCastShadowsEnabled());
    rcm.setReceiveShadows(
        ri, poOurModel->GetCommonRenderable()->IsReceiveShadowsEnabled());
    // Investigate this more before making it a property on common renderable
    // component.
    rcm.setScreenSpaceContactShadows(ri, false);
  }

  poOurModel->setAsset(asset);
  m_mapszpoAssets.insert(std::pair(poOurModel->GetGlobalGuid(), poOurModel));
}

////////////////////////////////////////////////////////////////////////////////////
void ModelManager::populateSceneWithAsyncLoadedAssets(Model* model) {
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);

  auto& rcm = modelViewer->getFilamentEngine()->getRenderableManager();

  auto* asset = model->getAsset();
  size_t count = asset->popRenderables(nullptr, 0);
  while (count) {
    asset->popRenderables(readyRenderables_, count);

    utils::Slice<Entity> const listOfRenderables{
        asset->getRenderableEntities(), asset->getRenderableEntityCount()};

    for (auto entity : listOfRenderables) {
      auto ri = rcm.getInstance(entity);
      rcm.setCastShadows(ri,
                         model->GetCommonRenderable()->IsCastShadowsEnabled());
      rcm.setReceiveShadows(
          ri, model->GetCommonRenderable()->IsReceiveShadowsEnabled());
      // Investigate this more before making it a property on common renderable
      // component.
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

////////////////////////////////////////////////////////////////////////////////////
void ModelManager::updateAsyncAssetLoading() {
  resourceLoader_->asyncUpdateLoad();

  // This does not specify per resource, but a global, best we can do with this
  // information is if we're done loading <everything> that was marked as async
  // load, then load that physics data onto a collidable if required. This gives
  // us visuals without collidbales in a scene with <tons> of objects; but would
  // eventually settle
  float percentComplete = resourceLoader_->asyncGetLoadProgress();

  for (const auto& asset : m_mapszpoAssets) {
    populateSceneWithAsyncLoadedAssets(asset.second);

    if (percentComplete != 1.0f) {
      continue;
    }

    // if its 'done' loading, we need to create our large AABB collision
    // object if this model it's referencing required one.
    //
    // Also need to make sure it hasn't already created one for this model.
    if (asset.second->HasComponentByStaticTypeID(
            Collidable::StaticGetTypeID()) &&
        !CollisionManager::Instance()->bHasEntityObjectRepresentation(
            asset.first)) {
      CollisionManager::Instance()->vAddCollidable(asset.second);
    }  // end make collision
  }  // end foreach
}  // end method

////////////////////////////////////////////////////////////////////////////////////
std::future<Resource<std::string_view>> ModelManager::loadGlbFromAsset(
    Model* poOurModel,
    const std::string& path,
    bool isFallback) {
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto promise_future(promise->get_future());
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  modelViewer->setModelState(ModelState::LOADING);

  try {
    const asio::io_context::strand& strand_(modelViewer->getStrandContext());
    const std::string assetPath = modelViewer->getAssetPath();

    asio::post(strand_, [&, poOurModel, promise, path, isFallback, assetPath] {
      try {
        auto buffer = readBinaryFile(path, assetPath);
        handleFile(poOurModel, buffer, path, isFallback, promise);
      } catch (const std::exception& e) {
        std::cerr << "Lambda Exception " << e.what() << '\n';
        promise->set_exception(std::make_exception_ptr(e));
      } catch (...) {
        std::cerr << "Unknown Exception in lambda" << '\n';
      }
    });
  } catch (const std::exception& e) {
    std::cerr << "Total Exception: " << e.what() << '\n';
    promise->set_exception(std::make_exception_ptr(e));
  }
  return promise_future;
}

////////////////////////////////////////////////////////////////////////////////////
std::future<Resource<std::string_view>> ModelManager::loadGlbFromUrl(
    Model* poOurModel,
    std::string url,
    bool isFallback) {
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto promise_future(promise->get_future());
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  modelViewer->setModelState(ModelState::LOADING);
  asio::post(modelViewer->getStrandContext(),
             [&, poOurModel, promise, url = std::move(url), isFallback] {
               plugin_common_curl::CurlClient client;
               auto buffer = client.RetrieveContentAsVector();
               if (client.GetCode() != CURLE_OK) {
                 modelViewer->setModelState(ModelState::ERROR);
                 promise->set_value(Resource<std::string_view>::Error(
                     "Couldn't load Glb from " + url));
               }
               handleFile(poOurModel, buffer, url, isFallback, promise);
             });
  return promise_future;
}

////////////////////////////////////////////////////////////////////////////////////
void ModelManager::handleFile(Model* poOurModel,
                              const std::vector<uint8_t>& buffer,
                              const std::string& fileSource,
                              bool isFallback,
                              const PromisePtr& promise) {
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  if (!buffer.empty()) {
    loadModelGlb(poOurModel, buffer, fileSource);
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

////////////////////////////////////////////////////////////////////////////////////
std::future<Resource<std::string_view>> ModelManager::loadGltfFromAsset(
    Model* /*poOurModel*/,
    const std::string& /* path */,
    const std::string& /* pre_path */,
    const std::string& /* post_path */,
    bool /* isFallback */) {
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto future(promise->get_future());
  promise->set_value(Resource<std::string_view>::Error("Not implemented yet"));
  return future;
}

////////////////////////////////////////////////////////////////////////////////////
std::future<Resource<std::string_view>> ModelManager::loadGltfFromUrl(
    Model* /*poOurModel*/,
    const std::string& /* url */,
    bool /* isFallback */) {
  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto future(promise->get_future());
  promise->set_value(Resource<std::string_view>::Error("Not implemented yet"));
  return future;
}

}  // namespace plugin_filament_view
