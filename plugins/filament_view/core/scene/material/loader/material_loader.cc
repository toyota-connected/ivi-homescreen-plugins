
#include "core/scene/material/loader/material_loader.h"

#include "core/include/file_utils.h"
#include "plugins/common/curl_client/curl_client.h"

namespace plugin_filament_view {

using ::filament::RgbType;
using ::filament::math::float3;

MaterialLoader::MaterialLoader() = default;

// This function does NOT set default parameter values.
Resource<::filament::Material*> MaterialLoader::loadMaterialFromAsset(
    const std::string& path) {
  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  ::filament::Engine* engine = modelViewer->getFilamentEngine();

  auto buffer = readBinaryFile(path, modelViewer->getAssetPath());

  if (!buffer.empty()) {
    auto material = ::filament::Material::Builder()
                        .package(buffer.data(), buffer.size())
                        .build(*engine);

    return Resource<::filament::Material*>::Success(material);
  } else {
    SPDLOG_ERROR("Could not load material from asset.");
    return Resource<::filament::Material*>::Error(
        "Could not load material from asset.");
  }
}

Resource<::filament::Material*> MaterialLoader::loadMaterialFromUrl(
    const std::string& url) {
  plugin_common_curl::CurlClient client;
  // TODO client.Init(url);
  std::vector<uint8_t> buffer = client.RetrieveContentAsVector();
  if (client.GetCode() != CURLE_OK) {
    return Resource<::filament::Material*>::Error(
        "Failed to load material from " + url);
  }

  CustomModelViewer* modelViewer = CustomModelViewer::Instance(__FUNCTION__);
  ::filament::Engine* engine = modelViewer->getFilamentEngine();

  if (!buffer.empty()) {
    auto material = ::filament::Material::Builder()
                        .package(buffer.data(), buffer.size())
                        .build(*engine);
    return Resource<::filament::Material*>::Success(material);
  } else {
    return Resource<::filament::Material*>::Error(
        "Could not load material from asset.");
  }
}

void MaterialLoader::PrintMaterialInformation(
    const ::filament::Material* material) {
  spdlog::info("Material Informaton {}", material->getName());
  size_t paramCount = material->getParameterCount();
  spdlog::info("Material Informaton {}", paramCount);

  auto InfoList = new filament::Material::ParameterInfo[paramCount];
  material->getParameters(InfoList, paramCount);

  for (size_t i = 0; i < paramCount; ++i) {
    spdlog::info("Param Informaton {}", InfoList[i].name);
  }

  spdlog::info("Material isDoubleSided {}", material->isDoubleSided());
  spdlog::info("Material isDepthCullingEnabled {}",
               material->isDepthCullingEnabled());
  spdlog::info("Material isDepthWriteEnabled {}",
               material->isDepthWriteEnabled());
  spdlog::info("Material isColorWriteEnabled {}",
               material->isColorWriteEnabled());

  delete [] InfoList;
}

}  // namespace plugin_filament_view
