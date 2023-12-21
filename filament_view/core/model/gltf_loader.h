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

#pragma once

#include "viewer/custom_model_viewer.h"
#include "core/include/resource.h"

namespace plugin_filament_view {

class CustomModelViewer;

class Position;

class GltfLoader {
 public:
  explicit GltfLoader(CustomModelViewer* modelViewer,
                      const std::string& flutterAssets);

  ~GltfLoader() = default;

  static std::future<Resource<std::string>> loadGltfFromAsset(const std::string& path,
                                             const std::string& pre_path,
                                             const std::string& post_path,
                                             float scale,
                                             const Position* centerPosition,
                                             bool isFallback = false);

  static std::future<Resource<std::string>> loadGltfFromUrl(const std::string& url,
                                           float scale,
                                           const Position* centerPosition,
                                           bool isFallback = false);

 private:
  CustomModelViewer* modelViewer_;
  std::string flutterAssets_;
};
}  // namespace plugin_filament_view