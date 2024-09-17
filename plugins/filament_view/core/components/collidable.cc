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
#include "collidable.h"

#include <list>
#include <algorithm>
#include <core/systems/collision_manager.h>

#include "core/include/literals.h"
#include "core/utils/deserialize.h"
#include "plugins/common/common.h"

namespace plugin_filament_view {

Collidable::Collidable(const flutter::EncodableMap& params)
    : Component(std::string(__FUNCTION__)) {
  Deserialize::DecodeParameterWithDefault(
      kCollidableExtents, &m_f3ExtentsSize, params,
      filament::math::float3(1.0f, 1.0f, 1.0f));

  // Deserialize the static flag, defaulting to 'true'
  Deserialize::DecodeParameterWithDefault(kCollidableIsStatic, &isStatic,
                                          params, true);

  if (isStatic) {
    Deserialize::DecodeParameterWithDefault(kCenterPosition,
                                            &m_f3CenterPosition, params,
                                            filament::math::float3(0, 0, 0));
  }

  // Deserialize the collision layer, defaulting to 0
  Deserialize::DecodeParameterWithDefaultInt64(kCollidableLayer,
                                               &collisionLayer, params, 0);

  // Deserialize the collision mask, defaulting to 0xFFFFFFFF
  Deserialize::DecodeParameterWithDefaultInt64(kCollidableMask, &collisionMask,
                                               params, 0xFFFFFFFFu);

  // Deserialize the flag for matching attached objects, defaulting to 'false'
  Deserialize::DecodeParameterWithDefault(kCollidableShouldMatchAttachedObject,
                                          &shouldMatchAttachedObject, params,
                                          false);

  if (!shouldMatchAttachedObject) {
    Deserialize::DecodeParameterWithDefault(kCenterPosition, &m_f3ExtentsSize,
                                            params,
                                            filament::math::float3(1, 1, 1));

    // Deserialize the shape type, defaulting to some default ShapeType (replace
    // ShapeType::Default with your actual default)
    Deserialize::DecodeEnumParameterWithDefault(
        kCollidableShapeType, &shapeType_, params, ShapeType::Cube);
  }
}

void Collidable::DebugPrint(const std::string& tabPrefix) const {
  spdlog::debug(tabPrefix + "Collidable Debug Info:");

  // Log whether the object is static
  spdlog::debug(tabPrefix + "Is Static: {}", isStatic);

  if (isStatic) {
    spdlog::debug(tabPrefix + "Center Point: x={}, y={}, z={}",
                  m_f3CenterPosition.x, m_f3CenterPosition.y,
                  m_f3CenterPosition.z);
  }

  // Log the collision layer and mask
  spdlog::debug(tabPrefix + "Collision Layer: {}", collisionLayer);
  spdlog::debug(tabPrefix + "Collision Mask: 0x{:X}", collisionMask);

  // Log the flag for whether it should match the attached object
  spdlog::debug(tabPrefix + "Should Match Attached Object: {}",
                shouldMatchAttachedObject);

  // Log the shape type (you can modify this to log the enum name if needed)
  spdlog::debug(tabPrefix + "Shape Type: {}",
                static_cast<int>(shapeType_));  // assuming ShapeType is an enum

  // Log the extents size (x, y, z)
  spdlog::debug(tabPrefix + "Extents Size: x={}, y={}, z={}", m_f3ExtentsSize.x,
                m_f3ExtentsSize.y, m_f3ExtentsSize.z);
}

bool Collidable::bDoesIntersect(const Ray& ray, ::filament::math::float3 &hitPosition) const {
    // Extract relevant data
    const filament::math::float3& center = m_f3CenterPosition;
    const filament::math::float3& extents = m_f3ExtentsSize;
    const filament::math::float3 rayOrigin = ray.f3GetPosition();
    const filament::math::float3 rayDirection = ray.f3GetDirection();

    switch(shapeType_) {
        case ShapeType::Sphere: {
            // Sphere-ray intersection
            float radius = extents.x;  // Assuming the x component of extents represents the radius
            filament::math::float3 oc = rayOrigin - center; // Vector from ray origin to sphere center
            float a = dot(rayDirection, rayDirection);
            float b = 2.0f * dot(oc, rayDirection);
            float c = dot(oc, oc) - radius * radius;
            float discriminant = b * b - 4 * a * c;

            if (discriminant > 0) {
                float t = (-b - sqrt(discriminant)) / (2.0f * a);
                if (t > 0) {
                    hitPosition = rayOrigin + t * rayDirection;
                    SPDLOG_INFO("Collided with sphere {}", GetOwner()->GetGlobalGuid());
                    return true;  // Ray hits the sphere
                }
            }
            break;
        }

        case ShapeType::Cube: {
            // Cube-ray intersection (Axis-Aligned Bounding Box, AABB)
            filament::math::float3 minBound = center - extents * 0.5f;
            filament::math::float3 maxBound = center + extents * 0.5f;

            float tmin = (minBound.x - rayOrigin.x) / rayDirection.x;
            float tmax = (maxBound.x - rayOrigin.x) / rayDirection.x;
            if (tmin > tmax) std::swap(tmin, tmax);

            float tymin = (minBound.y - rayOrigin.y) / rayDirection.y;
            float tymax = (maxBound.y - rayOrigin.y) / rayDirection.y;
            if (tymin > tymax) std::swap(tymin, tymax);

            if ((tmin > tymax) || (tymin > tmax))
                return false;

            if (tymin > tmin)
                tmin = tymin;
            if (tymax < tmax)
                tmax = tymax;

            float tzmin = (minBound.z - rayOrigin.z) / rayDirection.z;
            float tzmax = (maxBound.z - rayOrigin.z) / rayDirection.z;
            if (tzmin > tzmax) std::swap(tzmin, tzmax);

            if ((tmin > tzmax) || (tzmin > tmax))
                return false;

            if (tzmin > tmin)
                tmin = tzmin;
            if (tzmax < tmax)
                tmax = tzmax;

            if (tmin > 0) {
                hitPosition = rayOrigin + tmin * rayDirection;
                SPDLOG_INFO("Collided with cube {}", GetOwner()->GetGlobalGuid());
                return true;  // Ray hits the cube
            }
            break;
        }

        case ShapeType::Plane: {
            // Plane-ray intersection
            // Assuming the plane is defined by its normal (we'll assume it's aligned with the Y-axis) and a point (the center)
            filament::math::float3 planeNormal = {0.0f, 1.0f, 0.0f};  // Y-axis aligned plane
            float denom = dot(rayDirection, planeNormal);
            if (fabs(denom) > 1e-6) { // If denom is not close to zero
                float t = dot(center - rayOrigin, planeNormal) / denom;
                if (t >= 0) {
                    hitPosition = rayOrigin + t * rayDirection;
                    SPDLOG_INFO("Collided with plane {}", GetOwner()->GetGlobalGuid());
                    return true;  // Ray hits the plane
                }
            }
            break;
        }

        // Additional cases like Capsule, etc., can be handled here
        default:
            break;
    }

    return false;  // No intersection
}



}  // namespace plugin_filament_view