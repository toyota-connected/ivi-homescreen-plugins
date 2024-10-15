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
#pragma once

#include <future>
#include <asio/io_context_strand.hpp>
#include <core/entity/derived/model/model.h>
#include <core/include/resource.h>
#include <core/include/settings.h>
#include <core/systems/base/ecsystem.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/ResourceLoader.h>

namespace plugin_filament_view {

class Model;

class ModelSystem : public ECSystem {
 public:
  ModelSystem() = default;

  void destroyAllAssetsOnModels();
  void destroyAsset(filament::gltfio::FilamentAsset* asset);

  void loadModelGlb(Model* poOurModel,
                    const std::vector<uint8_t>& buffer,
                    const std::string& assetName);

  void loadModelGltf(Model* poOurModel,
                     const std::vector<uint8_t>& buffer,
                     std::function<const ::filament::backend::BufferDescriptor&(
                         std::string uri)>& callback);

  filament::gltfio::FilamentAsset* poFindAssetByGuid(const std::string& szName);

  void updateAsyncAssetLoading();

  std::future<Resource<std::string_view>> loadGlbFromAsset(
      Model* poOurModel,
      const std::string& path,
      bool isFallback = false);

  std::future<Resource<std::string_view>>
  loadGlbFromUrl(Model* poOurModel, std::string url, bool isFallback = false);

  static std::future<Resource<std::string_view>> loadGltfFromAsset(
      Model* poOurModel,
      const std::string& path,
      const std::string& pre_path,
      const std::string& post_path,
      bool isFallback = false);

  static std::future<Resource<std::string_view>> loadGltfFromUrl(
      Model* poOurModel,
      const std::string& url,
      bool isFallback = false);

  void vInitSystem() override;
  void vUpdate(float fElapsedTime) override;
  void vShutdownSystem() override;
  void DebugPrint() override;

  [[nodiscard]] size_t GetTypeID() const override { return StaticGetTypeID(); }

  [[nodiscard]] static size_t StaticGetTypeID() {
    return typeid(ModelSystem).hash_code();
  }

 private:
  // sunlight_ needs to be moved, no reason to be on this class
  utils::Entity sunlight_;
  ::filament::gltfio::AssetLoader* assetLoader_{};
  ::filament::gltfio::MaterialProvider* materialProvider_{};
  ::filament::gltfio::ResourceLoader* resourceLoader_{};

  // This is the EntityObject guids to model instantiated.
  std::map<EntityGUID, Model*> m_mapszpoAssets;

  // This will be needed for a list of prefab instances to load from
  // std::map<Model*> <name>models_;

  // This is a reusable list of renderables for popping off
  // async load.
  // NOTE If you change this size; the async update loop on the system count
  // needs to change.
  utils::Entity readyRenderables_[128];

  // Todo, this needs to be moved; if its not initialized, undefined <results>
  ::filament::viewer::Settings settings_;

  // not actively used, to be moved
  std::vector<float> morphWeights_;

  void populateSceneWithAsyncLoadedAssets(Model* model);

  using PromisePtr = std::shared_ptr<std::promise<Resource<std::string_view>>>;
  void handleFile(
      Model* poOurModel,
      const std::vector<uint8_t>& buffer,
      const std::string& fileSource,
      bool isFallback,
      const PromisePtr&
          promise);  // NOLINT(readability-avoid-const-params-in-decls)
};
}  // namespace plugin_filament_view
