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

#include <core/components/base/component.h>

#include <flutter/encodable_value.h>

namespace plugin_filament_view {

class ShapeSystem;

/// @brief Enum for different types of shapes that can be created.
enum class ShapeType {
  Unset = 0,
  Plane = 1,
  Cube = 2,
  Sphere = 3,
  // TODO: add Cylinder
  // TODO: add Capsule
  Max
};

const char* shapeTypeToString(ShapeType type) {
  switch (type) {
    case ShapeType::Unset:
      return "Unset";
    case ShapeType::Plane:
      return "Plane";
    case ShapeType::Cube:
      return "Cube";
    case ShapeType::Sphere:
      return "Sphere";
    case ShapeType::Max:
      return "";
  }
}

using ShapeParams = std::unordered_map<std::string, float>;

class Shape : public Component {
    friend class ShapeSystem;

  public:
    /// @brief The type of shape.
    ShapeType type = ShapeType::Unset;

    /// @brief Whether the shape is rendered as a wireframe.
    /// Whenever set, the geometry needs to be reset in the renderer.
    bool isWireframe = false;

    Shape(const ShapeType type, bool isWireframe = false, ShapeParams params = {})
      : Component(std::string(__FUNCTION__)),
        type(type),
        isWireframe(isWireframe),
        _vertexBuffer(nullptr),
        _indexBuffer(nullptr),
        _params(std::move(params)) {}

    explicit Shape(const flutter::EncodableMap& params);

    Shape(const Shape& other)
      : Component(std::string(__FUNCTION__)),
        type(other.type) {}

    ~Shape() override;

  protected:
    ::filament::VertexBuffer* _vertexBuffer = nullptr;
    ::filament::IndexBuffer* _indexBuffer = nullptr;

    ShapeParams _params;

    void debugPrint(const std::string& prefix) const override;

    [[nodiscard]] inline Component* Clone() const override {
      return new Shape(*this);  // Copy constructor is called here
    }
};

}  // namespace plugin_filament_view
