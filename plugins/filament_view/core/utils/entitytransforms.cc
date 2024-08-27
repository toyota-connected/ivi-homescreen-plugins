#include "entitytransforms.h"
#include <filament/math/TMatHelpers.h>

namespace plugin_filament_view {

using ::utils::Entity;

filament::math::mat3f EntityTransforms::identity3x3() {
    return filament::math::mat3f(
        filament::math::float3(1.0f, 0.0f, 0.0f),
        filament::math::float3(0.0f, 1.0f, 0.0f),
        filament::math::float3(0.0f, 0.0f, 1.0f)
    );
}

filament::math::mat4f EntityTransforms::identity4x4() {
    return filament::math::mat4f(
        filament::math::float4(1.0f, 0.0f, 0.0f, 0.0f),
        filament::math::float4(0.0f, 1.0f, 0.0f, 0.0f),
        filament::math::float4(0.0f, 0.0f, 1.0f, 0.0f),
        filament::math::float4(0.0f, 0.0f, 0.0f, 1.0f)
    );
}

filament::math::mat4f EntityTransforms::oApplyShear(filament::math::mat4f matrix, filament::math::float3 shear) {
    // Create a shear matrix
    filament::math::mat4f shearMatrix = identity4x4();

    // Apply shear values (assuming shear.x applies to YZ, shear.y applies to XZ, shear.z applies to XY)
    shearMatrix[1][0] = shear.x;  // Shear along X axis
    shearMatrix[2][0] = shear.y;  // Shear along Y axis
    shearMatrix[2][1] = shear.z;  // Shear along Z axis

    // Multiply the original matrix by the shear matrix
    return matrix * shearMatrix;
}

filament::math::mat3f EntityTransforms::QuaternionToMat3f(const filament::math::quatf& rotation) {
    // Extract quaternion components
    float x = rotation.x;
    float y = rotation.y;
    float z = rotation.z;
    float w = rotation.w;

    // Calculate coefficients
    float xx = x * x;
    float yy = y * y;
    float zz = z * z;
    float xy = x * y;
    float xz = x * z;
    float yz = y * z;
    float wx = w * x;
    float wy = w * y;
    float wz = w * z;

    // Create the 3x3 rotation matrix
    return filament::math::mat3f(
        filament::math::float3(1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy)),
        filament::math::float3(2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx)),
        filament::math::float3(2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy))
    );
}

filament::math::mat4f EntityTransforms::QuaternionToMat4f(const filament::math::quatf& rotation) {
    // Use the QuaternionToMat3f function to get the 3x3 matrix
    filament::math::mat3f rotationMatrix = QuaternionToMat3f(rotation);

    // Embed the 3x3 rotation matrix into a 4x4 matrix
    return filament::math::mat4f(rotationMatrix);
}

// Functions that use CustomModelViewer to get the engine
void EntityTransforms::vApplyScale(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 scale) {
    auto engine = CustomModelViewer::Instance(__FUNCTION__)->getFilamentEngine();
    vApplyScale(poEntity, scale, engine);
}

void EntityTransforms::vApplyRotation(std::shared_ptr<utils::Entity> poEntity, filament::math::quatf rotation) {
    auto engine = CustomModelViewer::Instance(__FUNCTION__)->getFilamentEngine();
    vApplyRotation(poEntity, rotation, engine);
}

void EntityTransforms::vApplyTranslate(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 translation) {
    auto engine = CustomModelViewer::Instance(__FUNCTION__)->getFilamentEngine();
    vApplyTranslate(poEntity, translation, engine);
}

void EntityTransforms::vApplyTransform(std::shared_ptr<utils::Entity> poEntity, filament::math::mat4f transform) {
    auto engine = CustomModelViewer::Instance(__FUNCTION__)->getFilamentEngine();
    vApplyTransform(poEntity, transform, engine);
}

void EntityTransforms::vApplyTransform(std::shared_ptr<utils::Entity> poEntity, filament::math::quatf rotation, filament::math::float3 scale, filament::math::float3 translation) {
    auto engine = CustomModelViewer::Instance(__FUNCTION__)->getFilamentEngine();
    vApplyTransform(poEntity, rotation, scale, translation, engine);
}

void EntityTransforms::vApplyShear(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 shear) {
    auto engine = CustomModelViewer::Instance(__FUNCTION__)->getFilamentEngine();
    vApplyShear(poEntity, shear, engine);
}

void EntityTransforms::vResetTransform(std::shared_ptr<utils::Entity> poEntity) {
    auto engine = CustomModelViewer::Instance(__FUNCTION__)->getFilamentEngine();
    vResetTransform(poEntity, engine);
}

filament::math::mat4f EntityTransforms::oGetCurrentTransform(std::shared_ptr<utils::Entity> poEntity) {
    auto engine = CustomModelViewer::Instance(__FUNCTION__)->getFilamentEngine();
    return oGetCurrentTransform(poEntity, engine);
}

void EntityTransforms::vApplyLookAt(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 target, filament::math::float3 up) {
    auto engine = CustomModelViewer::Instance(__FUNCTION__)->getFilamentEngine();
    vApplyLookAt(poEntity, target, up, engine);
}

