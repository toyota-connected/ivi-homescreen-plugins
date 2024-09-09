
#pragma once

#include <flutter/basic_message_channel.h>
#include <flutter/encodable_value.h>
#include <flutter/standard_method_codec.h>

#include <string>

namespace nav_render_view_plugin {

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
  ErrorOr() = default;

  T TakeValue() && { return std::get<T>(std::move(v_)); }

  std::variant<T, FlutterError> v_;
};

}  // namespace nav_render_view_plugin
