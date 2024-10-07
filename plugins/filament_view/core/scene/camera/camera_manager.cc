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

#include "camera_manager.h"

#include <core/systems/derived/filament_system.h>
#include <core/systems/ecsystems_manager.h>
#include <core/utils/entitytransforms.h>

#include "asio/post.hpp"

#include <filament/math/TMatHelpers.h>
#include <filament/math/mat4.h>
#include <filament/math/vec4.h>

#include "core/include/additionalmath.h"
#include "plugins/common/common.h"
#include "touch_pair.h"

#define USING_CAM_MANIPULATOR 0

namespace plugin_filament_view {
CameraManager::CameraManager() : currentVelocity_(0), initialTouchPosition_(0) {
  SPDLOG_TRACE("++CameraManager::CameraManager");
  setDefaultCamera();
  SPDLOG_TRACE("--CameraManager::CameraManager: {}");
}

void CameraManager::setDefaultCamera() {
  SPDLOG_TRACE("++{}::{}", __FILE__, __FUNCTION__);

  auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "CameraManager::setDefaultCamera");
  const auto engine = filamentSystem->getFilamentEngine();

  auto fview = filamentSystem->getFilamentView();
  assert(fview);

  cameraEntity_ = engine->getEntityManager().create();
  camera_ = engine->createCamera(cameraEntity_);

  /// With the default parameters, the scene must contain at least one Light
  /// of intensity similar to the sun (e.g.: a 100,000 lux directional light).
  camera_->setExposure(kAperture, kShutterSpeed, kSensitivity);

  auto viewport = fview->getViewport();
  cameraManipulator_ = CameraManipulator::Builder()
                           .viewport(static_cast<int>(viewport.width),
                                     static_cast<int>(viewport.height))
                           .build(::filament::camutils::Mode::ORBIT);
  filament::math::float3 eye, center, up;
  cameraManipulator_->getLookAt(&eye, &center, &up);
  setCameraLookat(eye, center, up);
  fview->setCamera(camera_);
  SPDLOG_TRACE("--{}::{}", __FILE__, __FUNCTION__);
}

void CameraManager::setCameraLookat(filament::math::float3 eye,
                                    filament::math::float3 center,
                                    filament::math::float3 up) {
  if (camera_ == nullptr) {
    SPDLOG_DEBUG("Unable to set Camera Lookat, camera is null {} {} {}",
                 __FILE__, __FUNCTION__, __LINE__);
    return;
  }

  camera_->lookAt(eye, center, up);
}

std::string CameraManager::updateExposure(Exposure* exposure) {
  if (!exposure) {
    return "Exposure not found";
  }
  auto e = exposure;
  if (e->exposure_.has_value()) {
    SPDLOG_DEBUG("[setExposure] exposure: {}", e->exposure_.value());
    camera_->setExposure(e->exposure_.value());
    return "Exposure updated successfully";
  }

  auto aperture = e->aperture_.has_value() ? e->aperture_.value() : kAperture;
  auto shutterSpeed =
      e->shutterSpeed_.has_value() ? e->shutterSpeed_.value() : kShutterSpeed;
  auto sensitivity =
      e->sensitivity_.has_value() ? e->sensitivity_.value() : kSensitivity;
  SPDLOG_DEBUG("[setExposure] aperture: {}, shutterSpeed: {}, sensitivity: {}",
               aperture, shutterSpeed, sensitivity);
  camera_->setExposure(aperture, shutterSpeed, sensitivity);
  return "Exposure updated successfully";
}

