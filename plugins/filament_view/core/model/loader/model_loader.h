/*
 * Copyright 2020-2023 Toyota Connected North America
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

#include <filament/IndirectLight.h>
#include <filament/MaterialInstance.h>
#include <filament/TransformManager.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/ResourceLoader.h>
#include <asio/io_context_strand.hpp>

#include "core/include/resource.h"
#include "core/model/model.h"
#include "viewer/custom_model_viewer.h"
#include "viewer/settings.h"

namespace plugin_filament_view {

class CustomModelViewer;

class Model;

class ModelLoader {
 public:
  ModelLoader();

  ~ModelLoader();

  void destroyAllModels();

  void destroyModel(filament::gltfio::FilamentAsset* asset);

  void loadModelGlb(const std::vector<uint8_t>& buffer,
                    const ::filament::float3* centerPosition,
                    float scale,
                    const std::string& assetName,
                    bool transformToUnitCube = false);

  void loadModelGltf(const std::vector<uint8_t>& buffer,
                     const ::filament::float3* centerPosition,
                     float scale,
                     std::function<const ::filament::backend::BufferDescriptor&(
                         std::string uri)>& callback,
                     bool transform = false);

  [[nodiscard]] std::vector<filament::gltfio::FilamentAsset*> getAssets()
      const {
    return assets_;
  };

  filament::gltfio::FilamentAsset* poFindAssetByName(const std::string& szName);

  [[nodiscard]] static std::optional<::filament::math::mat4f> getModelTransform(
      filament::gltfio::FilamentAsset* asset);

  static void clearRootTransform(filament::gltfio::FilamentAsset* asset);

  static void transformToUnitCube(filament::gltfio::FilamentAsset* asset,
                                  const ::filament::float3* centerPoint,
                                  float scale);

  void updateScene();

  std::future<Resource<std::string_view>> loadGlbFromAsset(
      const std::string& path,
      float scale,
      const ::filament::float3* centerPosition,
      bool isFallback = false);

  std::future<Resource<std::string_view>> loadGlbFromUrl(
      std::string url,
      float scale,
      const ::filament::float3* centerPosition,
      bool isFallback = false);

  static std::future<Resource<std::string_view>> loadGltfFromAsset(
      const std::string& path,
      const std::string& pre_path,
      const std::string& post_path,
      float scale,
      const ::filament::float3* centerPosition,
      bool isFallback = false);

  static std::future<Resource<std::string_view>> loadGltfFromUrl(
      const std::string& url,
      float scale,
      const ::filament::float3* centerPosition,
      bool isFallback = false);

  friend class CustomModelViewer;

 private:
  std::vector<filament::gltfio::FilamentInstance*> instances_;

  utils::Entity sunlight_;
  ::filament::gltfio::AssetLoader* assetLoader_;
  ::filament::gltfio::MaterialProvider* materialProvider_;
  ::filament::gltfio::ResourceLoader* resourceLoader_;

  std::vector<filament::gltfio::FilamentAsset*> assets_;

  ::filament::IndirectLight* indirectLight_ = nullptr;

  utils::Entity readyRenderables_[128];

  // Todo implement for ease of use of finding assets by tag quickly.
  // std::map<std::string, filament::gltfio::FilamentInstance*> m_mapszpoAssets;

  ::filament::viewer::Settings settings_;
  std::vector<float> morphWeights_;

  ::filament::math::mat4f inline fitIntoUnitCube(
      const ::filament::Aabb& bounds,
      ::filament::math::float3 offset);

  void updateRootTransform(filament::gltfio::FilamentAsset* asset,
                           bool autoScaleEnabled);

  void populateScene(::filament::gltfio::FilamentAsset* asset);

  [[nodiscard]] bool isRemoteMode() const { return assets_.empty(); }

  static void removeAsset(filament::gltfio::FilamentAsset* asset);

  [[nodiscard]] ::filament::mat4f getTransform(
      filament::gltfio::FilamentAsset* asset);

  void setTransform(filament::gltfio::FilamentAsset* asset,
                    ::filament::mat4f mat);

  using PromisePtr = std::shared_ptr<std::promise<Resource<std::string_view>>>;
  void handleFile(
      const std::vector<uint8_t>& buffer,
      const std::string& fileSource,
      float scale,
      const ::filament::float3* centerPosition,
      bool isFallback,
      const PromisePtr&
          promise);  // NOLINT(readability-avoid-const-params-in-decls)
};
}  // namespace plugin_filament_view
