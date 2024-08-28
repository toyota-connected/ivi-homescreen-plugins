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

#include "messages.g.h"

#include <map>
#include <optional>
#include <sstream>
#include <string>

#include <flutter/basic_message_channel.h>
#include <flutter/binary_messenger.h>
#include <flutter/encodable_value.h>
#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>

#include "plugins/common/common.h"

using flutter::EncodableList;
using flutter::EncodableMap;
using flutter::EncodableValue;
using flutter::MessageReply;
using flutter::MethodCall;
using flutter::MethodResult;

namespace plugin_filament_view {

void FilamentViewApi::SetUp(flutter::BinaryMessenger* binary_messenger,
                            FilamentViewApi* api,
                            int32_t id) {
  {
    std::stringstream ss;
    ss << "io.sourcya.playx.3d.scene.channel_" << id;
    const auto channel =
        std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
            binary_messenger, ss.str().c_str(),
            &flutter::StandardMethodCodec::GetInstance());
    if (api != nullptr) {
      channel->SetMethodCallHandler(
          [api](const MethodCall<EncodableValue>& methodCall,
                std::unique_ptr<MethodResult<EncodableValue>> result) {
            spdlog::trace("[{}]", methodCall.method_name());

            static constexpr char kChangeAnimationByIndex[] =
                "CHANGE_ANIMATION_BY_INDEX";

            static constexpr char kChangeLightColorByIndex[] =
                "CHANGE_DIRECT_LIGHT_COLOR_BY_INDEX";
            static constexpr char kChangeLightColorByIndexKey[] =
                "CHANGE_DIRECT_LIGHT_COLOR_BY_INDEX_KEY";
            static constexpr char kChangeLightColorByIndexColor[] =
                "CHANGE_DIRECT_LIGHT_COLOR_BY_INDEX_COLOR";
            static constexpr char kChangeLightColorByIndexIntensity[] =
                "CHANGE_DIRECT_LIGHT_COLOR_BY_INDEX_INTENSITY";

            static constexpr char kToggleShapesInScene[] =
                "TOGGLE_SHAPES_IN_SCENE";
            static constexpr char kToggleShapesInSceneValue[] =
                "TOGGLE_SHAPES_IN_SCENE_VALUE";

            static constexpr char kToggleCameraAutoRotate[] =
                "TOGGLE_CAMERA_AUTO_ROTATE";
            static constexpr char kToggleCameraAutoRotateValue[] =
                "TOGGLE_CAMERA_AUTO_ROTATE_VALUE";
            static constexpr char kChangeCameraRotation[] = "ROTATE_CAMERA";
            static constexpr char kChangeCameraRotationValue[] =
                "ROTATE_CAMERA_VALUE";

            if (methodCall.method_name() == kChangeAnimationByIndex) {
              result->Success();
            } else if (methodCall.method_name() == kChangeLightColorByIndex) {
              const auto& args =
                  std::get_if<EncodableMap>(methodCall.arguments());
              int32_t index;
              std::string colorString;
              int32_t intensity = 0;
              for (auto& it : *args) {
                if (kChangeLightColorByIndexColor ==
                        std::get<std::string>(it.first) &&
                    std::holds_alternative<std::string>(it.second)) {
                  colorString = std::get<std::string>(it.second);
                } else if (kChangeLightColorByIndexKey ==
                               std::get<std::string>(it.first) &&
                           std::holds_alternative<int32_t>(it.second)) {
                  index = std::get<int32_t>(it.second);
                } else if (kChangeLightColorByIndexIntensity ==
                               std::get<std::string>(it.first) &&
                           std::holds_alternative<int32_t>(it.second)) {
                  intensity = std::get<int32_t>(it.second);
                }
              }
              api->ChangeDirectLightByIndex(index, colorString, intensity,
                                            nullptr);

              result->Success();
            } else if (methodCall.method_name() == kToggleShapesInScene) {
              const auto& args =
                  std::get_if<EncodableMap>(methodCall.arguments());
              for (auto& it : *args) {
                if (kToggleShapesInSceneValue ==
                    std::get<std::string>(it.first)) {
                  bool bValue = std::get<bool>(it.second);
                  api->ToggleShapesInScene(bValue, nullptr);
                }
              }
              result->Success();
            } else if (methodCall.method_name() == kToggleCameraAutoRotate) {
              const auto& args =
                  std::get_if<EncodableMap>(methodCall.arguments());
              for (auto& it : *args) {
                if (kToggleCameraAutoRotateValue ==
                    std::get<std::string>(it.first)) {
                  bool bValue = std::get<bool>(it.second);
                  api->ToggleCameraAutoRotate(bValue, nullptr);
                }
              }
              result->Success();
            } else if (methodCall.method_name() == kChangeCameraRotation) {
              const auto& args =
                  std::get_if<EncodableMap>(methodCall.arguments());
              for (auto& it : *args) {
                if (kChangeCameraRotationValue ==
                    std::get<std::string>(it.first)) {
                  auto fValue = static_cast<float>(std::get<double>(it.second));
                  api->SetCameraRotation(fValue, nullptr);
                }
              }
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
                                 FilamentViewApi* api,
                                 int32_t id) {
  {
    std::stringstream ss;
    ss << "io.sourcya.playx.3d.scene.model_state_channel_" << id;
    const auto channel =
        std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
            binary_messenger, ss.str().c_str(),
            &flutter::StandardMethodCodec::GetInstance());
    if (api != nullptr) {
      channel->SetMethodCallHandler(
          [](const MethodCall<EncodableValue>& methodCall,
             std::unique_ptr<MethodResult<EncodableValue>> result) {
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
                          FilamentViewApi* api,
                          int32_t id) {
  {
    std::stringstream ss;
    ss << "io.sourcya.playx.3d.scene.scene_state_" << id;
    const auto channel =
        std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
            binary_messenger, ss.str().c_str(),
            &flutter::StandardMethodCodec::GetInstance());
    if (api != nullptr) {
      channel->SetMethodCallHandler(
          [](const MethodCall<EncodableValue>& methodCall,
             std::unique_ptr<MethodResult<EncodableValue>> result) {
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
                          FilamentViewApi* api,
                          int32_t id) {
  {
    std::stringstream ss;
    ss << "io.sourcya.playx.3d.scene.shape_state_" << id;
    const auto channel =
        std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
            binary_messenger, ss.str().c_str(),
            &flutter::StandardMethodCodec::GetInstance());
    if (api != nullptr) {
      channel->SetMethodCallHandler(
          [](const MethodCall<EncodableValue>& methodCall,
             std::unique_ptr<MethodResult<EncodableValue>> result) {
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
                               FilamentViewApi* api,
                               int32_t id) {
  {
    std::stringstream ss;
    ss << "io.sourcya.playx.3d.scene.renderer_channel_" << id;
    const auto channel =
        std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
            binary_messenger, ss.str().c_str(),
            &flutter::StandardMethodCodec::GetInstance());
    if (api != nullptr) {
      channel->SetMethodCallHandler(
          [](const MethodCall<EncodableValue>& methodCall,
             std::unique_ptr<MethodResult<EncodableValue>> result) {
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