std::string CameraManager::updateProjection(Projection* projection) {
  if (!projection) {
    return "Projection not found";
  }
  auto p = projection;
  if (p->projection_.has_value() && p->left_.has_value() &&
      p->right_.has_value() && p->top_.has_value() && p->bottom_.has_value()) {
    const auto project = p->projection_.value();
    auto left = p->left_.value();
    auto right = p->right_.value();
    auto top = p->top_.value();
    auto bottom = p->bottom_.value();
    auto near = p->near_.has_value() ? p->near_.value() : kNearPlane;
    auto far = p->far_.has_value() ? p->far_.value() : kFarPlane;
    SPDLOG_DEBUG(
        "[setProjection] left: {}, right: {}, bottom: {}, top: {}, near: {}, "
        "far: {}",
        left, right, bottom, top, near, far);
    camera_->setProjection(project, left, right, bottom, top, near, far);
    return "Projection updated successfully";
  }

  if (p->fovInDegrees_.has_value() && p->fovDirection_.has_value()) {
    auto fovInDegrees = p->fovInDegrees_.value();
    auto aspect =
        p->aspect_.has_value() ? p->aspect_.value() : calculateAspectRatio();
    auto near = p->near_.has_value() ? p->near_.value() : kNearPlane;
    auto far = p->far_.has_value() ? p->far_.value() : kFarPlane;
    const auto fovDirection = p->fovDirection_.value();
    SPDLOG_DEBUG(
        "[setProjection] fovInDegress: {}, aspect: {}, near: {}, far: {}, "
        "direction: {}",
        fovInDegrees, aspect, near, far,
        Projection::getTextForFov(fovDirection));

    camera_->setProjection(fovInDegrees, aspect, near, far, fovDirection);
    return "Projection updated successfully";
  }

  return "Projection info must be provided";
}

std::string CameraManager::updateCameraShift(std::vector<double>* shift) {
  if (!shift) {
    return "Camera shift not found";
  }
  const auto s = shift;
  if (s->size() >= 2) {
    return "Camera shift info must be provided";
  }
  SPDLOG_DEBUG("[setShift] {}, {}", s->at(0), s->at(1));
  camera_->setShift({s->at(0), s->at(1)});
  return "Camera shift updated successfully";
}

std::string CameraManager::updateCameraScaling(std::vector<double>* scaling) {
  if (!scaling) {
    return "Camera scaling must be provided";
  }
  const auto s = scaling;
  if (s->size() >= 2) {
    return "Camera scaling info must be provided";
  }
  SPDLOG_DEBUG("[setScaling] {}, {}", s->at(0), s->at(1));
  camera_->setScaling({s->at(0), s->at(1)});
  return "Camera scaling updated successfully";
}

void CameraManager::updateCameraManipulator(Camera* cameraInfo) {
  if (!cameraInfo) {
    return;
  }

  auto manipulatorBuilder = CameraManipulator::Builder();

  if (cameraInfo->targetPosition_) {
    const auto tp = cameraInfo->targetPosition_.get();
    manipulatorBuilder.targetPosition(tp->x, tp->y, tp->z);

  } else {
    manipulatorBuilder.targetPosition(kDefaultObjectPosition.x,
                                      kDefaultObjectPosition.y,
                                      kDefaultObjectPosition.z);
  }

  if (cameraInfo->upVector_) {
    const auto upVector = cameraInfo->upVector_.get();
    manipulatorBuilder.upVector(upVector->x, upVector->y, upVector->z);
  }
  if (cameraInfo->zoomSpeed_.has_value()) {
    manipulatorBuilder.zoomSpeed(cameraInfo->zoomSpeed_.value());
  }

  if (cameraInfo->orbitHomePosition_) {
    const auto orbitHomePosition = cameraInfo->orbitHomePosition_.get();
    manipulatorBuilder.orbitHomePosition(
        orbitHomePosition->x, orbitHomePosition->y, orbitHomePosition->z);
  }

  if (cameraInfo->orbitSpeed_) {
    const auto orbitSpeed = cameraInfo->orbitSpeed_.get();
    manipulatorBuilder.orbitSpeed(orbitSpeed->at(0), orbitSpeed->at(1));
  }

  manipulatorBuilder.fovDirection(cameraInfo->fovDirection_);

  if (cameraInfo->fovDegrees_.has_value()) {
    manipulatorBuilder.fovDegrees(cameraInfo->fovDegrees_.value());
  }

  if (cameraInfo->farPlane_.has_value()) {
    manipulatorBuilder.farPlane(cameraInfo->farPlane_.value());
  }

  if (cameraInfo->flightStartPosition_) {
    const auto flightStartPosition = cameraInfo->flightStartPosition_.get();
    manipulatorBuilder.flightStartPosition(
        flightStartPosition->x, flightStartPosition->y, flightStartPosition->z);
  }

  if (cameraInfo->flightStartOrientation_) {
    const auto flightStartOrientation =
        cameraInfo->flightStartOrientation_.get();
    auto pitch = flightStartOrientation->at(0);  // 0f;
    auto yaw = flightStartOrientation->at(1);    // 0f;
    manipulatorBuilder.flightStartOrientation(pitch, yaw);
  }

  if (cameraInfo->flightMoveDamping_.has_value()) {
    manipulatorBuilder.flightMoveDamping(
        cameraInfo->flightMoveDamping_.value());
  }

  if (cameraInfo->flightSpeedSteps_.has_value()) {
    manipulatorBuilder.flightSpeedSteps(cameraInfo->flightSpeedSteps_.value());
  }

  if (cameraInfo->flightMaxMoveSpeed_.has_value()) {
    manipulatorBuilder.flightMaxMoveSpeed(
        cameraInfo->flightMaxMoveSpeed_.value());
  }

  if (cameraInfo->groundPlane_) {
    const auto groundPlane = cameraInfo->groundPlane_.get();
    auto a = groundPlane->at(0);
    auto b = groundPlane->at(1);
    auto c = groundPlane->at(2);
    auto d = groundPlane->at(3);
    manipulatorBuilder.groundPlane(a, b, c, d);
  }

  auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "CameraManager::setDefaultCamera");

  const auto viewport = filamentSystem->getFilamentView()->getViewport();
  manipulatorBuilder.viewport(static_cast<int>(viewport.width),
                              static_cast<int>(viewport.height));
  cameraManipulator_ = manipulatorBuilder.build(cameraInfo->mode_);
}

