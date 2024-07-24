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
                    std::string assetName,
                    bool transformToUnitCube = false);

  void loadModelGltf(const std::vector<uint8_t>& buffer,
                     const ::filament::float3* centerPosition,
                     float scale,
                     std::function<const ::filament::backend::BufferDescriptor&(
                         std::string uri)>& callback,
                     bool transform = false);

  std::vector<filament::gltfio::FilamentAsset*> getAssets() const { return assets_; };

  filament::gltfio::FilamentAsset* poFindAssetByName(const std::string& szName);

  std::optional<::filament::math::mat4f> getModelTransform(filament::gltfio::FilamentAsset* asset);

  void clearRootTransform(filament::gltfio::FilamentAsset* asset);

  void transformToUnitCube(filament::gltfio::FilamentAsset* asset, const ::filament::float3* centerPoint, float scale);

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

  std::future<Resource<std::string_view>> loadGltfFromAsset(
      const std::string& path,
      const std::string& pre_path,
      const std::string& post_path,
      float scale,
      const ::filament::float3* centerPosition,
      bool isFallback = false);

  std::future<Resource<std::string_view>> loadGltfFromUrl(
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

  void fetchResources(
      ::filament::gltfio::FilamentAsset* asset,
      const std::function<uint8_t*(std::string asset)> callback);

  ::filament::math::mat4f inline fitIntoUnitCube(
      const ::filament::Aabb& bounds,
      ::filament::math::float3 offset);

  void updateRootTransform(filament::gltfio::FilamentAsset* asset, bool autoScaleEnabled);

  void populateScene(::filament::gltfio::FilamentAsset* asset);

  bool isRemoteMode() const { return assets_.empty(); }

  void removeAsset(filament::gltfio::FilamentAsset* asset);

  ::filament::mat4f getTransform(filament::gltfio::FilamentAsset* asset);

  void setTransform(filament::gltfio::FilamentAsset* asset, ::filament::mat4f mat);

  std::vector<char> buffer_;
  void handleFile(
      const std::vector<uint8_t>& buffer,
      const std::string& fileSource,
      float scale,
      const ::filament::float3* centerPosition,
      bool isFallback,
      const std::shared_ptr<std::promise<Resource<std::string_view>>>& promise);
};
}  // namespace plugin_filament_view
