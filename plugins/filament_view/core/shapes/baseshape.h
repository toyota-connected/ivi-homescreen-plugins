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

#include <filament/math/quat.h>

#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"

#include "core/scene/geometry/direction.h"
#include "core/scene/geometry/position.h"
#include "core/scene/material/material_definitions.h"

#include "core/components/basetransform.h"
#include "core/components/commonrenderable.h"

namespace plugin_filament_view {

class MaterialManager;
using ::utils::Entity;

namespace shapes {

class BaseShape {
 public:
  BaseShape(const std::string& flutter_assets_path,
            const flutter::EncodableMap& params);

  virtual ~BaseShape();

  virtual void DebugPrint(const char* tag) const;

  // Disallow copy and assign.
  BaseShape(const BaseShape&) = delete;
  BaseShape& operator=(const BaseShape&) = delete;

  //[[nodiscard]] Material* getMaterial() const { return m_poMaterial->get(); }

  virtual bool bInitAndCreateShape(::filament::Engine* engine_,
                                   std::shared_ptr<Entity> entityObject,
                                   MaterialManager* material_manager) = 0;

  void vRemoveEntityFromScene();
  void vAddEntityToScene();

  // futures to create - capsule, cylinder
  enum class ShapeType { Unset = 0, Plane = 1, Cube = 2, Sphere = 3, Max };

 protected:
  ::filament::VertexBuffer* m_poVertexBuffer;
  ::filament::IndexBuffer* m_poIndexBuffer;

  // uses Vertex and Index buffer to create the material and geometry
  // using all the internal variables.
  void vBuildRenderable(::filament::Engine* engine_,
                        MaterialManager* material_manager);

  int id{};
  ShapeType type_{};

  // Components
  BaseTransform m_oBaseTransform;
  CommonRenderable m_oCommonRenderable;

  /// direction of the shape rotation in the world space
  filament::math::float3 m_f3Normal;
  /// material to be used for the shape.
  std::optional<std::unique_ptr<MaterialDefinitions>> m_poMaterialDefinitions;
  Resource<filament::MaterialInstance*> m_poMaterialInstance;

  std::shared_ptr<utils::Entity> m_poEntity;

  // Whether we have winding indexes in both directions.
  bool m_bDoubleSided = false;

  // TODO - Note this is backlogged for using value.
  //        For now this is unimplemented, but would be a <small> savings
  //        when building as code currently allocates buffers for UVs
  bool m_bHasTexturedMaterial = true;

 private:
  void vDestroyBuffers();
};

}  // namespace shapes
}  // namespace plugin_filament_view