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

#ifndef PIGEON_MESSAGES_G_H_
#define PIGEON_MESSAGES_G_H_
#include <flutter/binary_messenger.h>
#include <flutter/encodable_value.h>
#include <flutter/standard_message_codec.h>

#include <map>
#include <optional>
#include <string>
#include <utility>

namespace camera_plugin {

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
  explicit ErrorOr(const FlutterError&& rhs) : v_(rhs) {}

  [[nodiscard]] bool has_error() const {
    return std::holds_alternative<FlutterError>(v_);
  }
  const T& value() const { return std::get<T>(v_); };
  [[nodiscard]] const FlutterError& error() const {
    return std::get<FlutterError>(v_);
  };

 private:
  friend class CameraApi;
  ErrorOr() = default;
  T TakeValue() && { return std::get<T>(std::move(v_)); }

  std::variant<T, FlutterError> v_;
};

// Pigeon version of platform interface's ResolutionPreset.
enum class PlatformResolutionPreset {
  low = 0,
  medium = 1,
  high = 2,
  veryHigh = 3,
  ultraHigh = 4,
  max = 5
};

// Pigeon version of MediaSettings.
//
// Generated class from Pigeon that represents data sent in messages.
class PlatformMediaSettings {
 public:
  // Constructs an object setting all non-nullable fields.
  explicit PlatformMediaSettings(
      const PlatformResolutionPreset& resolution_preset,
      bool enable_audio);

  // Constructs an object setting all fields.
  explicit PlatformMediaSettings(
      const PlatformResolutionPreset& resolution_preset,
      const int64_t* frames_per_second,
      const int64_t* video_bitrate,
      const int64_t* audio_bitrate,
      bool enable_audio);

  [[nodiscard]] const PlatformResolutionPreset& resolution_preset() const;
  void set_resolution_preset(const PlatformResolutionPreset& value_arg);

  [[nodiscard]] const int64_t* frames_per_second() const;
  void set_frames_per_second(const int64_t* value_arg);
  void set_frames_per_second(int64_t value_arg);

  [[nodiscard]] const int64_t* video_bitrate() const;
  void set_video_bitrate(const int64_t* value_arg);
  void set_video_bitrate(int64_t value_arg);

  [[nodiscard]] const int64_t* audio_bitrate() const;
  void set_audio_bitrate(const int64_t* value_arg);
  void set_audio_bitrate(int64_t value_arg);

  [[nodiscard]] bool enable_audio() const;
  void set_enable_audio(bool value_arg);

 private:
  static PlatformMediaSettings FromEncodableList(
      const flutter::EncodableList& list);
  [[nodiscard]] flutter::EncodableList ToEncodableList() const;
  friend class CameraApi;
  friend class PigeonCodecSerializer;
  PlatformResolutionPreset resolution_preset_;
  std::optional<int64_t> frames_per_second_;
  std::optional<int64_t> video_bitrate_;
  std::optional<int64_t> audio_bitrate_;
  bool enable_audio_;
};

// A representation of a size from the native camera APIs.
//
// Generated class from Pigeon that represents data sent in messages.
class PlatformSize {
 public:
  // Constructs an object setting all fields.
  explicit PlatformSize(double width, double height);

  [[nodiscard]] double width() const;
  void set_width(double value_arg);

  [[nodiscard]] double height() const;
  void set_height(double value_arg);

 private:
  static PlatformSize FromEncodableList(const flutter::EncodableList& list);
  [[nodiscard]] flutter::EncodableList ToEncodableList();
  friend class CameraApi;
  friend class PigeonCodecSerializer;
  double width_;
  double height_;
};

class PigeonCodecSerializer : public flutter::StandardCodecSerializer {
 public:
  PigeonCodecSerializer();
  inline static PigeonCodecSerializer& GetInstance() {
    static PigeonCodecSerializer sInstance;
    return sInstance;
  }

  void WriteValue(const flutter::EncodableValue& value,
                  flutter::ByteStreamWriter* stream) const override;

 protected:
  flutter::EncodableValue ReadValueOfType(
      uint8_t type,
      flutter::ByteStreamReader* stream) const override;
};

// Generated interface from Pigeon that represents a handler of messages from
// Flutter.
class CameraApi {
 public:
  CameraApi(const CameraApi&) = delete;
  CameraApi& operator=(const CameraApi&) = delete;
  virtual ~CameraApi() = default;
  // Returns the names of all of the available capture devices.
  virtual ErrorOr<flutter::EncodableList> GetAvailableCameras() = 0;
  // Creates a camera instance for the given device name and settings.
  virtual void Create(const std::string& camera_name,
                      const PlatformMediaSettings& settings,
                      std::function<void(ErrorOr<int64_t> reply)> result) = 0;
  // Initializes a camera, and returns the size of its preview.
  virtual void Initialize(
      int64_t camera_id,
      std::function<void(ErrorOr<PlatformSize> reply)> result) = 0;
  // Disposes a camera that is no longer in use.
  virtual std::optional<FlutterError> Dispose(int64_t camera_id) = 0;
  // Takes a picture with the given camera, and returns the path to the
  // resulting file.
  virtual void TakePicture(
      int64_t camera_id,
      std::function<void(ErrorOr<std::string> reply)> result) = 0;
  // Starts recording video with the given camera.
  virtual void StartVideoRecording(
      int64_t camera_id,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  // Finishes recording video with the given camera, and returns the path to
  // the resulting file.
  virtual void StopVideoRecording(
      int64_t camera_id,
      std::function<void(ErrorOr<std::string> reply)> result) = 0;
  // Starts the preview stream for the given camera.
  virtual void PausePreview(
      int64_t camera_id,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;
  // Resumes the preview stream for the given camera.
  virtual void ResumePreview(
      int64_t camera_id,
      std::function<void(std::optional<FlutterError> reply)> result) = 0;

  // The codec used by CameraApi.
  static const flutter::StandardMessageCodec& GetCodec();
  // Sets up an instance of `CameraApi` to handle messages through the
  // `binary_messenger`.
  static void SetUp(flutter::BinaryMessenger* binary_messenger, CameraApi* api);
  static void SetUp(flutter::BinaryMessenger* binary_messenger,
                    CameraApi* api,
                    const std::string& message_channel_suffix);
  static flutter::EncodableValue WrapError(std::string_view error_message);
  static flutter::EncodableValue WrapError(const FlutterError& error);

 protected:
  CameraApi() = default;
};
}  // namespace camera_plugin
#endif  // PIGEON_MESSAGES_G_H_
