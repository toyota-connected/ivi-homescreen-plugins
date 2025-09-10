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

namespace plugin_filament_view {

// Deserialization
static constexpr char kId[] = "id";
static constexpr char kParentId[] = "parentId";
static constexpr char kName[] = "name";
static constexpr char kGuid[] = "guid";
static constexpr char kShapeType[] = "shapeType";
static constexpr char kSize[] = "size";
static constexpr char kPosition[] = "position";
static constexpr char kStartingPosition[] = "startingPosition";
static constexpr char kNormal[] = "normal";
static constexpr char kScale[] = "scale";
static constexpr char kRotation[] = "rotation";
static constexpr char kIsWireframe[] = "isWireframe";
static constexpr char kParams[] = "params";
static constexpr char kMaterial[] = "material";
static constexpr char kCullingEnabled[] = "cullingEnabled";
static constexpr char kReceiveShadows[] = "receiveShadows";
static constexpr char kCastShadows[] = "castShadows";
static constexpr char kDirection[] = "direction";
static constexpr char kLength[] = "length";
static constexpr char kModels[] = "models";
static constexpr char kCameras[] = "cameras";
static constexpr char kEntities[] = "entities";
static constexpr char kComponents[] = "components";
static constexpr char kSystems[] = "systems";
static constexpr char kFallback[] = "fallback";
static constexpr char kScene[] = "scene";
static constexpr char kShapes[] = "shapes";
static constexpr char kSkybox[] = "skybox";
static constexpr char kLight[] = "light";
static constexpr char kLights[] = "lights";
static constexpr char kIndirectLight[] = "indirectLight";
static constexpr char kCamera[] = "camera";
static constexpr char kExposure[] = "exposure";
static constexpr char kProjection[] = "projection";
static constexpr char kLensProjection[] = "lensProjection";
static constexpr char kOrbitOriginEntity[] = "orbitOriginEntity";
static constexpr char kDollyOffset[] = "dollyOffset";
static constexpr char kOrbitRotation[] = "orbitRotation";
static constexpr char kTargetEntity[] = "targetEntity";
static constexpr char kTargetPoint[] = "targetPoint";
static constexpr char kViewId[] = "viewId";
static constexpr char kFlightMaxMoveSpeed[] = "flightMaxMoveSpeed";
static constexpr char kFlightMoveDamping[] = "flightMoveDamping";
static constexpr char kFlightSpeedSteps[] = "flightSpeedSteps";
static constexpr char kFlightStartOrientation[] = "flightStartOrientation";
static constexpr char kFlightStartPosition[] = "flightStartPosition";
static constexpr char kFovDirection[] = "fovDirection";
static constexpr char kFovDegrees[] = "fovDegrees";
static constexpr char kFarPlane[] = "farPlane";
static constexpr char kMode[] = "mode";
static constexpr char kScaling[] = "scaling";
static constexpr char kShift[] = "shift";
static constexpr char kUpVector[] = "upVector";
static constexpr char kZoomSpeed[] = "zoomSpeed";
static constexpr char kFocalLength[] = "focalLength";
static constexpr char kAspect[] = "aspect";
static constexpr char kNear[] = "near";
static constexpr char kFar[] = "far";
static constexpr char kAperture[] = "aperture";
static constexpr char kSensitivity[] = "sensitivity";
static constexpr char kShutterSpeed[] = "shutterSpeed";
static constexpr char kLeft[] = "left";
static constexpr char kRight[] = "right";
static constexpr char kBottom[] = "bottom";
static constexpr char kTop[] = "top";
static constexpr char kFovInDegrees[] = "fovInDegrees";
static constexpr char kAutoPlay[] = "autoPlay";
static constexpr char kIndex[] = "index";
static constexpr char kAnimation[] = "animation";
static constexpr char kLoop[] = "loop";
static constexpr char kResetToTPoseOnReset[] = "resetToTPoseOnReset";
static constexpr char kPlaybackSpeed[] = "playbackSpeed";
static constexpr char kNotifyOfAnimationEvents[] = "notifyOfAnimationEvents";
static constexpr char kType[] = "type";
static constexpr char kColor[] = "color";
static constexpr char kColorTemperature[] = "colorTemperature";
static constexpr char kIntensity[] = "intensity";
static constexpr char kCastLight[] = "castLight";
static constexpr char kFalloffRadius[] = "falloffRadius";
static constexpr char kSpotLightConeInner[] = "spotLightConeInner";
static constexpr char kSpotLightConeOuter[] = "spotLightConeOuter";
static constexpr char kSunAngularRadius[] = "sunAngularRadius";
static constexpr char kSunHaloSize[] = "sunHaloSize";
static constexpr char kSunHaloFalloff[] = "sunHaloFalloff";

// specific collider values:
static constexpr char kCollider[] = "collider";
static constexpr char kColliderShapeType[] = "collider_shapeType";
static constexpr char kColliderExtents[] = "collider_extentSize";
static constexpr char kColliderIsStatic[] = "collider_isStatic";
static constexpr char kColliderLayer[] = "collider_layer";
static constexpr char kColliderMask[] = "collider_mask";
static constexpr char kColliderShouldMatchAttachedObject[] = "collider_shouldMatchAttachedObject";

// Custom model viewer for sending frames to dart.
static constexpr char kUpdateFrame[] = "updateFrame";
static constexpr char kPreRenderFrame[] = "preRenderFrame";
static constexpr char kPostRenderFrame[] = "postRenderFrame";
static constexpr char kParam_DeltaTime[] = "deltaTime";
static constexpr char kParam_FPS[] = "fps";
static constexpr char kParam_ElapsedFrameTime[] = "elapsedFrameTime";
static constexpr char kParam_cpuFrametime[] = "cpuFt";
static constexpr char kParam_gpuFrametime[] = "gpuFt";

// Collision Manager and uses, sending messages to dart from native
static constexpr char kCollisionEvent[] = "collision_event";
static constexpr char kCollisionEventSourceGuid[] = "collision_event_source";
static constexpr char kCollisionEventHitCount[] = "collision_event_hit_count";
static constexpr char kCollisionEventHitResult[] = "collision_event_hit_result_";
static constexpr char kCollisionEventType[] = "collision_event_type";
enum CollisionEventType {
  eFromNonNative,
  eNativeOnTouchBegin,
  eNativeOnTouchHeld,
  eNativeOnTouchEnd
};

static constexpr char kAnimationEvent[] = "animation_event";
static constexpr char kAnimationEventType[] = "animation_event_type";
enum AnimationEventType {
  eAnimationStarted,
  eAnimationEnded,
  // might want paused, un-paused, speed change.
};
static constexpr char kAnimationEventData[] = "animation_event_data";

static constexpr char kCamera_Inertia_RotationSpeed[] = "inertia_rotationSpeed";
static constexpr char kCamera_Inertia_VelocityFactor[] = "inertia_velocityFactor";
static constexpr char kCamera_Inertia_DecayFactor[] = "inertia_decayFactor";

static constexpr char kCamera_Pan_angleCapX[] = "pan_angleCapX";
static constexpr char kCamera_Pan_angleCapY[] = "pan_angleCapY";
static constexpr char kCamera_Zoom_minCap[] = "zoom_minCap";
static constexpr char kCamera_Zoom_maxCap[] = "zoom_maxCap";

// Configuration values stored in ecs for easier lookup
static constexpr char kAssetPath[] = "assetPath";
static constexpr char kIsGlb[] = "isGlb";
static constexpr char kModelInstancingMode[] = "instancingMode";

}  // namespace plugin_filament_view
