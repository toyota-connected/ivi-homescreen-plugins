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

#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"

#include <filament/LightManager.h>

namespace plugin_filament_view {
class Light {
 public:
  explicit Light(float colorTemperature = 6'500.0f,
                 float intensity = 100'000.0f,
                 ::filament::math::float3 direction = {0.0f, -1.0f, 0.0f},
                 bool castShadows = true);

  explicit Light(const flutter::EncodableMap& params);

  void DebugPrint(const char* tag);

  static ::filament::LightManager::Type textToLightType(
      const std::string& type);

  static const char* lightTypeToText(::filament::LightManager::Type type);

  // Copy constructor for shallow copy
  Light(const Light& other)
      : type_(other.type_),
        color_(other.color_),
        colorTemperature_(other.colorTemperature_),
        intensity_(other.intensity_),
        position_(other.position_ ? std::make_unique<::filament::math::float3>(
                                        *other.position_)
                                  : nullptr),
        direction_(
            other.direction_
                ? std::make_unique<::filament::math::float3>(*other.direction_)
                : nullptr),
        castLight_(other.castLight_),
        castShadows_(other.castShadows_),
        falloffRadius_(other.falloffRadius_),
        spotLightConeInner_(other.spotLightConeInner_),
        spotLightConeOuter_(other.spotLightConeOuter_),
        sunAngularRadius_(other.sunAngularRadius_),
        sunHaloSize_(other.sunHaloSize_),
        sunHaloFalloff_(other.sunHaloFalloff_) {}

  // Copy assignment operator for shallow copy
  Light& operator=(const Light& other) {
    if (this != &other) {
      type_ = other.type_;
      color_ = other.color_;
      colorTemperature_ = other.colorTemperature_;
      intensity_ = other.intensity_;
      position_ =
          other.position_
              ? std::make_unique<::filament::math::float3>(*other.position_)
              : nullptr;
      direction_ =
          other.direction_
              ? std::make_unique<::filament::math::float3>(*other.direction_)
              : nullptr;
      castLight_ = other.castLight_;
      castShadows_ = other.castShadows_;
      falloffRadius_ = other.falloffRadius_;
      spotLightConeInner_ = other.spotLightConeInner_;
      spotLightConeOuter_ = other.spotLightConeOuter_;
      sunAngularRadius_ = other.sunAngularRadius_;
      sunHaloSize_ = other.sunHaloSize_;
      sunHaloFalloff_ = other.sunHaloFalloff_;
    }
    return *this;
  }

  friend class LightSystem;

  void ChangeColor(const std::string& color) { color_ = color; }

  void ChangeIntensity(float fIntensity) { intensity_ = fIntensity; }

 private:
  ::filament::LightManager::Type type_;
  std::optional<std::string> color_;
  std::optional<float> colorTemperature_;
  std::optional<float> intensity_;
  std::unique_ptr<::filament::math::float3> position_;
  std::unique_ptr<::filament::math::float3> direction_;
  std::optional<bool> castLight_;
  std::optional<bool> castShadows_;
  std::optional<float> falloffRadius_;
  std::optional<float> spotLightConeInner_;
  std::optional<float> spotLightConeOuter_;
  std::optional<float> sunAngularRadius_;
  std::optional<float> sunHaloSize_;
  std::optional<float> sunHaloFalloff_;
};
}  // namespace plugin_filament_view