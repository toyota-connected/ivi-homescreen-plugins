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
#include "material_loader.h"

#include <core/include/file_utils.h>
#include <core/include/literals.h>
#include <core/systems/derived/filament_system.h>
#include <core/systems/ecs.h>
#include <core/utils/filament_types.h>

#include <plugins/common/curl_client/curl_client.h>

namespace plugin_filament_view {

using filament::RgbType;
using filament::math::float3;

////////////////////////////////////////////////////////////////////////////
MaterialLoader::MaterialLoader() = default;

////////////////////////////////////////////////////////////////////////////
// This function does NOT set default parameter values.
MaterialDefinition MaterialLoader::loadMaterialFromAsset(const std::string& path) {
  const auto assetPath = ECSManager::GetInstance()->getConfigValue<std::string>(kAssetPath);
  const auto buffer = readBinaryFile(path, assetPath);

  if (!buffer.empty()) {
    const auto filamentSystem = ECSManager::GetInstance()->getSystem<FilamentSystem>(
      "loadMaterialFromAsset"
    );
    const auto engine = filamentSystem->getFilamentEngine();

    const auto material =
      filament::Material::Builder().package(buffer.data(), buffer.size()).build(*engine);

    return MaterialDefinition::Success(material);
  }

  SPDLOG_ERROR("Could not load material from asset.");
  return MaterialDefinition::Error("Could not load material from asset.");
}

////////////////////////////////////////////////////////////////////////////
MaterialDefinition MaterialLoader::loadMaterialFromUrl(const std::string& url) {
  plugin_common_curl::CurlClient client;
  // TODO client.Init(url);
  const std::vector<uint8_t> buffer = client.RetrieveContentAsVector();
  if (client.GetCode() != CURLE_OK) {
    return MaterialDefinition::Error("Failed to load material from " + url);
  }

  const auto filamentSystem = ECSManager::GetInstance()->getSystem<FilamentSystem>(
    "loadMaterialFromUrl"
  );
  const auto engine = filamentSystem->getFilamentEngine();

  if (!buffer.empty()) {
    const auto material =
      filament::Material::Builder().package(buffer.data(), buffer.size()).build(*engine);
    return MaterialDefinition::Success(material);
  }

  return MaterialDefinition::Error("Could not load material from asset.");
}

////////////////////////////////////////////////////////////////////////////
void MaterialLoader::PrintMaterialInformation(const filament::Material* material) {
  spdlog::info("Material Informaton {}", material->getName());
  size_t paramCount = material->getParameterCount();
  spdlog::info("Material Informaton {}", paramCount);

  const auto InfoList = new filament::Material::ParameterInfo[paramCount];
  material->getParameters(InfoList, paramCount);

  for (size_t i = 0; i < paramCount; ++i) {
    spdlog::info("Param Informaton {}", InfoList[i].name);
  }

  spdlog::info("Material isDoubleSided {}", material->isDoubleSided());
  spdlog::info("Material isDepthCullingEnabled {}", material->isDepthCullingEnabled());
  spdlog::info("Material isDepthWriteEnabled {}", material->isDepthWriteEnabled());
  spdlog::info("Material isColorWriteEnabled {}", material->isColorWriteEnabled());

  delete[] InfoList;
}

}  // namespace plugin_filament_view
