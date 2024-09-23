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

#include "asio/post.hpp"

#include <filament/math/TMatHelpers.h>
#include <filament/math/mat4.h>
#include <filament/math/vec4.h>

#include "plugins/common/common.h"
#include "touch_pair.h"

namespace plugin_filament_view {
CameraManager::CameraManager() {
  SPDLOG_TRACE("++CameraManager::CameraManager");
  setDefaultCamera();
  SPDLOG_TRACE("--CameraManager::CameraManager: {}");
}

void CameraManager::setDefaultCamera() {
  SPDLOG_TRACE("++CameraManager::setDefaultCamera");

  // const auto promise(std::make_shared<std::promise<void>>());
  // auto future(promise->get_future());
  // asio::post(modelViewer->getStrandContext(), [&, promise] {

  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  ::filament::Engine* engine = modelViewer->getFilamentEngine();

  auto fview = modelViewer->getFilamentView();
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
                           .build(::filament::camutils::Mode::FREE_FLIGHT);
  filament::math::float3 eye, center, up;
  cameraManipulator_->getLookAt(&eye, &center, &up);
  setCameraLookat(eye, center, up);
  fview->setCamera(camera_);
  SPDLOG_TRACE("--CameraManager::setDefaultCamera");
  // promise->set_value();

  //});
  // return future;
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
    SPDLOG_DEBUG("[CameraManipulator] targetPosition: {}, {}, {}", tp->x, tp->y,
                 tp->z);
  } else {
    manipulatorBuilder.targetPosition(kDefaultObjectPosition.x,
                                      kDefaultObjectPosition.y,
                                      kDefaultObjectPosition.z);
    SPDLOG_DEBUG("[CameraManipulator] targetPosition: {}, {}, {}",
                 kDefaultObjectPosition.x, kDefaultObjectPosition.y,
                 kDefaultObjectPosition.z);
  }

  if (cameraInfo->upVector_) {
    const auto upVector = cameraInfo->upVector_.get();
    manipulatorBuilder.upVector(upVector->x, upVector->y, upVector->z);
    SPDLOG_DEBUG("[CameraManipulator] upVector: {}, {}, {}", upVector->x,
                 upVector->y, upVector->z);
  }
  if (cameraInfo->zoomSpeed_.has_value()) {
    manipulatorBuilder.zoomSpeed(cameraInfo->zoomSpeed_.value());
    SPDLOG_DEBUG("[CameraManipulator] zoomSpeed: {}",
                 cameraInfo->zoomSpeed_.value());
  }

  if (cameraInfo->orbitHomePosition_) {
    const auto orbitHomePosition = cameraInfo->orbitHomePosition_.get();
    manipulatorBuilder.orbitHomePosition(
        orbitHomePosition->x, orbitHomePosition->y, orbitHomePosition->z);
    SPDLOG_DEBUG("[CameraManipulator] orbitHomePosition: {}, {}, {}",
                 orbitHomePosition->x, orbitHomePosition->y,
                 orbitHomePosition->z);
  }

  if (cameraInfo->orbitSpeed_) {
    const auto orbitSpeed = cameraInfo->orbitSpeed_.get();
    manipulatorBuilder.orbitSpeed(orbitSpeed->at(0), orbitSpeed->at(1));
    SPDLOG_DEBUG("[CameraManipulator] orbitSpeed: {}, {}", orbitSpeed->at(0),
                 orbitSpeed->at(1));
  }

  manipulatorBuilder.fovDirection(cameraInfo->fovDirection_);
  SPDLOG_DEBUG("[CameraManipulator] fovDirection: {}",
               static_cast<int>(cameraInfo->fovDirection_));

  if (cameraInfo->fovDegrees_.has_value()) {
    manipulatorBuilder.fovDegrees(cameraInfo->fovDegrees_.value());
    SPDLOG_DEBUG("[CameraManipulator] fovDegrees: {}",
                 cameraInfo->fovDegrees_.value());
  }

  if (cameraInfo->farPlane_.has_value()) {
    manipulatorBuilder.farPlane(cameraInfo->farPlane_.value());
    SPDLOG_DEBUG("[CameraManipulator] farPlane: {}",
                 cameraInfo->farPlane_.value());
  }

  if (cameraInfo->mapExtent_) {
    const auto mapExtent = cameraInfo->mapExtent_.get();
    manipulatorBuilder.mapExtent(mapExtent->at(0), mapExtent->at(1));
    SPDLOG_DEBUG("[CameraManipulator] mapExtent: {}, {}", mapExtent->at(0),
                 mapExtent->at(1));
  }

  if (cameraInfo->flightStartPosition_) {
    const auto flightStartPosition = cameraInfo->flightStartPosition_.get();
    manipulatorBuilder.flightStartPosition(
        flightStartPosition->x, flightStartPosition->y, flightStartPosition->z);
    SPDLOG_DEBUG("[CameraManipulator] flightStartPosition: {}, {}, {}",
                 flightStartPosition->x, flightStartPosition->y,
                 flightStartPosition->z);
  }

  if (cameraInfo->flightStartOrientation_) {
    const auto flightStartOrientation =
        cameraInfo->flightStartOrientation_.get();
    auto pitch = flightStartOrientation->at(0);  // 0f;
    auto yaw = flightStartOrientation->at(1);    // 0f;
    manipulatorBuilder.flightStartOrientation(pitch, yaw);
    SPDLOG_DEBUG("[CameraManipulator] flightStartOrientation: {}, {}", pitch,
                 yaw);
  }

