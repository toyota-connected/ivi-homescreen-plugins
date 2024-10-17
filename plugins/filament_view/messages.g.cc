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

#include "messages.g.h"

#include <map>
#include <sstream>
#include <string>

#include <core/include/literals.h>
#include <core/scene/geometry/ray.h>
#include <core/systems/derived/collision_system.h>
#include <core/systems/ecsystems_manager.h>
#include <core/systems/messages/ecs_message.h>

#include <flutter/basic_message_channel.h>
#include <flutter/binary_messenger.h>
#include <flutter/encodable_value.h>
#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>
#include <plugins/common/common.h>

using flutter::EncodableList;
using flutter::EncodableMap;
using flutter::EncodableValue;
using flutter::MessageReply;
using flutter::MethodCall;
using flutter::MethodResult;

namespace plugin_filament_view {

void FilamentViewApi::SetUp(flutter::BinaryMessenger* binary_messenger,
                            FilamentViewApi* api,
                            const int32_t id) {
  {
    std::stringstream ss;
    ss << "io.sourcya.playx.3d.scene.channel_" << id;
    const auto channel = std::make_unique<flutter::MethodChannel<>>(
        binary_messenger, ss.str().c_str(),
        &flutter::StandardMethodCodec::GetInstance());
    if (api != nullptr) {
      channel->SetMethodCallHandler([api](const MethodCall<>& methodCall,
                                          std::unique_ptr<MethodResult<>>
                                              result) {
        spdlog::trace("[{}]", methodCall.method_name());

        if (methodCall.method_name() == kChangeAnimationByIndex) {
          result->Success();
        } else if (methodCall.method_name() == kChangeLightColorByIndex) {
          const auto& args = std::get_if<EncodableMap>(methodCall.arguments());
          int32_t index = 0;
          std::string colorString;
          int32_t intensity = 0;
          for (const auto& [fst, snd] : *args) {
            if (kChangeLightColorByIndexColor == std::get<std::string>(fst) &&
                std::holds_alternative<std::string>(snd)) {
              colorString = std::get<std::string>(snd);
            } else if (kChangeLightColorByIndexKey ==
                           std::get<std::string>(fst) &&
                       std::holds_alternative<int32_t>(snd)) {
              index = std::get<int32_t>(snd);
            } else if (kChangeLightColorByIndexIntensity ==
                           std::get<std::string>(fst) &&
                       std::holds_alternative<int32_t>(snd)) {
              intensity = std::get<int32_t>(snd);
            }
          }
          api->ChangeDirectLightByIndex(index, colorString, intensity, nullptr);

          result->Success();
        } else if (methodCall.method_name() == kToggleShapesInScene) {
          const auto& args = std::get_if<EncodableMap>(methodCall.arguments());
          for (const auto& [fst, snd] : *args) {
            if (kToggleShapesInSceneValue == std::get<std::string>(fst)) {
              bool bValue = std::get<bool>(snd);
              api->ToggleShapesInScene(bValue, nullptr);
            }
          }
          result->Success();
        } else if (methodCall.method_name() ==
                   kToggleCollidableVisualsInScene) {
          const auto& args = std::get_if<EncodableMap>(methodCall.arguments());
          for (const auto& [fst, snd] : *args) {
            if (kToggleCollidableVisualsInSceneValue ==
                std::get<std::string>(fst)) {
              bool bValue = std::get<bool>(snd);
              api->ToggleDebugCollidableViewsInScene(bValue, nullptr);
            }
          }
          result->Success();
        } else if (methodCall.method_name() == kChangeCameraMode) {
          const auto& args = std::get_if<EncodableMap>(methodCall.arguments());
          for (const auto& [fst, snd] : *args) {
            if (kChangeCameraModeValue == std::get<std::string>(fst)) {
              auto szValue = std::get<std::string>(snd);
              api->ChangeCameraMode(szValue, nullptr);
            }
          }
          result->Success();
        } else if (methodCall.method_name() ==
                   kResetInertiaCameraToDefaultValues) {
          api->vResetInertiaCameraToDefaultValues(nullptr);
          result->Success();
        } else if (methodCall.method_name() == kChangeCameraRotation) {
          const auto& args = std::get_if<EncodableMap>(methodCall.arguments());
          for (const auto& [fst, snd] : *args) {
            if (kChangeCameraRotationValue == std::get<std::string>(fst)) {
              auto fValue = static_cast<float>(std::get<double>(snd));
              api->SetCameraRotation(fValue, nullptr);
            }
          }
          result->Success();
        } else if (methodCall.method_name() == kCollisionRayRequest) {
          const auto& args = std::get_if<EncodableMap>(methodCall.arguments());
          filament::math::float3 origin(0);
          filament::math::float3 direction(0);
          float length;
          std::string guidForReferenceLookup;
          for (const auto& [fst, snd] : *args) {
            if (kCollisionRayRequestOriginX == std::get<std::string>(fst)) {
              origin.x = static_cast<float>(std::get<double>(snd));
            } else if (kCollisionRayRequestOriginY ==
                       std::get<std::string>(fst)) {
              origin.y = static_cast<float>(std::get<double>(snd));
            } else if (kCollisionRayRequestOriginZ ==
                       std::get<std::string>(fst)) {
              origin.z = static_cast<float>(std::get<double>(snd));
            } else if (kCollisionRayRequestDirectionX ==
                       std::get<std::string>(fst)) {
              direction.x = static_cast<float>(std::get<double>(snd));
            } else if (kCollisionRayRequestDirectionY ==
                       std::get<std::string>(fst)) {
              direction.y = static_cast<float>(std::get<double>(snd));
            } else if (kCollisionRayRequestDirectionZ ==
                       std::get<std::string>(fst)) {
              direction.z = static_cast<float>(std::get<double>(snd));
            } else if (kCollisionRayRequestLength ==
                       std::get<std::string>(fst)) {
              length = static_cast<float>(std::get<double>(snd));
            } else if (kCollisionRayRequestGUID == std::get<std::string>(fst)) {
              guidForReferenceLookup = std::get<std::string>(snd);
            }
          }

          // this is an async call,  we won't return results
          // in-line here.
          Ray rayInfo(origin, direction, length);

          ECSMessage rayInformation;
          rayInformation.addData(ECSMessageType::DebugLine, rayInfo);
          ECSystemManager::GetInstance()->vRouteMessage(rayInformation);

          ECSMessage collisionRequest;
          collisionRequest.addData(ECSMessageType::CollisionRequest, rayInfo);
          collisionRequest.addData(ECSMessageType::CollisionRequestRequestor,
                                   guidForReferenceLookup);
          collisionRequest.addData(ECSMessageType::CollisionRequestType,
                                   eFromNonNative);
          ECSystemManager::GetInstance()->vRouteMessage(collisionRequest);

          result->Success();
        } else {
          result->NotImplemented();
        }
      });
    } else {
      channel->SetMethodCallHandler(nullptr);
    }
  }
}

void ModelStateChannelApi::SetUp(flutter::BinaryMessenger* binary_messenger,
                                 const FilamentViewApi* api,
                                 const int32_t id) {
  {
    std::stringstream ss;
    ss << "io.sourcya.playx.3d.scene.model_state_channel_" << id;
    const auto channel = std::make_unique<flutter::MethodChannel<>>(
        binary_messenger, ss.str().c_str(),
        &flutter::StandardMethodCodec::GetInstance());
    if (api != nullptr) {
      channel->SetMethodCallHandler([](const MethodCall<>& methodCall,
                                       std::unique_ptr<MethodResult<>> result) {
        if (methodCall.method_name() == "listen") {
          result->Success();
        } else {
          spdlog::error("[{}]", methodCall.method_name());
          result->NotImplemented();
        }
      });
    } else {
      channel->SetMethodCallHandler(nullptr);
    }
  }
}

void SceneStateApi::SetUp(flutter::BinaryMessenger* binary_messenger,
                          const FilamentViewApi* api,
                          const int32_t id) {
  {
    std::stringstream ss;
    ss << "io.sourcya.playx.3d.scene.scene_state_" << id;
    const auto channel = std::make_unique<flutter::MethodChannel<>>(
        binary_messenger, ss.str().c_str(),
        &flutter::StandardMethodCodec::GetInstance());
    if (api != nullptr) {
      channel->SetMethodCallHandler([](const MethodCall<>& methodCall,
                                       std::unique_ptr<MethodResult<>> result) {
        if (methodCall.method_name() == "listen") {
          result->Success();
        } else {
          spdlog::error("[{}]", methodCall.method_name());
          result->NotImplemented();
        }
      });
    } else {
      channel->SetMethodCallHandler(nullptr);
    }
  }
}

void ShapeStateApi::SetUp(flutter::BinaryMessenger* binary_messenger,
                          const FilamentViewApi* api,
                          const int32_t id) {
  {
    std::stringstream ss;
    ss << "io.sourcya.playx.3d.scene.shape_state_" << id;
    const auto channel = std::make_unique<flutter::MethodChannel<>>(
        binary_messenger, ss.str().c_str(),
        &flutter::StandardMethodCodec::GetInstance());
    if (api != nullptr) {
      channel->SetMethodCallHandler([](const MethodCall<>& methodCall,
                                       std::unique_ptr<MethodResult<>> result) {
        if (methodCall.method_name() == "listen") {
          result->Success();
        } else {
          spdlog::error("[{}]", methodCall.method_name());
          result->NotImplemented();
        }
      });
    } else {
      channel->SetMethodCallHandler(nullptr);
    }
  }
}

void RendererChannelApi::SetUp(flutter::BinaryMessenger* binary_messenger,
                               const FilamentViewApi* api,
                               const int32_t id) {
  {
    std::stringstream ss;
    ss << "io.sourcya.playx.3d.scene.renderer_channel_" << id;
    const auto channel = std::make_unique<flutter::MethodChannel<>>(
        binary_messenger, ss.str().c_str(),
        &flutter::StandardMethodCodec::GetInstance());
    if (api != nullptr) {
      channel->SetMethodCallHandler([](const MethodCall<>& methodCall,
                                       std::unique_ptr<MethodResult<>> result) {
        if (methodCall.method_name() == "listen") {
          result->Success();
        } else {
          spdlog::error("[{}]", methodCall.method_name());
          result->NotImplemented();
        }
      });
    } else {
      channel->SetMethodCallHandler(nullptr);
    }
  }
}

}  // namespace plugin_filament_view
