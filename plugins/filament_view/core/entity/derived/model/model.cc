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

#include "model.h"

#include <core/components/derived/collidable.h>
#include <core/include/literals.h>
#include <core/scene/scene.h>
#include <core/utils/deserialize.h>
#include <plugins/common/common.h>
#include <utility>

namespace plugin_filament_view {

Model::Model(std::string assetPath,
             std::string url,
             Model* fallback,
             Animation* animation,
             std::shared_ptr<BaseTransform> poTransform,
             std::shared_ptr<CommonRenderable> poCommonRenderable,
             const flutter::EncodableMap& params)
    : EntityObject(assetPath),
      assetPath_(std::move(assetPath)),
      url_(std::move(url)),
      fallback_(fallback),
      animation_(animation),
      m_poAsset(nullptr) {
  m_poBaseTransform = std::weak_ptr<BaseTransform>(poTransform);
  m_poCommonRenderable = std::weak_ptr<CommonRenderable>(poCommonRenderable);

  DeserializeNameAndGlobalGuid(params);

  vAddComponent(std::move(poTransform));
  vAddComponent(std::move(poCommonRenderable));

  // if we have collidable data request, we need to build that component, as its
  // optional
  auto it = params.find(flutter::EncodableValue(kCollidable));
  if (it != params.end() && !it->second.IsNull()) {
    // They're requesting a collidable on this object. Make one.
    auto collidableComp = std::make_shared<Collidable>(params);
    vAddComponent(std::move(collidableComp));
  }
}

GlbModel::GlbModel(std::string assetPath,
                   std::string url,
                   Model* fallback,
                   Animation* animation,
                   std::shared_ptr<BaseTransform> poTransform,
                   std::shared_ptr<CommonRenderable> poCommonRenderable,
                   const flutter::EncodableMap& params)
    : Model(std::move(assetPath),
            std::move(url),
            fallback,
            animation,
            std::move(poTransform),
            std::move(poCommonRenderable),
            params) {}

GltfModel::GltfModel(std::string assetPath,
                     std::string url,
                     std::string pathPrefix,
                     std::string pathPostfix,
                     Model* fallback,
                     Animation* animation,
                     std::shared_ptr<BaseTransform> poTransform,
                     std::shared_ptr<CommonRenderable> poCommonRenderable,
                     const flutter::EncodableMap& params)
    : Model(std::move(assetPath),
            std::move(url),
            fallback,
            animation,
            std::move(poTransform),
            std::move(poCommonRenderable),
            params),
      pathPrefix_(std::move(pathPrefix)),
      pathPostfix_(std::move(pathPostfix)) {}

std::unique_ptr<Model> Model::Deserialize(const std::string& flutterAssetsPath,
                                          const flutter::EncodableMap& params) {
  SPDLOG_TRACE("++Model::Model");
  std::unique_ptr<Animation> animation;
  std::unique_ptr<Model> fallback;
  std::optional<std::string> assetPath;
  std::optional<std::string> pathPrefix;
  std::optional<std::string> pathPostfix;
  std::optional<std::string> url;
  std::unique_ptr<Scene> scene;
  bool is_glb = false;

  auto oTransform = std::make_shared<BaseTransform>(params);
  auto oCommonRenderable = std::make_shared<CommonRenderable>(params);

  for (const auto& it : params) {
    if (it.second.IsNull())
      continue;

    auto key = std::get<std::string>(it.first);
    if (key == "animation" &&
        std::holds_alternative<flutter::EncodableMap>(it.second)) {
      animation = std::make_unique<Animation>(
          flutterAssetsPath, std::get<flutter::EncodableMap>(it.second));
    } else if (key == "assetPath" &&
               std::holds_alternative<std::string>(it.second)) {
      assetPath = std::get<std::string>(it.second);
    } else if (key == "isGlb" && std::holds_alternative<bool>(it.second)) {
      is_glb = std::get<bool>(it.second);
    } else if (key == "url" && std::holds_alternative<std::string>(it.second)) {
      url = std::get<std::string>(it.second);
    } else if (key == "pathPrefix" &&
               std::holds_alternative<std::string>(it.second)) {
      pathPrefix = std::get<std::string>(it.second);
    } else if (key == "pathPostfix" &&
               std::holds_alternative<std::string>(it.second)) {
      pathPostfix = std::get<std::string>(it.second);
    } else if (key == "scene" &&
               std::holds_alternative<flutter::EncodableMap>(it.second)) {
      scene = std::make_unique<Scene>(flutterAssetsPath, it.second);
    } /*else if (!it.second.IsNull()) {
      spdlog::debug("[Model] Unhandled Parameter");
      plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(),
                                                           it.second);
    }*/
  }

  if (is_glb) {
    return std::make_unique<plugin_filament_view::GlbModel>(
        assetPath.has_value() ? std::move(assetPath.value()) : "",
        url.has_value() ? std::move(url.value()) : "", nullptr,
        animation ? animation.release() : nullptr, oTransform,
        oCommonRenderable, params);
  }

  return std::make_unique<plugin_filament_view::GltfModel>(
      assetPath.has_value() ? std::move(assetPath.value()) : "",
      url.has_value() ? std::move(url.value()) : "",
      pathPrefix.has_value() ? std::move(pathPrefix.value()) : "",
      pathPostfix.has_value() ? std::move(pathPostfix.value()) : "", nullptr,
      animation ? animation.release() : nullptr, oTransform, oCommonRenderable,
      params);
}

void Model::DebugPrint() const {
  vDebugPrintComponents();
}

}  // namespace plugin_filament_view
