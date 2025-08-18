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

#include <core/components/derived/animation.h>
#include <core/components/derived/commonrenderable.h>
#include <core/components/derived/transform.h>
#include <core/entity/base/entityobject.h>
#include <core/entity/derived/renderable_entityobject.h>
#include <gltfio/FilamentAsset.h>
#include <string>

namespace plugin_filament_view {

enum class ModelInstancingMode {
  none = 0,
  primary = 1,
  secondary = 2,
};

const char* modelInstancingModeToString(ModelInstancingMode mode);

class Model : public RenderableEntityObject {
    friend class ModelSystem;

  public:
    Model();
    ~Model() override = default;

    /// @brief Static deserializer - calls the constructor and deserializeFrom
    /// under the hood
    static std::shared_ptr<Model> Deserialize(const flutter::EncodableMap& params);

    // Disallow copy and assign.
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    void setAsset(filament::gltfio::FilamentAsset* poAsset) { m_poAsset = poAsset; }

    void setAssetInstance(filament::gltfio::FilamentInstance* poAssetInstance) {
      m_poAssetInstance = poAssetInstance;
    }

    [[nodiscard]] filament::gltfio::FilamentAsset* getAsset() const { return m_poAsset; }

    [[nodiscard]] filament::gltfio::FilamentInstance* getAssetInstance() const {
      return m_poAssetInstance;
    }

    [[nodiscard]] std::shared_ptr<Transform> getTransform() const {
      return getComponent<Transform>();
    }
    [[nodiscard]] std::shared_ptr<CommonRenderable> getCommonRenderable() const {
      return getComponent<CommonRenderable>();
    }

    [[nodiscard]] std::string getAssetPath() const { return assetPath_; }

    /// @returns model's instancing mode
    [[nodiscard]] ModelInstancingMode getInstancingMode() const { return _instancingMode; }

    /// Returns whether the model is in the scene
    [[nodiscard]] bool isInScene() const { return m_isInScene; }

    [[nodiscard]] virtual AABB getAABB() const override;

  protected:
    std::string assetPath_;

    filament::gltfio::FilamentAsset* m_poAsset;
    filament::gltfio::FilamentInstance* m_poAssetInstance;
    std::map<FilamentEntity, EntityGUID> _childrenEntities;

    ModelInstancingMode _instancingMode = ModelInstancingMode::none;
    /// Whether it's been inserted into the scene
    bool m_isInScene = false;

    void debugPrint() const override;

    virtual void deserializeFrom(const flutter::EncodableMap& params) override;
};

}  // namespace plugin_filament_view
