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

#include "shell/platform/common/client_wrapper/include/flutter/encodable_value.h"

#include <filament/TextureSampler.h>
#include <optional>

namespace plugin_filament_view {

class TextureSampler {
 public:
  explicit TextureSampler(const flutter::EncodableMap& params);

  void DebugPrint(const char* tag);

  // Disallow copy and assign.
  TextureSampler(const TextureSampler&) = delete;

  TextureSampler& operator=(const TextureSampler&) = delete;

  [[nodiscard]] ::filament::TextureSampler::MagFilter getMagFilter() const;
  [[nodiscard]] ::filament::TextureSampler::MinFilter getMinFilter() const;
  [[nodiscard]] ::filament::TextureSampler::WrapMode getWrapModeR() const;
  [[nodiscard]] ::filament::TextureSampler::WrapMode getWrapModeS() const;
  [[nodiscard]] ::filament::TextureSampler::WrapMode getWrapModeT() const;
  [[nodiscard]] double getAnisotropy() const {
    return anisotropy_.has_value() ? anisotropy_.value() : 1;
  }

 private:
  std::string min_;
  std::string mag_;
  std::string wrapR_;
  std::string wrapS_;
  std::string wrapT_;
  std::optional<double> anisotropy_{};
};
}  // namespace plugin_filament_view