void CameraManager::updateCamera(
    Camera* cameraInfo) {
  SPDLOG_DEBUG("++CameraManager::updateCamera");

   updateExposure(cameraInfo->exposure_.get());
   updateProjection(cameraInfo->projection_.get());
   updateLensProjection(cameraInfo->lensProjection_.get());
   updateCameraShift(cameraInfo->shift_.get());
   updateCameraScaling(cameraInfo->scaling_.get());
   updateCameraManipulator(cameraInfo);

  SPDLOG_DEBUG("--CameraManager::updateCamera");
}

void CameraManager::setPrimaryCamera(std::unique_ptr<Camera> camera) {
  primaryCamera_ = std::shared_ptr<Camera>(std::move(camera));

  // We'll want to set the 'defaults' depending on camera mode here.
  if (primaryCamera_->eCustomCameraMode_ == Camera::InertiaAndGestures) {
    filament::math::float3 eye, center, up;
    cameraManipulator_->getLookAt(&eye, &center, &up);

    setCameraLookat(*primaryCamera_->flightStartPosition_,
                    *primaryCamera_->targetPosition_,
                    *primaryCamera_->upVector_);
  }
}

void CameraManager::vResetInertiaCameraToDefaultValues() {
  if (primaryCamera_->eCustomCameraMode_ == Camera::InertiaAndGestures) {
    primaryCamera_->vResetInertiaCameraToDefaultValues();

    currentVelocity_ = {0};

    setCameraLookat(*primaryCamera_->flightStartPosition_,
                    *primaryCamera_->targetPosition_,
                    *primaryCamera_->upVector_);
  }
}

void CameraManager::lookAtDefaultPosition() {
  filament::math::float3 eye, center, up;
  cameraManipulator_->getLookAt(&eye, &center, &up);
  setCameraLookat(eye, center, up);
}

void CameraManager::ChangePrimaryCameraMode(const std::string& szValue) {
  if (szValue == Camera::kModeAutoOrbit) {
    primaryCamera_->eCustomCameraMode_ = Camera::AutoOrbit;
  } else if (szValue == Camera::kModeInertiaAndGestures) {
    primaryCamera_->eCustomCameraMode_ = Camera::InertiaAndGestures;
  } else {
    spdlog::warn(
        "Camera mode unset, you tried to set to {} , but that's not "
        "implemented.",
        szValue);
    primaryCamera_->eCustomCameraMode_ = Camera::Unset;
  }
}

