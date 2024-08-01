
#include "core/scene/material/loader/material_loader.h"

#include "core/include/file_utils.h"
#include "plugins/common/curl_client/curl_client.h"

namespace plugin_filament_view {

using ::filament::RgbType;
using ::filament::math::float3;

MaterialLoader::MaterialLoader() {}

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
    const ::filament::Material* material) const {
  SPDLOG_DEBUG("Material Informaton {}", material->getName());
  int paramCount = material->getParameterCount();
  SPDLOG_DEBUG("Material Informaton {}", paramCount);

  filament::Material::ParameterInfo* InfoList =
      new filament::Material::ParameterInfo[paramCount];
  material->getParameters(InfoList, paramCount);

  for (int i = 0; i < paramCount; ++i) {
    SPDLOG_DEBUG("Param Informaton {}", InfoList[i].name);
  }

  SPDLOG_DEBUG("Material isDoubleSided {}", material->isDoubleSided());
  SPDLOG_DEBUG("Material isDepthCullingEnabled {}",
               material->isDepthCullingEnabled());
  SPDLOG_DEBUG("Material isDepthWriteEnabled {}",
               material->isDepthWriteEnabled());
  SPDLOG_DEBUG("Material isColorWriteEnabled {}",
               material->isColorWriteEnabled());

  delete InfoList;
}

}  // namespace plugin_filament_view
