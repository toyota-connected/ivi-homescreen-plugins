/*
 * Copyright 2020 Toyota Connected North America
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
#include <flutter/standard_method_codec.h>

#include <optional>
#include <string>

#include "audioplayers_linux_plugin.h"

#include "plugins/common/common.h"

namespace audioplayers_linux_plugin {

using flutter::BasicMessageChannel;
using flutter::CustomEncodableValue;
using flutter::EncodableList;
using flutter::EncodableMap;
using flutter::EncodableValue;
using flutter::MethodCall;
using flutter::MethodResult;

/// The codec used by AudioPlayersApi.
const StandardMethodCodec& AudioPlayersApi::GetCodec() {
  return StandardMethodCodec::GetInstance();
}

// Sets up an instance of `AudioPlayersApi` to handle messages through the
// `binary_messenger`.
void AudioPlayersApi::SetUp(BinaryMessenger* binary_messenger,
                            AudioPlayersApi* api) {
  {
    const auto channel = std::make_unique<MethodChannel<>>(
        binary_messenger, "xyz.luan/audioplayers", &GetCodec());
    if (api != nullptr) {
      channel->SetMethodCallHandler([api](const MethodCall<>& methodCall,
                                          std::unique_ptr<MethodResult<>>
                                              result) {
        const auto& args = std::get_if<EncodableMap>(methodCall.arguments());
        std::string playerId;
        for (const auto& [fst, snd] : *args) {
          if ("playerId" == std::get<std::string>(fst) &&
              std::holds_alternative<std::string>(snd)) {
            playerId = std::get<std::string>(snd);
          }
        }
        if (playerId.empty()) {
          result->Error("LinuxAudioError",
                        "Call missing mandatory parameter playerId.",
                        EncodableValue());
          return;
        }

        try {
          if (methodCall.method_name() == "create") {
            api->Create(playerId, [&](std::optional<FlutterError>&& output) {
              if (output.has_value()) {
                result->Error("create", "failed", WrapError(output.value()));
                return;
              }
              result->Success(EncodableValue(1));
            });
            return;
          }

          auto player = AudioplayersLinuxPlugin::GetPlayer(playerId);
          if (!player) {
            result->Error(
                "LinuxAudioError",
                "Player has not yet been created or has already been disposed.",
                EncodableValue());
            return;
          }

          if (const auto& method_name = methodCall.method_name();
              method_name == "pause") {
            player->Pause();
          } else if (method_name == "resume") {
            player->Resume();
          } else if (method_name == "stop") {
            player->Stop();
          } else if (method_name == "release") {
            player->ReleaseMediaSource();
          } else if (method_name == "seek") {
            EncodableValue valuePosition;
            for (const auto& [fst, snd] : *args) {
              if ("position" == std::get<std::string>(fst)) {
                valuePosition = snd;
                break;
              }
            }
            int32_t set_position =
                valuePosition.IsNull()
                    ? static_cast<int32_t>(player->GetPosition().value_or(0))
                    : std::get<int32_t>(valuePosition);
            player->SetPosition(set_position);
          } else if (method_name == "setSourceUrl") {
            EncodableValue valueUrl;
            EncodableValue valueIsLocal;
            for (const auto& [fst, snd] : *args) {
              if ("url" == std::get<std::string>(fst)) {
                valueUrl = snd;
              } else if ("isLocal" == std::get<std::string>(fst))
                valueIsLocal = snd;
            }

            if (valueUrl.IsNull()) {
              result->Error("LinuxAudioError",
                            "Null URL received on setSourceUrl.",
                            EncodableValue());
              return;
            }
            auto url = std::get<std::string>(valueUrl);
            bool isLocal =
                !valueIsLocal.IsNull() && std::get<bool>(valueIsLocal);
            if (isLocal) {
              url = std::string("file://") + url;
            }
            player->SetSourceUrl(url);
          } else if (method_name == "getDuration") {
            auto optDuration = player->GetDuration();
            result->Success(optDuration.has_value()
                                ? EncodableValue(optDuration.value())
                                : EncodableValue());
            return;
          } else if (method_name == "setVolume") {
            EncodableValue valueVolume;
            for (const auto& [fst, snd] : *args) {
              if ("volume" == std::get<std::string>(fst)) {
                valueVolume = snd;
                break;
              }
            }
            double volume =
                valueVolume.IsNull() ? 1.0 : std::get<double>(valueVolume);
            player->SetVolume(volume);
          } else if (method_name == "getCurrentPosition") {
            auto optPosition = player->GetPosition();
            result->Success(optPosition.has_value()
                                ? EncodableValue(optPosition.value())
                                : EncodableValue());
            return;
          } else if (method_name == "setPlaybackRate") {
            EncodableValue valuePlaybackRate;
            for (const auto& [fst, snd] : *args) {
              if ("playbackRate" == std::get<std::string>(fst)) {
                valuePlaybackRate = snd;
                break;
              }
            }
            double playbackRate = valuePlaybackRate.IsNull()
                                      ? 1.0
                                      : std::get<double>(valuePlaybackRate);
            player->SetPlaybackRate(playbackRate);
          } else if (method_name == "setReleaseMode") {
            EncodableValue valueReleaseMode;
            for (const auto& [fst, snd] : *args) {
              if ("releaseMode" == std::get<std::string>(fst)) {
                valueReleaseMode = snd;
                break;
              }
            }
            std::string releaseMode =
                valueReleaseMode.IsNull()
                    ? std::string()
                    : std::get<std::string>(valueReleaseMode);
            if (releaseMode.empty()) {
              result->Error(
                  "LinuxAudioError",
                  "Error calling setReleaseMode, releaseMode cannot be null",
                  EncodableValue());
              return;
            }
            auto looping = releaseMode.find("loop") != std::string::npos;
            player->SetLooping(looping);
          } else if (method_name == "setPlayerMode") {
            // TODO check support for low latency mode:
            // https://gstreamer.freedesktop.org/documentation/additional/design/latency.html?gi-language=c
          } else if (method_name == "setBalance") {
            EncodableValue valueBalance;
            for (const auto& [fst, snd] : *args) {
              if ("balance" == std::get<std::string>(fst)) {
                valueBalance = snd;
                break;
              }
            }
            double balance =
                valueBalance.IsNull() ? 0.0f : std::get<double>(valueBalance);
            player->SetBalance(static_cast<float>(balance));
          } else if (method_name == "emitLog") {
            EncodableValue valueMessage;
            for (const auto& [fst, snd] : *args) {
              if ("message" == std::get<std::string>(fst)) {
                valueMessage = snd;
                break;
              }
            }
            std::string message = valueMessage.IsNull()
                                      ? ""
                                      : std::get<std::string>(valueMessage);
            player->OnLog(message.c_str());
          } else if (method_name == "emitError") {
            EncodableValue valueCode;
            EncodableValue valueMessage;
            for (const auto& [fst, snd] : *args) {
              if ("code" == std::get<std::string>(fst)) {
                valueCode = snd;
                break;
              }
              if ("message" == std::get<std::string>(fst)) {
                valueMessage = snd;
                break;
              }
            }

            auto code =
                valueCode.IsNull() ? "" : std::get<std::string>(valueCode);
            auto message = valueMessage.IsNull()
                               ? ""
                               : std::get<std::string>(valueMessage);
            player->OnError(code.c_str(), message.c_str(), nullptr, nullptr);
          } else if (method_name == "dispose") {
            player->Dispose();
            // TODO audioPlayers::erase(playerId);
          } else {
            SPDLOG_DEBUG("Unhandled: {}", method_name);
            result->NotImplemented();
            return;
          }
          result->Success();

        } catch (const gchar* error) {
          result->Error("LinuxAudioError", error, EncodableValue());
        } catch (const std::exception& e) {
          result->Error("LinuxAudioError", e.what(), EncodableValue());
        } catch (...) {
          result->Error("LinuxAudioError", "Unknown AudioPlayersLinux error",
                        EncodableValue());
        }
      });
    } else {
      channel->SetMethodCallHandler(nullptr);
    }
  }
}

EncodableValue AudioPlayersApi::WrapError(
    const std::string_view error_message) {
  return EncodableValue(
      EncodableList{EncodableValue(std::string(error_message)),
                    EncodableValue("Error"), EncodableValue()});
}

EncodableValue AudioPlayersApi::WrapError(const FlutterError& error) {
  return EncodableValue(EncodableList{EncodableValue(error.code()),
                                      EncodableValue(error.message()),
                                      error.details()});
}

/// The codec used by AudioPlayersApi.
const StandardMethodCodec& AudioPlayersGlobalApi::GetCodec() {
  return StandardMethodCodec::GetInstance();
}

// Sets up an instance of `AudioPlayersGlobalApi` to handle messages through the
// `binary_messenger`.
void AudioPlayersGlobalApi::SetUp(BinaryMessenger* binary_messenger,
                                  const AudioPlayersGlobalApi* api) {
  {
    const auto channel = std::make_unique<MethodChannel<>>(
        binary_messenger, "xyz.luan/audioplayers.global", &GetCodec());
    if (api != nullptr) {
      channel->SetMethodCallHandler(
          [](const MethodCall<>& call,
             std::unique_ptr<MethodResult<>> /* result */) {
            plugin_common::Encodable::PrintFlutterEncodableValue(
                "global", *call.arguments());
          });
    } else {
      channel->SetMethodCallHandler(nullptr);
    }
  }
}

EncodableValue AudioPlayersGlobalApi::WrapError(
    const std::string_view error_message) {
  return EncodableValue(
      EncodableList{EncodableValue(std::string(error_message)),
                    EncodableValue("Error"), EncodableValue()});
}

EncodableValue AudioPlayersGlobalApi::WrapError(const FlutterError& error) {
  return EncodableValue(EncodableList{EncodableValue(error.code()),
                                      EncodableValue(error.message()),
                                      error.details()});
}

}  // namespace audioplayers_linux_plugin