void CameraManager::updateCamerasFeatures(float fElapsedTime) {
  if (!primaryCamera_ || (primaryCamera_->eCustomCameraMode_ == Camera::Unset &&
                          !primaryCamera_->forceSingleFrameUpdate_)) {
    return;
  }

  if (primaryCamera_->eCustomCameraMode_ == Camera::AutoOrbit) {
    primaryCamera_->forceSingleFrameUpdate_ = false;

    // Note these TODOs are marked for a next iteration tasking.

    // TODO this should be moved to a property on camera
    constexpr float speed = 0.5f;  // Rotation speed
    // TODO this should be moved to a property on camera
    constexpr float radius = 8.0f;  // Distance from the camera to the object

    // camera needs angle
    primaryCamera_->fCurrentOrbitAngle_ += fElapsedTime * speed;

    filament::math::float3 eye;
    eye.x = radius * std::cos(primaryCamera_->fCurrentOrbitAngle_);
    eye.y = primaryCamera_->orbitHomePosition_->y;
    eye.z = radius * std::sin(primaryCamera_->fCurrentOrbitAngle_);

    // Center of the rotation (object position)
    filament::math::float3 center = *primaryCamera_->targetPosition_;

    // Up vector
    filament::math::float3 up = *primaryCamera_->upVector_;

    setCameraLookat(eye, center, up);
  } else if (primaryCamera_->eCustomCameraMode_ == Camera::InertiaAndGestures) {
    currentVelocity_.y = 0.0f;

    // Update camera position around the center
    if ((currentVelocity_.x == 0.0f && currentVelocity_.y == 0.0f &&
         currentVelocity_.z == 0.0f) &&
        !isPanGesture()) {
      return;
    }

#if USING_CAM_MANIPULATOR == 0  // Not using camera manipulator
    auto rotationSpeed =
        static_cast<float>(primaryCamera_->inertia_rotationSpeed_);

    // Calculate rotation angles from velocity
    float angleX = currentVelocity_.x * rotationSpeed;
    // float angleY = currentVelocity_.y * rotationSpeed;

    // Update the orbit angle of the camera
    primaryCamera_->fCurrentOrbitAngle_ += angleX;

    // Calculate the new camera eye position based on the orbit angle
    float zoomSpeed = primaryCamera_->zoomSpeed_.value_or(0.1f);
    float radius =
        primaryCamera_->current_zoom_radius_ - currentVelocity_.z * zoomSpeed;

    // Clamp the radius between zoom_minCap_ and zoom_maxCap_
    radius =
        std::clamp(radius, static_cast<float>(primaryCamera_->zoom_minCap_),
                   static_cast<float>(primaryCamera_->zoom_maxCap_));

    filament::math::float3 eye;
    eye.x = radius * std::cos(primaryCamera_->fCurrentOrbitAngle_);
    eye.y = primaryCamera_->flightStartPosition_->y;
    eye.z = radius * std::sin(primaryCamera_->fCurrentOrbitAngle_);

    filament::math::float3 center = *primaryCamera_->targetPosition_;
    filament::math::float3 up = {0.0f, 1.0f, 0.0f};

    setCameraLookat(eye, center, up);

    // Now we're going to add on pan
    auto modelMatrix = camera_->getModelMatrix();

    auto pitchQuat = filament::math::quatf::fromAxisAngle(
        filament::float3{1.0f, 0.0f, 0.0f},
        primaryCamera_->current_pitch_addition_);

    auto yawQuat = filament::math::quatf::fromAxisAngle(
        filament::float3{0.0f, 1.0f, 0.0f},
        primaryCamera_->current_yaw_addition_);

    filament::math::mat4f pitchMatrix =
        EntityTransforms::QuaternionToMat4f(pitchQuat);
    filament::math::mat4f yawMatrix =
        EntityTransforms::QuaternionToMat4f(yawQuat);

    modelMatrix = modelMatrix * yawMatrix * pitchMatrix;
    camera_->setModelMatrix(modelMatrix);

#else  // using camera manipulator
    // At this time, this does not use velocity/inertia and doesn't cap Y
    // meaning you can get a full up/down view and around.
    cameraManipulator_->update(fElapsedTime);

    filament::math::float3 eye, center, up;
    cameraManipulator_->getLookAt(&eye, &center, &up);
    setCameraLookat(eye, center, up);
#endif

    // Apply inertia decay to gradually reduce velocity
    auto inertiaDecayFactor_ =
        static_cast<float>(primaryCamera_->inertia_decayFactor_);
    currentVelocity_ *= inertiaDecayFactor_;

    primaryCamera_->current_zoom_radius_ = radius;
  }
}

void CameraManager::destroyCamera() {
  SPDLOG_DEBUG("++CameraManager::destroyCamera");
  auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "destroyCamera");
  const auto engine = filamentSystem->getFilamentEngine();

  engine->destroyCameraComponent(cameraEntity_);
  SPDLOG_DEBUG("--CameraManager::destroyCamera");
}

void CameraManager::endGesture() {
  tentativePanEvents_.clear();
  tentativeOrbitEvents_.clear();
  tentativeZoomEvents_.clear();
  currentGesture_ = Gesture::NONE;
  cameraManipulator_->grabEnd();
}

