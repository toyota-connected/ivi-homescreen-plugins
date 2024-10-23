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

#include <core/scene/material/material_parameter.h>
#include <core/components/base/component.h>
#include <core/include/resource.h>
#include <filament/MaterialInstance.h>
#include <map>
#include <memory>

using TextureMap = std::map<std::string, Resource<::filament::Texture*>>;

namespace plugin_filament_view {

class MaterialDefinitions : public Component {
 public:
  explicit MaterialDefinitions(const flutter::EncodableMap& params);

 MaterialDefinitions(const MaterialDefinitions& other)
  : Component(std::string(__FUNCTION__)), assetPath_(other.assetPath_), url_(other.url_) {

  for (const auto& [key, value] : other.parameters_) {
   if (value) {
    parameters_.emplace(key, value->clone());
   }
  }
 }

  ~MaterialDefinitions() override;

  void vSetMaterialInstancePropertiesFromMyPropertyMap(
      const ::filament::Material* materialResult,
      filament::MaterialInstance* materialInstance,
      const TextureMap& loadedTextures) const;

  // this will either get the assetPath or the url, priority of assetPath
  // looking for which is valid. Used to see if we have this loaded in cache.
  [[nodiscard]] std::string szGetMaterialDefinitionLookupName() const;

  // This will go through each of the parameters and return only the
  // texture_(definitions) so the material manager can load what's not already
  // loaded.
  [[nodiscard]] std::vector<MaterialParameter*>
  vecGetTextureMaterialParameters() const;

  [[nodiscard]] std::string szGetMaterialAssetPath() const {
    return assetPath_;
  }
  [[nodiscard]] std::string szGetMaterialURLPath() const { return url_; }

 void DebugPrint(const std::string& tabPrefix) const override;

 static size_t StaticGetTypeID() { return typeid(MaterialDefinitions).hash_code(); }

 [[nodiscard]] size_t GetTypeID() const override { return StaticGetTypeID(); }

 [[nodiscard]] Component* Clone() const override {
  auto* cloned = new MaterialDefinitions(*this);  // Copy constructor is called here
  return cloned;
 }

 private:
  std::string assetPath_;
  std::string url_;
  std::map<std::string, std::unique_ptr<MaterialParameter>> parameters_;
};
}  // namespace plugin_filament_view