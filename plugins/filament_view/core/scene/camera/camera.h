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

#include "exposure.h"
#include "lens_projection.h"
#include "projection.h"

#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"

#include <memory>

#include <camutils/Manipulator.h>

namespace plugin_filament_view {

class Exposure;
class Projection;

class Camera {
 public:
  explicit Camera(const flutter::EncodableMap& params);

  void DebugPrint(const char* tag);

  Camera(const Camera& other)
      : mode_(other.mode_),
        eCustomCameraMode_(other.eCustomCameraMode_),
        forceSingleFrameUpdate_(other.forceSingleFrameUpdate_),
        fCurrentOrbitAngle_(other.fCurrentOrbitAngle_),
        fovDirection_(),
        inertia_rotationSpeed_(other.inertia_rotationSpeed_),
        inertia_velocityFactor_(other.inertia_velocityFactor_),
        inertia_decayFactor_(other.inertia_decayFactor_),
        pan_angleCapX_(other.pan_angleCapX_),
        pan_angleCapY_(other.pan_angleCapY_),
        zoom_minCap_(other.zoom_minCap_),
        zoom_maxCap_(other.zoom_maxCap_),
        current_zoom_radius_(other.current_zoom_radius_),
        current_pitch_addition_(other.current_pitch_addition_),
        current_yaw_addition_(other.current_yaw_addition_) {
    // Deep copy unique_ptr members
    if (other.exposure_) {
      exposure_ = std::make_unique<Exposure>(*other.exposure_);
    }
    if (other.projection_) {
      projection_ = std::make_unique<Projection>(*other.projection_);
    }
    if (other.lensProjection_) {
      lensProjection_ =
          std::make_unique<LensProjection>(*other.lensProjection_);
    }
    if (other.scaling_) {
      scaling_ = std::make_unique<std::vector<double>>(*other.scaling_);
    }
    if (other.shift_) {
      shift_ = std::make_unique<std::vector<double>>(*other.shift_);
    }
    if (other.targetPosition_) {
      targetPosition_ =
          std::make_unique<::filament::math::float3>(*other.targetPosition_);
    }
    if (other.upVector_) {
      upVector_ = std::make_unique<::filament::math::float3>(*other.upVector_);
    }
    if (other.orbitHomePosition_) {
      orbitHomePosition_ =
          std::make_unique<::filament::math::float3>(*other.orbitHomePosition_);
    }
    if (other.flightStartPosition_) {
      flightStartPosition_ = std::make_unique<::filament::math::float3>(
          *other.flightStartPosition_);
    }
    if (other.flightStartOrientation_) {
      flightStartOrientation_ =
          std::make_unique<std::vector<float>>(*other.flightStartOrientation_);
    }
    if (other.orbitSpeed_) {
      orbitSpeed_ = std::make_unique<std::vector<float>>(*other.orbitSpeed_);
    }
  }

  Camera& operator=(const Camera&) = delete;

  [[nodiscard]] std::unique_ptr<Camera> clone() const {
    return std::make_unique<Camera>(
        *this);  // Calls the custom copy constructor
  }

  [[nodiscard]] static const char* getTextForMode(
      ::filament::camutils::Mode mode);

  [[nodiscard]] static ::filament::camutils::Mode getModeForText(
      const std::string& mode);

  [[nodiscard]] static const char* getTextForFov(::filament::camutils::Fov fov);

  [[nodiscard]] static ::filament::camutils::Fov getFovForText(
      const std::string& fov);

  friend class CameraManager;

  void vSetCurrentCameraOrbitAngle(float fValue) {
    fCurrentOrbitAngle_ = fValue;
    forceSingleFrameUpdate_ = true;
  }

  void vResetInertiaCameraToDefaultValues() {
    current_zoom_radius_ = flightStartPosition_->x;
    current_pitch_addition_ = 0;
    current_yaw_addition_ = 0;

    vSetCurrentCameraOrbitAngle(0.0f);
  }

 private:
  static constexpr char kModeOrbit[] = "ORBIT";
  static constexpr char kModeMap[] = "MAP";
  static constexpr char kModeFreeFlight[] = "FREE_FLIGHT";
  static constexpr char kFovVertical[] = "VERTICAL";
  static constexpr char kFovHorizontal[] = "HORIZONTAL";
  /// Auto orbit is a 'camera feature', where it will auto orbit around
  /// a targetPosition_ Camera features are updated from camera_manager.cc:
  /// updateCamerasFeatures() currently.
  static constexpr char kModeAutoOrbit[] = "AUTO_ORBIT";
  static constexpr char kModeInertiaAndGestures[] = "INERTIA_AND_GESTURES";

  enum CustomCameraMode { Unset, AutoOrbit, InertiaAndGestures };