// Functions that take an engine as a parameter (these already exist)
void EntityTransforms::vApplyScale(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 scale, ::filament::Engine* engine) {
    if (!(*poEntity))
        return;

    auto& transformManager = engine->getTransformManager();
    auto instance = transformManager.getInstance(*poEntity);

    // Get the current transform
    auto currentTransform = transformManager.getTransform(instance);

    // Create the scaling matrix
    auto scalingMatrix = filament::math::mat4f::scaling(scale);

    // Combine the current transform with the scaling matrix
    auto combinedTransform = currentTransform * scalingMatrix;

    // Set the combined transform back to the entity
    transformManager.setTransform(instance, combinedTransform);
}

void EntityTransforms::vApplyRotation(std::shared_ptr<utils::Entity> poEntity, filament::math::quatf rotation, ::filament::Engine* engine) {
    if (!(*poEntity))
        return;

    auto& transformManager = engine->getTransformManager();
    auto instance = transformManager.getInstance(*poEntity);

    // Get the current transform
    auto currentTransform = transformManager.getTransform(instance);

    // Convert the quaternion to a 4x4 rotation matrix
    filament::math::mat4f rotationMat4 = QuaternionToMat4f(rotation);

    // Combine the current transform with the rotation matrix
    auto combinedTransform = currentTransform * rotationMat4;

    // Set the combined transform back to the entity
    transformManager.setTransform(instance, combinedTransform);
}

void EntityTransforms::vApplyTranslate(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 translation, ::filament::Engine* engine) {
    if (!(*poEntity))
        return;

    auto& transformManager = engine->getTransformManager();
    auto instance = transformManager.getInstance(*poEntity);

    // Get the current transform
    auto currentTransform = transformManager.getTransform(instance);

    // Create the translation matrix
    auto translationMatrix = filament::math::mat4f::translation(translation);

    // Combine the current transform with the translation matrix
    auto combinedTransform = currentTransform * translationMatrix;

    // Set the combined transform back to the entity
    transformManager.setTransform(instance, combinedTransform);
}

void EntityTransforms::vApplyTransform(std::shared_ptr<utils::Entity> poEntity, filament::math::mat4f transform, ::filament::Engine* engine) {
    if (!(*poEntity))
        return;

    auto& transformManager = engine->getTransformManager();
    auto instance = transformManager.getInstance(*poEntity);

    // Set the provided transform matrix directly to the entity
    transformManager.setTransform(instance, transform);
}

void EntityTransforms::vApplyTransform(std::shared_ptr<utils::Entity> poEntity, filament::math::quatf rotation, filament::math::float3 scale, filament::math::float3 translation, ::filament::Engine* engine) {
    if (!(*poEntity))
        return;

    auto& transformManager = engine->getTransformManager();
    auto instance = transformManager.getInstance(*poEntity);

    // Create the rotation, scaling, and translation matrices
    auto rotationMatrix = QuaternionToMat4f(rotation);
    auto scalingMatrix = filament::math::mat4f::scaling(scale);
    auto translationMatrix = filament::math::mat4f::translation(translation);

    // Combine the transformations: translate * rotate * scale
    auto combinedTransform = translationMatrix * rotationMatrix * scalingMatrix;

    // Set the combined transform back to the entity
    transformManager.setTransform(instance, combinedTransform);
}

void EntityTransforms::vApplyShear(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 shear, ::filament::Engine* engine) {
    if (!(*poEntity))
        return;

    auto& transformManager = engine->getTransformManager();
    auto instance = transformManager.getInstance(*poEntity);

    // Get the current transform
    auto currentTransform = transformManager.getTransform(instance);

    // Create the shear matrix
    auto shearMatrix = oApplyShear(currentTransform, shear);

    // Combine the current transform with the shear matrix
    auto combinedTransform = currentTransform * shearMatrix;

    // Set the combined transform back to the entity
    transformManager.setTransform(instance, combinedTransform);
}

void EntityTransforms::vResetTransform(std::shared_ptr<utils::Entity> poEntity, ::filament::Engine* engine) {
    if (!(*poEntity))
        return;

    auto& transformManager = engine->getTransformManager();
    auto instance = transformManager.getInstance(*poEntity);

    // Reset the transform to identity
    auto identityMatrix = identity4x4();
    transformManager.setTransform(instance, identityMatrix);
}

filament::math::mat4f EntityTransforms::oGetCurrentTransform(std::shared_ptr<utils::Entity> poEntity, ::filament::Engine* engine) {
    if (!(*poEntity))
        return identity4x4();

    auto& transformManager = engine->getTransformManager();
    auto instance = transformManager.getInstance(*poEntity);

    // Return the current transform
    return transformManager.getTransform(instance);
}

void EntityTransforms::vApplyLookAt(std::shared_ptr<utils::Entity> poEntity, filament::math::float3 target, filament::math::float3 up, ::filament::Engine* engine) {
    if (!(*poEntity))
        return;

    auto& transformManager = engine->getTransformManager();
    auto instance = transformManager.getInstance(*poEntity);

    // Get the current position of the entity
    auto currentTransform = transformManager.getTransform(instance);
    filament::math::float3 position = currentTransform[3].xyz;

    // Calculate the look-at matrix
    auto lookAtMatrix = filament::math::mat4f::lookAt(position, target, up);

    // Set the look-at transform to the entity
    transformManager.setTransform(instance, lookAtMatrix);
}

} // namespace plugin_filament_view
