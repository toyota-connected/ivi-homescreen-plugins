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

#include <math/vec4.h>
#include <memory>

#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"

#include "core/scene/material/texture/texture.h"
#include "core/scene/material/texture/texture_sampler.h"

namespace plugin_filament_view {

class Texture;

class TextureSampler;

using MaterialTextureValue = std::variant<std::unique_ptr<Texture>>;
using MaterialFloatValue = float;
using MaterialColorValue = ::filament::math::vec4<float>;

class MaterialParameter {
 public:
  enum class MaterialType {
    // color can be presented by int or Color like Colors.white
    COLOR,
    BOOL,
    BOOL_VECTOR,
    FLOAT,
    FLOAT_VECTOR,
    INT,
    INT_VECTOR,
    MAT3,
    MAT4,
    TEXTURE,
  };

  MaterialParameter(std::string name,
                    MaterialType type,
                    MaterialTextureValue value);
  MaterialParameter(std::string name,
                    MaterialType type,
                    MaterialFloatValue value);
  MaterialParameter(std::string name,
                    MaterialType type,
                    MaterialColorValue value);

  static std::unique_ptr<MaterialParameter> Deserialize(
      const std::string& flutter_assets_path,
      const flutter::EncodableMap& params);

  ~MaterialParameter();

  void Print(const char* tag);

  // Disallow copy and assign.
  MaterialParameter(const MaterialParameter&) = delete;
  MaterialParameter& operator=(const MaterialParameter&) = delete;

  std::string szGetParameterName() const { return name_; }

  friend class Material;

 private:
  static constexpr char kColor[] = "COLOR";
  static constexpr char kBool[] = "BOOL";
  static constexpr char kBoolVector[] = "BOOL_VECTOR";
  static constexpr char kFloat[] = "FLOAT";
  static constexpr char kFloatVector[] = "FLOAT_VECTOR";
  static constexpr char kInt[] = "INT";
  static constexpr char kIntVector[] = "INT_VECTOR";
  static constexpr char kMat3[] = "MAT3";
  static constexpr char kMat4[] = "MAT4";
  static constexpr char kTexture[] = "TEXTURE";

  std::string name_;
  MaterialType type_;
  std::optional<MaterialTextureValue> textureValue_;
  std::optional<MaterialFloatValue> fValue_;
  std::optional<MaterialColorValue> colorValue_;

  // TODO delete this, colorOf functionality exists in base filament.
  static MaterialColorValue HexToColorFloat4(const std::string& hex);

  static const char* getTextForType(MaterialType type);

  static MaterialType getTypeForText(const std::string& type);
};
}  // namespace plugin_filament_view