  if (cameraInfo->flightMoveDamping_.has_value()) {
    manipulatorBuilder.flightMoveDamping(
        cameraInfo->flightMoveDamping_.value());
    SPDLOG_DEBUG("[CameraManipulator] flightMoveDamping: {}",
                 cameraInfo->flightMoveDamping_.value());
  }

  if (cameraInfo->flightSpeedSteps_.has_value()) {
    manipulatorBuilder.flightSpeedSteps(cameraInfo->flightSpeedSteps_.value());
    SPDLOG_DEBUG("[CameraManipulator] flightSpeedSteps: {}",
                 cameraInfo->flightSpeedSteps_.value());
  }

  if (cameraInfo->flightMaxMoveSpeed_.has_value()) {
    manipulatorBuilder.flightMaxMoveSpeed(
        cameraInfo->flightMaxMoveSpeed_.value());
    SPDLOG_DEBUG("[CameraManipulator] flightMaxMoveSpeed: {}",
                 cameraInfo->flightMaxMoveSpeed_.value());
  }

  if (cameraInfo->groundPlane_) {
    const auto groundPlane = cameraInfo->groundPlane_.get();
    auto a = groundPlane->at(0);
    auto b = groundPlane->at(1);
    auto c = groundPlane->at(2);
    auto d = groundPlane->at(3);
    manipulatorBuilder.groundPlane(a, b, c, d);
    SPDLOG_DEBUG("[CameraManipulator] flightMaxMoveSpeed: {}, {}, {}, {}", a, b,
                 c, d);
  }

  const auto modelViewer = CustomModelViewer::Instance(__FUNCTION__);

  const auto viewport = modelViewer->getFilamentView()->getViewport();
  manipulatorBuilder.viewport(static_cast<int>(viewport.width),
                              static_cast<int>(viewport.height));
  cameraManipulator_ = manipulatorBuilder.build(cameraInfo->mode_);
}

std::future<Resource<std::string_view>> CameraManager::updateCamera(
    Camera* cameraInfo) {
  SPDLOG_DEBUG("++CameraManager::updateCamera");
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);

  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto future(promise->get_future());

  if (!cameraInfo) {
    promise->set_value(Resource<std::string_view>::Error("Camera not found"));
  } else {
    asio::post(modelViewer->getStrandContext(), [&, promise, cameraInfo] {
      updateExposure(cameraInfo->exposure_.get());
      updateProjection(cameraInfo->projection_.get());
      updateLensProjection(cameraInfo->lensProjection_.get());
      updateCameraShift(cameraInfo->shift_.get());
      updateCameraScaling(cameraInfo->scaling_.get());
      updateCameraManipulator(cameraInfo);
      promise->set_value(
          Resource<std::string_view>::Success("Camera updated successfully"));
    });
  }

  SPDLOG_DEBUG("--CameraManager::updateCamera");
  return future;
}

void CameraManager::setPrimaryCamera(std::unique_ptr<Camera> camera) {
  primaryCamera_ = std::shared_ptr<Camera>(std::move(camera));
}

void CameraManager::lookAtDefaultPosition() {
  // SPDLOG_TRACE("++CameraManager::lookAtDefaultPosition");

  // SPDLOG_DEBUG("++CameraManager::lookAtDefaultPosition");
  filament::math::float3 eye, center, up;
  cameraManipulator_->getLookAt(&eye, &center, &up);
  setCameraLookat(eye, center, up);
  // SPDLOG_TRACE("--CameraManager::lookAtDefaultPosition");
}

void CameraManager::togglePrimaryCameraFeatureMode(bool bValue) {
  primaryCamera_->customMode_ = bValue;
}

void CameraManager::updateCamerasFeatures(float fElapsedTime) {
  if (!primaryCamera_ || (!primaryCamera_->customMode_ &&
                          !primaryCamera_->forceSingleFrameUpdate_)) {
    return;
  }

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
}

void CameraManager::destroyCamera() {
  SPDLOG_DEBUG("++CameraManager::destroyCamera");
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  ::filament::Engine* engine = modelViewer->getFilamentEngine();

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
  auto viewport = CustomModelViewer::Instance(__FUNCTION__)
                      ->getFilamentView()
                      ->getViewport();

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
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);

  auto viewport = modelViewer->getFilamentView()->getViewport();
  auto touch =
      TouchPair(point_count, point_data_size, point_data, viewport.height);
  switch (action) {
    case ACTION_MOVE:
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
        previousTouch_ = touch;
        return;
      }

      if (currentGesture_ != Gesture::NONE) {
        cameraManipulator_->grabUpdate(touch.x(), touch.y());
        return;
      }

      // DETECT NEW GESTURE

      if (point_count == 1) {
        tentativeOrbitEvents_.push_back(touch);
      }

      if (point_count == 2) {
        tentativePanEvents_.push_back(touch);
        tentativeZoomEvents_.push_back(touch);
      }

      if (isOrbitGesture()) {
        cameraManipulator_->grabBegin(touch.x(), touch.y(), false);
        currentGesture_ = Gesture::ORBIT;
        return;
      }

      if (isZoomGesture()) {
        currentGesture_ = Gesture::ZOOM;
        previousTouch_ = touch;
        return;
      }

      if (isPanGesture()) {
        cameraManipulator_->grabBegin(touch.x(), touch.y(), true);
        currentGesture_ = Gesture::PAN;
        return;
      }
      break;
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
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);

  auto viewport = modelViewer->getFilamentView()->getViewport();
  return static_cast<float>(viewport.width) /
         static_cast<float>(viewport.height);
}

void CameraManager::updateCameraOnResize(uint32_t width, uint32_t height) {
  cameraManipulator_->setViewport(static_cast<int>(width),
                                  static_cast<int>(height));
  updateCameraProjection();
}

}  // namespace plugin_filament_view