bool CameraManager::isOrbitGesture() {
  return tentativeOrbitEvents_.size() > kGestureConfidenceCount;
}

bool CameraManager::isPanGesture() {
  if (tentativePanEvents_.size() <= kGestureConfidenceCount) {
    return false;
  }
  auto oldest = tentativePanEvents_.front().midpoint();
  auto newest = tentativePanEvents_.back().midpoint();
  return distance(oldest, newest) > kPanConfidenceDistance;
}

bool CameraManager::isZoomGesture() {
  if (tentativeZoomEvents_.size() <= kGestureConfidenceCount) {
    return false;
  }
  auto oldest = tentativeZoomEvents_.front().separation();
  auto newest = tentativeZoomEvents_.back().separation();
  return std::abs(newest - oldest) > kZoomConfidenceDistance;
}

Ray CameraManager::oGetRayInformationFromOnTouchPosition(
    TouchPair touch) const {
  auto castingValues = aGetRayInformationFromOnTouchPosition(touch);
  constexpr float defaultLength = 1000.0f;
  Ray returnRay(castingValues.first, castingValues.second, defaultLength);
  return returnRay;
}

std::pair<filament::math::float3, filament::math::float3>
CameraManager::aGetRayInformationFromOnTouchPosition(TouchPair touch) const {

  auto filamentSystem =
      ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
          FilamentSystem::StaticGetTypeID(), "CameraManager::aGetRayInformationFromOnTouchPosition");

  auto viewport = filamentSystem->getFilamentView()->getViewport();

  // Note at time of writing on a 800*600 resolution this seems like the 10%
  // edges aren't super accurate this might need to be looked at more.

  float ndcX = (2.0f * (float)touch.x()) / (float)viewport.width - 1.0f;
  float ndcY = 1.0f - (2.0f * (float)touch.y()) / (float)viewport.height;
  ndcY = -ndcY;

  filament::math::vec4<float> rayClip(ndcX, ndcY, -1.0f, 1.0f);

  // Get inverse projection and view matrices
  filament::math::mat4 invProj = inverse(camera_->getProjectionMatrix());
  filament::math::vec4<double> rayView = invProj * rayClip;
  rayView = filament::math::vec4<double>(rayView.x, rayView.y, -1.0f, 0.0f);

  filament::math::mat4 invView = inverse(camera_->getViewMatrix());
  filament::math::vec3<double> rayDirection = (invView * rayView).xyz;
  rayDirection = normalize(rayDirection);

  // Camera position
  filament::math::vec3<double> rayOrigin = invView[3].xyz;

  return {rayOrigin, rayDirection};
}

