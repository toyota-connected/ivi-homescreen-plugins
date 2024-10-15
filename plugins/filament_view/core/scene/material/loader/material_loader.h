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

#include <core/include/resource.h>
#include <filament/Material.h>
#include <future>

namespace plugin_filament_view {

class MaterialLoader {
 public:
  MaterialLoader();
  ~MaterialLoader() = default;

  static Resource<::filament::Material*> loadMaterialFromAsset(
      const std::string& path);

  static Resource<::filament::Material*> loadMaterialFromUrl(
      const std::string& url);

  // Disallow copy and assign.
  MaterialLoader(const MaterialLoader&) = delete;
  MaterialLoader& operator=(const MaterialLoader&) = delete;

  static void PrintMaterialInformation(const ::filament::Material* material);

 private:
};
}  // namespace plugin_filament_view