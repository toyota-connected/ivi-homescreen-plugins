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

#ifndef PIGEON_MESSAGES_G_H_
#define PIGEON_MESSAGES_G_H_
#include <flutter/basic_message_channel.h>
#include <flutter/binary_messenger.h>
#include <flutter/encodable_value.h>
#include <flutter/standard_method_codec.h>

#include <map>
#include <optional>
#include <string>
#include <utility>

namespace audioplayers_linux_plugin {

// Generated class from Pigeon.

class FlutterError {
 public:
  explicit FlutterError(std::string code) : code_(std::move(code)) {}
  explicit FlutterError(std::string code, std::string message)
      : code_(std::move(code)), message_(std::move(message)) {}
  explicit FlutterError(std::string code,
                        std::string message,
                        flutter::EncodableValue details)
      : code_(std::move(code)),
        message_(std::move(message)),
        details_(std::move(details)) {}

  [[nodiscard]] const std::string& code() const { return code_; }
  [[nodiscard]] const std::string& message() const { return message_; }
  [[nodiscard]] const flutter::EncodableValue& details() const {
    return details_;
  }

 private:
  std::string code_;
  std::string message_;
  flutter::EncodableValue details_;
};

template <class T>
class ErrorOr {
 public:
  explicit ErrorOr(const T& rhs) : v_(rhs) {}
  explicit ErrorOr(const T&& rhs) : v_(std::move(rhs)) {}
  explicit ErrorOr(const FlutterError& rhs) : v_(rhs) {}
  explicit ErrorOr(const FlutterError&& rhs) : v_(std::move(rhs)) {}

  [[nodiscard]] bool has_error() const {
    return std::holds_alternative<FlutterError>(v_);
  }
  const T& value() const { return std::get<T>(v_); };
  [[nodiscard]] const FlutterError& error() const {
    return std::get<FlutterError>(v_);
  };

 private:
  friend class AudioPlayersApi;
  ErrorOr() = default;
  T TakeValue() && { return std::get<T>(std::move(v_)); }

  std::variant<T, FlutterError> v_;
};

// Generated interface from Pigeon that represents a handler of messages from
// Flutter.
class AudioPlayersApi {
 public:
  AudioPlayersApi(const AudioPlayersApi&) = delete;
  AudioPlayersApi& operator=(const AudioPlayersApi&) = delete;
  virtual ~AudioPlayersApi() = default;
  virtual void Create(
      const std::string& player_id,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void Dispose(
      const std::string& player_id,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void GetCurrentPosition(
      const std::string& player_id,
      std::function<void(ErrorOr<std::optional<int64_t>> reply)> result) = 0;
  virtual void GetDuration(
      const std::string& player_id,
      std::function<void(ErrorOr<std::optional<int64_t>> reply)> result) = 0;
  virtual void Pause(
      const std::string& player_id,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void Release(
      const std::string& player_id,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void Resume(
      const std::string& player_id,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void Seek(
      const std::string& player_id,
      int64_t position,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void SetBalance(
      const std::string& player_id,
      double balance,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void SetPlayerMode(
      const std::string& player_id,
      const std::string& player_mode,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void SetPlaybackRate(
      const std::string& player_id,
      double playback_rate,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void SetReleaseMode(
      const std::string& player_id,
      const std::string& release_mode,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void SetSourceBytes(
      const std::string& player_id,
      const std::vector<uint8_t>& bytes,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void SetSourceUrl(
      const std::string& player_id,
      const std::string& url,
      bool is_local,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void SetVolume(
      const std::string& player_id,
      double volume,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void Stop(
      const std::string& player_id,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void EmitLog(
      const std::string& player_id,
      const std::string& message,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void EmitError(
      const std::string& player_id,
      const std::string& code,
      const std::string& message,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;

  // The codec used by AudioPlayersApi.
  static const flutter::StandardMethodCodec& GetCodec();
  // Sets up an instance of `AudioPlayersApi` to handle messages through the
  // `binary_messenger`.
  static void SetUp(flutter::BinaryMessenger* binary_messenger,
                    AudioPlayersApi* api);
  static flutter::EncodableValue WrapError(std::string_view error_message);
  static flutter::EncodableValue WrapError(const FlutterError& error);

 protected:
  AudioPlayersApi() = default;
};

class AudioPlayersGlobalApi {
 public:
  AudioPlayersGlobalApi(const AudioPlayersGlobalApi&) = delete;
  AudioPlayersGlobalApi& operator=(const AudioPlayersGlobalApi&) = delete;
  virtual ~AudioPlayersGlobalApi() = default;

  virtual void SetAudioContextGlobal(
      const std::string& player_id,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void EmitLogGlobal(
      const std::string& player_id,
      const std::string& message,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  virtual void EmitErrorGlobal(
      const std::string& player_id,
      const std::string& code,
      const std::string& message,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;

  // The codec used by AudioPlayersApi.
  static const flutter::StandardMethodCodec& GetCodec();
  // Sets up an instance of `AudioPlayersGlobalApi` to handle messages through
  // the `binary_messenger`.
  static void SetUp(flutter::BinaryMessenger* binary_messenger,
                    const AudioPlayersGlobalApi* api);
  static flutter::EncodableValue WrapError(std::string_view error_message);
  static flutter::EncodableValue WrapError(const FlutterError& error);

 protected:
  AudioPlayersGlobalApi() = default;
};

}  // namespace audioplayers_linux_plugin
#endif  // PIGEON_MESSAGES_G_H_
