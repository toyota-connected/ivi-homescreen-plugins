/*
 * Copyright 2023-2024 Toyota Connected North America
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

#undef _HAS_EXCEPTIONS

#include "messages.h"

#include <flutter/basic_message_channel.h>
#include <flutter/binary_messenger.h>
#include <flutter/encodable_value.h>
#include <flutter/method_call.h>
#include <flutter/method_channel.h>

#include <string>

#include "plugins/common/common.h"
#include "rapidjson/writer.h"

namespace flatpak_plugin {

using flutter::BasicMessageChannel;
using flutter::CustomEncodableValue;
using flutter::EncodableList;
using flutter::EncodableMap;
using flutter::EncodableValue;
using flutter::MethodCall;
using flutter::MethodResult;

/// The codec used by DesktopWindowLinuxApi.
const flutter::JsonMethodCodec& FlatpakApi::GetCodec() {
  return flutter::JsonMethodCodec::GetInstance();
}

// Sets up an instance of `FlatpakApi` to handle messages through the
// `binary_messenger`.
void FlatpakApi::SetUp(flutter::BinaryMessenger* binary_messenger,
                       const FlatpakApi* api) {
  {
    auto const channel =
        std::make_unique<flutter::MethodChannel<rapidjson::Document>>(
            binary_messenger, "flutter/flatpak", &GetCodec());
    if (api != nullptr) {
      channel->SetMethodCallHandler(
          [](const MethodCall<rapidjson::Document>& /* call */,
             const std::unique_ptr<MethodResult<rapidjson::Document>>& result) {
            // const auto& method = call.method_name();
            // const auto args = call.arguments();
            result->Success();
          });
    } else {
      channel->SetMethodCallHandler(nullptr);
    }
  }
}

}  // namespace flatpak_plugin
