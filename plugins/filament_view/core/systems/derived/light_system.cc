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

#include "light_system.h"

#include <core/include/color.h>
#include <core/systems/derived/filament_system.h>
#include <core/systems/ecsystems_manager.h>
#include <filament/Color.h>
#include <filament/LightManager.h>
#include <plugins/common/common.h>
#include <asio/post.hpp>

namespace plugin_filament_view {

using filament::math::float3;
using filament::math::mat3f;
using filament::math::mat4f;

////////////////////////////////////////////////////////////////////////////////////
void LightSystem::setDefaultLight() {
  SPDLOG_TRACE("++LightManager::setDefaultLight");
  defaultlight_ = std::make_unique<Light>();
  changeLight(defaultlight_.get());
  // f.wait();
  // light.reset();
  // SPDLOG_TRACE("--LightManager::setDefaultLight: {}", f.get().getMessage());
}

////////////////////////////////////////////////////////////////////////////////////
std::future<Resource<std::string_view>> LightSystem::changeLight(Light* light) {
  SPDLOG_TRACE("++{}::{}", __FILE__, __FUNCTION__);

  // Use the copy assignment operator to copy the contents
  *defaultlight_ = *light;

  const asio::io_context::strand& strand_(
      *ECSystemManager::GetInstance()->GetStrand());

  if (entityLight_.isNull()) {
    post(strand_, [&] {
      const auto filamentSystem =
          ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
              FilamentSystem::StaticGetTypeID(), "changeLight");
      const auto engine = filamentSystem->getFilamentEngine();

      entityLight_ = engine->getEntityManager().create();
    });
  }

  const auto promise(
      std::make_shared<std::promise<Resource<std::string_view>>>());
  auto future(promise->get_future());

  if (!light) {
    promise->set_value(
        Resource<std::string_view>::Error("Light type must be provided"));
    SPDLOG_TRACE("--LightManager::changeLight");
    return future;
  }

  post(strand_, [&, promise, light] {
    auto builder = filament::LightManager::Builder(light->type_);

    if (light->color_.has_value()) {
      auto colorValue = colorOf(light->color_.value());
      builder.color({colorValue[0], colorValue[1], colorValue[2]});
    } else if (light->colorTemperature_.has_value()) {
      auto cct = filament::Color::cct(light->colorTemperature_.value());
      auto red = cct.r;
      auto green = cct.g;
      auto blue = cct.b;
      builder.color({red, green, blue});
    }
    if (light->intensity_.has_value()) {
      builder.intensity(light->intensity_.value());
    }
    if (light->position_) {
      builder.position(*light->position_);
    }
    if (light->direction_) {
      // Note if Direction is 0,0,0 and you're on a spotlight
      // nothing will show.
      if (*light->direction_ == float3(0, 0, 0) &&
          light->type_ == filament::LightManager::Type::SPOT) {
        spdlog::warn(
            "You've created a spot light without a direction, nothing will "
            "show. Undefined behavior.");
      }

      builder.direction(*light->direction_);
    }
    if (light->castLight_.has_value()) {
      builder.castLight(light->castLight_.value());
    }
    if (light->castShadows_.has_value()) {
      builder.castShadows(light->castShadows_.value());
    }
    if (light->falloffRadius_.has_value()) {
      builder.falloff(light->falloffRadius_.value());
    }
    if (light->spotLightConeInner_.has_value() &&
        light->spotLightConeOuter_.has_value()) {
      builder.spotLightCone(light->spotLightConeInner_.value(),
                            light->spotLightConeOuter_.value());
    }
    if (light->sunAngularRadius_.has_value()) {
      builder.sunAngularRadius(light->sunAngularRadius_.value());
    }
    if (light->sunHaloSize_.has_value()) {
      builder.sunHaloSize(light->sunHaloSize_.value());
    }
    if (light->sunHaloFalloff_.has_value()) {
      builder.sunHaloSize(light->sunHaloFalloff_.value());
    }

    auto filamentSystem =
        ECSystemManager::GetInstance()->poGetSystemAs<FilamentSystem>(
            FilamentSystem::StaticGetTypeID(), "lightManager::changelight");

    const auto engine = filamentSystem->getFilamentEngine();

    builder.build(*engine, entityLight_);

    auto scene = filamentSystem->getFilamentScene();

    // this remove looks sus; seems like it should be the first thing
    // in the function, todo investigate.
    scene->removeEntities(&entityLight_, 1);

    scene->addEntity(entityLight_);
    promise->set_value(
        Resource<std::string_view>::Success("Light created Successfully"));
  });
  SPDLOG_TRACE("--{}::{}", __FILE__, __FUNCTION__);
  return future;
}

////////////////////////////////////////////////////////////////////////////////////
void LightSystem::vInitSystem() {
  setDefaultLight();

  vRegisterMessageHandler(
      ECSMessageType::ChangeSceneLightProperties,
      [this](const ECSMessage& msg) {
        spdlog::debug("ChangeSceneLightProperties");

        const auto colorValue = msg.getData<std::string>(
            ECSMessageType::ChangeSceneLightPropertiesColorValue);

        const auto intensityValue = msg.getData<float>(
            ECSMessageType::ChangeSceneLightPropertiesIntensity);

        defaultlight_->ChangeColor(colorValue);
        defaultlight_->ChangeIntensity(intensityValue);

        changeLight(defaultlight_.get());

        spdlog::debug("ChangeSceneLightProperties Complete");
      });
}

////////////////////////////////////////////////////////////////////////////////////
void LightSystem::vUpdate(float /*fElapsedTime*/) {}

////////////////////////////////////////////////////////////////////////////////////
void LightSystem::vShutdownSystem() {
  defaultlight_.reset();
}

////////////////////////////////////////////////////////////////////////////////////
void LightSystem::DebugPrint() {
  spdlog::debug("{}::{}", __FILE__, __FUNCTION__);
}
}  // namespace plugin_filament_view