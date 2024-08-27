
#pragma once


#include <filament/math/vec3.h>
#include <memory>
#include <filament/Engine.h>
#include <filament/math/mat4.h>
#include <filament/math/quat.h>
#include <utils/Entity.h>

#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"

#include "core/scene/geometry/direction.h"
#include "core/scene/geometry/position.h"
#include "core/scene/material/model/material.h"

namespace plugin_filament_view {

using ::utils::Entity;

class EntityTransforms {
public:
    
    // Utility functions for quaternion to matrix conversion
    static filament::math::mat3f QuaternionToMat3f(const filament::math::quatf& rotation);
    static filament::math::mat4f QuaternionToMat4f(const filament::math::quatf& rotation);

    // Utility functions to create identity matrices
    static filament::math::mat3f identity3x3();
    static filament::math::mat4f identity4x4();

    // Utility function to create a shear matrix and apply it to a mat4f
    static filament::math::mat4f oApplyShear(filament::math::mat4f matrix, filament::math::float3 shear);

    // Static functions that use CustomModelViewer to get the engine
    static void vApplyScale(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 scale);
    static void vApplyRotation(std::shared_ptr<utils::Entity> poEntity, filament::math::quatf rotation);
    static void vApplyTranslate(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 translation);
    static void vApplyTransform(std::shared_ptr<utils::Entity> poEntity, filament::math::mat4f transform);
    static void vApplyTransform(std::shared_ptr<utils::Entity> poEntity, filament::math::quatf rotation, filament::math::float3 scale, filament::math::float3 translation);
    static void vApplyShear(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 shear);
    static void vResetTransform(std::shared_ptr<utils::Entity> poEntity);
    static void vApplyLookAt(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 target, filament::math::float3 up);

    static filament::math::mat4f oGetCurrentTransform(std::shared_ptr<utils::Entity> poEntity);
    
    // Static functions that take an engine as a parameter
    static void vApplyScale(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 scale, ::filament::Engine* engine);
    static void vApplyRotation(std::shared_ptr<utils::Entity> poEntity, filament::math::quatf rotation, ::filament::Engine* engine);
    static void vApplyTranslate(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 translation, ::filament::Engine* engine);
    static void vApplyTransform(std::shared_ptr<utils::Entity> poEntity, filament::math::mat4f transform, ::filament::Engine* engine);
    static void vApplyTransform(std::shared_ptr<utils::Entity> poEntity, filament::math::quatf rotation, filament::math::float3 scale, filament::math::float3 translation, ::filament::Engine* engine);
    static void vApplyShear(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 shear, ::filament::Engine* engine);
    static void vResetTransform(std::shared_ptr<utils::Entity> poEntity, ::filament::Engine* engine);
    static void vApplyLookAt(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 target, filament::math::float3 up, ::filament::Engine* engine);
    
    static filament::math::mat4f oGetCurrentTransform(std::shared_ptr<utils::Entity> poEntity, ::filament::Engine* engine);
    
};

}