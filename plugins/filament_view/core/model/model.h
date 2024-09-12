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

#include <core/components/commonrenderable.h>
#include <string>

#include <gltfio/FilamentAsset.h>
#include "core/components/basetransform.h"
#include "core/model/animation/animation.h"

#include "core/scene/entity/entityobject.h"

namespace plugin_filament_view {

class Animation;

class Model : public EntityObject {
 public:
  Model(std::string assetPath,
        std::string url,
        Model* fallback,
        Animation* animation,
        BaseTransform* poTransform,
        CommonRenderable* poCommonRenderable);

  virtual ~Model() = default;

  static std::unique_ptr<Model> Deserialize(
      const std::string& flutterAssetsPath,
      const flutter::EncodableMap& params);

  [[nodiscard]] Model* GetFallback() const { return fallback_; }

  [[nodiscard]] Animation* GetAnimation() const { return animation_; }

  // Disallow copy and assign.
  Model(const Model&) = delete;
  Model& operator=(const Model&) = delete;

  void setAsset(filament::gltfio::FilamentAsset* poAsset) {
    m_poAsset = poAsset;
  }

  filament::gltfio::FilamentAsset* getAsset() const { return m_poAsset; }

  const BaseTransform* GetBaseTransform() const { return m_poBaseTransform; }
  const CommonRenderable* GetCommonRenderable() const {
    return m_poCommonRenderable;
  }

 protected:
  std::string assetPath_;
  std::string url_;
  Model* fallback_;
  Animation* animation_;

  filament::gltfio::FilamentAsset* m_poAsset;

  void DebugPrint() const override;

  // Components - saved off here for faster
  // lookup, but they're not owned here, but on EntityObject's list.
  // Todo change to shared_ptrs
  BaseTransform* m_poBaseTransform;
  CommonRenderable* m_poCommonRenderable;
};

class GlbModel final : public Model {
 public:
  GlbModel(std::string assetPath,
           std::string url,
           Model* fallback,
           Animation* animation,
           BaseTransform* poTransform,
           CommonRenderable* poCommonRenderable);

  ~GlbModel() override = default;

  friend class ModelLoader;

  friend class SceneController;
};

class GltfModel final : public Model {
 public:
  GltfModel(std::string assetPath,
            std::string url,
            std::string pathPrefix,
            std::string pathPostfix,
            Model* fallback,
            Animation* animation,
            BaseTransform* poTransform,
            CommonRenderable* poCommonRenderable);

  ~GltfModel() override = default;

  friend class ModelLoader;

  friend class SceneController;

 private:
  std::string pathPrefix_;
  std::string pathPostfix_;
};

}  // namespace plugin_filament_view
