
#pragma once

#include <optional>
#include <string>
#include <utility>

enum class Status { Success, Error, Loading };

template <typename T>
class Resource {
 private:
  Status status;
  std::string_view message;
  std::optional<T> data;

 public:
  Resource(Status status,
           std::string_view message,
           std::optional<T> data = std::nullopt)
      : status(status), message(message), data(data) {}

  static Resource Success(T data) {
    return Resource(Status::Success, "", data);
  }

  static Resource Error(std::string_view message) {
    return Resource(Status::Error, message, std::nullopt);
  }

  [[nodiscard]] Status getStatus() const { return status; }

  [[nodiscard]] std::string_view getMessage() const { return message; }

  [[nodiscard]] std::optional<T> getData() const { return data; }
};