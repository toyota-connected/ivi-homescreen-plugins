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

#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"

#include <core/components/base/component.h>
#include <core/include/resource.h>
#include <core/scene/material/material_parameter.h>
#include <core/utils/filament_types.h>

#include <filament/MaterialInstance.h>

#include <map>
#include <memory>

namespace plugin_filament_view {

class BaseShape;
class Model;
class MaterialSystem;

class Material : public Component {
    friend class BaseShape;
    friend class Model;
    friend class MaterialSystem;

  protected:
    std::string assetPath_;
    std::string url_;

    MaterialDefinition* _matdef = nullptr;
    std::shared_ptr<MaterialInstance> _instance;

    /// Queue of material parameters to be applied
    std::vector<std::shared_ptr<MaterialParameter>> _tmpParams;

  public:
    explicit Material(const flutter::EncodableMap& params);

    Material(const Material& other)
      : Component(std::string(__FUNCTION__)),
        assetPath_(other.assetPath_),
        url_(other.url_) {
      for (const auto& param : other._tmpParams) {
        if (param) {
          _tmpParams.emplace_back(param);
        }
      }
    }

    Material(
      const std::string& assetPath,
      const std::vector<std::shared_ptr<MaterialParameter>>& parameters
    )
      : Component(std::string(__FUNCTION__)),
        assetPath_(assetPath),
        _tmpParams(parameters) {}

    ~Material() override;

    /// @brief Set a property on the material instance
    /// Under the hood: adds the parameter to a list, which is picked up by the MaterialSystem
    /// on the next iteration (could be the same frame but it depends on when it's called in the
    /// lifecycle)
    void setInstanceProperty(std::shared_ptr<MaterialParameter> param);

    // this will either get the assetPath or the url, priority of assetPath
    // looking for which is valid. Used to see if we have this loaded in cache.
    [[nodiscard]] const std::string* getLookupName() const;

    // This will go through each of the parameters and return only the
    // texture_(definitions) so the material manager can load what's not already
    // loaded.
    [[nodiscard]] std::vector<MaterialParameter*> getTextureMaterialParameters() const;

    [[nodiscard]] inline std::string szGetMaterialAssetPath() const { return assetPath_; }
    [[nodiscard]] inline std::string szGetMaterialURLPath() const { return url_; }

    void debugPrint(const std::string& tabPrefix) const override;

    [[nodiscard]] inline Component* Clone() const override {
      return new Material(*this);  // Copy constructor is called here
    }
};

const std::shared_ptr<MaterialParameter> kDefaultBaseColor = std::make_shared<MaterialParameter>(
  "baseColor",
  MaterialParameter::MaterialType::COLOR,
  filament::math::float4(1.0f, 1.0f, 1.0f, 1.0f)
);
const std::shared_ptr<MaterialParameter> kDefaultRoughness = std::make_shared<MaterialParameter>(
  "roughness",  //
  MaterialParameter::MaterialType::FLOAT,
  0.5f
);
const std::shared_ptr<MaterialParameter> kDefaultMetallic = std::make_shared<MaterialParameter>(
  "metallic",  //
  MaterialParameter::MaterialType::FLOAT,
  0.0f
);

const std::vector<std::shared_ptr<MaterialParameter>> kDefaultMaterialParameters{
  kDefaultBaseColor,
  kDefaultRoughness,
  kDefaultMetallic,
};

const Material kDefaultMaterial = Material(
  "assets/materials/lit.filamat",  //
  kDefaultMaterialParameters       //
);

}  // namespace plugin_filament_view