  /// An object that control camera Exposure.
  std::unique_ptr<Exposure> exposure_;

  /// An object that controls camera projection matrix.
  std::unique_ptr<Projection> projection_;

  /// An object that control camera and set it's projection matrix from the
  /// focal length.
  std::unique_ptr<LensProjection> lensProjection_;

  /// Sets an additional matrix that scales the projection matrix.
  /// This is useful to adjust the aspect ratio of the camera independent from
  /// its projection.
  /// Its sent as List of 2 double elements :
  ///     * xscaling  horizontal scaling to be applied after the projection
  ///     matrix.
  //      * yscaling vertical scaling to be applied after the projection
  //      matrix.
  std::unique_ptr<std::vector<double>> scaling_;

  ///      Sets an additional matrix that shifts (translates) the projection
  ///      matrix.
  ///     The shift parameters are specified in NDC coordinates.
  /// Its sent as List of 2 double elements :
  ///      *  xshift    horizontal shift in NDC coordinates applied after the
  ///      projection
  ///      *  yshift    vertical shift in NDC coordinates applied after the
  ///      projection
  std::unique_ptr<std::vector<double>> shift_;

  /// Mode of the camera that operates on.
  ::filament::camutils::Mode mode_;
  /// if we have a mode specified not in filament - auto orbit, to texture, PiP
  CustomCameraMode eCustomCameraMode_;
  bool forceSingleFrameUpdate_;

  /// The world-space position of interest, which defaults to (x:0,y:0,z:-4).
  std::unique_ptr<::filament::math::float3> targetPosition_;

  /// The orientation for the home position, which defaults to (x:0,y:1,z:0).
  std::unique_ptr<::filament::math::float3> upVector_;

  /// The scroll delta multiplier, which defaults to 0.01.
  std::optional<float> zoomSpeed_;

  // orbit
  /// The initial eye position in world space for ORBIT mode & autoorbit mode
  /// This defaults to (x:0,y:0,z:1).
  std::unique_ptr<::filament::math::float3> orbitHomePosition_;
  // used with autoorbit mode for determining where to go next
  float fCurrentOrbitAngle_;

  /// Sets the multiplier with viewport delta for ORBIT mode.This defaults to
  /// 0.01 List of 2 double :[x,y]
  std::unique_ptr<std::vector<float>> orbitSpeed_;

  /// The FOV axis that's held constant when the viewport changes.
  /// This defaults to Vertical.
  ::filament::camutils::Fov fovDirection_;

  /// The full FOV (not the half-angle) in the degrees.
  /// This defaults to 33.
  std::optional<float> fovDegrees_;

  /// The distance to the far plane, which defaults to 5000.
  std::optional<float> farPlane_;

  /// The initial eye position in world space for FREE_FLIGHT mode.
  /// Defaults to (x:0,y:0,z:0).
  std::unique_ptr<::filament::math::float3> flightStartPosition_;

  /// The initial orientation in pitch and yaw for FREE_FLIGHT mode.
  /// Defaults to [0,0].
  std::unique_ptr<std::vector<float>> flightStartOrientation_;

  /// The maximum camera translation speed in world units per second for
  /// FREE_FLIGHT mode. Defaults to 10.
  std::optional<float> flightMaxMoveSpeed_;

  /// The number of speed steps adjustable with scroll wheel for FREE_FLIGHT
  /// mode.
  ///  Defaults to 80.
  std::optional<int> flightSpeedSteps_;

  /// Applies a deceleration to camera movement in FREE_FLIGHT mode. Defaults to
  /// 0 (no damping). Lower values give slower damping times. A good default
  /// is 15.0. Too high a value may lead to instability.
  std::optional<float> flightMoveDamping_;

  // how much ongoing rotation velocity effects, default 0.05
  double inertia_rotationSpeed_;

  // 0-1 how much of a flick distance / delta gets multiplied, default 0.2
  double inertia_velocityFactor_;

  // 0-1 larger number means it takes longer for it to decay, default 0.86
  double inertia_decayFactor_;

  // when panning the max angle we let them go to the edge L/R
  double pan_angleCapX_;

  // when panning the max angle we let them go to the edge U/D
  double pan_angleCapY_;

  // when zooming the limit they're able to go 'into' the object before unable
  // to go any further in
  double zoom_minCap_;

  // when zooming the limit they're able to go from the object before unable to
  // go any further out
  double zoom_maxCap_;

  // used by camera manager to go between zoom min and max cap.
  float current_zoom_radius_;

  // used with pan angle caps
  float current_pitch_addition_;

  // used with pan angle caps
  float current_yaw_addition_;
};
}  // namespace plugin_filament_view