void CameraManager::onAction(int32_t action,
                             int32_t point_count,
                             const size_t point_data_size,
                             const double* point_data) {
  // We only care about updating the camera on action if we're set to use those
  // values.
  if (primaryCamera_->eCustomCameraMode_ != Camera::InertiaAndGestures ||
      cameraManipulator_ == nullptr) {
    return;
  }

#if 0  // Hack testing code - for testing camera controls on PC
  if ( action == ACTION_DOWN || action == ACTION_MOVE) {
    currentVelocity_.z += 1.0f;
    return;
  } else if (action == ACTION_UP) {
    currentVelocity_.z -= 1.0f;
    return;
  }
#endif

  auto filamentSystem =
     ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
         FilamentSystem::StaticGetTypeID(), "CameraManager::setDefaultCamera");

  auto viewport = filamentSystem->getFilamentView()->getViewport();
  auto touch =
      TouchPair(point_count, point_data_size, point_data, viewport.height);
  switch (action) {
    case ACTION_DOWN: {
      if (point_count == 1) {
        cameraManipulator_->grabBegin(touch.x(), touch.y(), false);
        initialTouchPosition_ = {touch.x(), touch.y()};
        currentVelocity_ = {0.0f};
      }
    } break;

    case ACTION_MOVE: {
      // CANCEL GESTURE DUE TO UNEXPECTED POINTER COUNT
      if ((point_count != 1 && currentGesture_ == Gesture::ORBIT) ||
          (point_count != 2 && currentGesture_ == Gesture::PAN) ||
          (point_count != 2 && currentGesture_ == Gesture::ZOOM)) {
        endGesture();
        return;
      }

      // UPDATE EXISTING GESTURE

      if (currentGesture_ == Gesture::ZOOM) {
        auto d0 = previousTouch_.separation();
        auto d1 = touch.separation();
        cameraManipulator_->scroll(touch.x(), touch.y(),
                                   (d0 - d1) * kZoomSpeed);

        currentVelocity_.z = (d0 - d1) * kZoomSpeed;

        previousTouch_ = touch;
        return;
      }

      if (currentGesture_ != Gesture::NONE) {
        cameraManipulator_->grabUpdate(touch.x(), touch.y());
        if (isPanGesture()) {
          return;
        }
      }

      // DETECT NEW GESTURE
      if (point_count == 1) {
        tentativeOrbitEvents_.push_back(touch);
      }

      if (point_count == 2) {
        tentativePanEvents_.push_back(touch);
        tentativeZoomEvents_.push_back(touch);
      }

      // Calculate the delta movement
      filament::math::float2 currentPosition = {touch.x(), touch.y()};
      filament::math::float2 delta = currentPosition - initialTouchPosition_;

      auto velocityFactor =
          static_cast<float>(primaryCamera_->inertia_velocityFactor_);

      if (isOrbitGesture()) {
        cameraManipulator_->grabUpdate(touch.x(), touch.y());
        currentGesture_ = Gesture::ORBIT;

        // Update velocity based on movement
        currentVelocity_.xy += delta * velocityFactor;

        // Update touch position for the next move
        initialTouchPosition_ = currentPosition;

        return;
      }

      if (isZoomGesture()) {
        currentGesture_ = Gesture::ZOOM;
        previousTouch_ = touch;
        return;
      }

      if (isPanGesture()) {
        primaryCamera_->current_pitch_addition_ +=
            delta.y * velocityFactor * .01f;
        primaryCamera_->current_yaw_addition_ -=
            delta.x * velocityFactor * .01f;

        // Convert your angle caps from degrees to radians
        float pitchCapRadians =
            static_cast<float>(primaryCamera_->pan_angleCapX_) *
            degreesToRadians;
        float yawCapRadians =
            static_cast<float>(primaryCamera_->pan_angleCapY_) *
            degreesToRadians;

        primaryCamera_->current_pitch_addition_ =
            std::clamp(primaryCamera_->current_pitch_addition_,
                       -pitchCapRadians, pitchCapRadians);

        primaryCamera_->current_yaw_addition_ =
            std::clamp(primaryCamera_->current_yaw_addition_, -yawCapRadians,
                       yawCapRadians);

        cameraManipulator_->grabBegin(touch.x(), touch.y(), true);
        currentGesture_ = Gesture::PAN;
      }
    } break;
    case ACTION_CANCEL:
    case ACTION_UP:
    default:
      endGesture();
      break;
  }
}

std::string CameraManager::updateLensProjection(
    LensProjection* lensProjection) {
  if (!lensProjection) {
    return "Lens projection not found";
  }

  float lensProjectionFocalLength = lensProjection->getFocalLength();
  if (cameraFocalLength_ != lensProjectionFocalLength)
    cameraFocalLength_ = lensProjectionFocalLength;
  auto aspect = lensProjection->getAspect().has_value()
                    ? lensProjection->getAspect().value()
                    : calculateAspectRatio();
  camera_->setLensProjection(
      lensProjectionFocalLength, aspect,
      lensProjection->getNear().has_value() ? lensProjection->getNear().value()
                                            : kNearPlane,
      lensProjection->getFar().has_value() ? lensProjection->getFar().value()
                                           : kFarPlane);
  return "Lens projection updated successfully";
}

void CameraManager::updateCameraProjection() {
  auto aspect = calculateAspectRatio();
  auto lensProjection = new LensProjection(cameraFocalLength_, aspect);
  updateLensProjection(lensProjection);
  delete lensProjection;
}

float CameraManager::calculateAspectRatio() {
  auto filamentSystem =
    ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
        FilamentSystem::StaticGetTypeID(), "CameraManager::aGetRayInformationFromOnTouchPosition");

  auto viewport = filamentSystem->getFilamentView()->getViewport();
  return static_cast<float>(viewport.width) /
         static_cast<float>(viewport.height);
}

void CameraManager::updateCameraOnResize(uint32_t width, uint32_t height) {
  cameraManipulator_->setViewport(static_cast<int>(width),
                                  static_cast<int>(height));
  updateCameraProjection();
}

}  // namespace plugin_filament_view
