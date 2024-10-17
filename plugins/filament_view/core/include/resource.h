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

#include <optional>
#include <string>

enum class Status { Success, Error, Loading };

template <typename T>
class Resource {
 private:
  Status status_;
  std::string_view message_ = {};
  std::optional<T> data_ = {};

 public:
  Resource(Status status,
           std::string_view message,
           std::optional<T> data = std::nullopt)
      : status_(status), message_(message), data_(std::move(data)) {}

  Resource() : status_(Status::Success), message_("") {}

  static Resource Success(T data) {
    return Resource(Status::Success, "", data);
  }

  static Resource Error(std::string_view message) {
    return Resource(Status::Error, message, std::nullopt);
  }

  [[nodiscard]] Status getStatus() const { return status_; }

  [[nodiscard]] std::string_view getMessage() const { return message_; }

  [[nodiscard]] std::optional<T> getData() const { return data_; }
};