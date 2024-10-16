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

#ifndef SCENE_TEXT_DESERIALIZER_H
#define SCENE_TEXT_DESERIALIZER_H

#include <core/entity/derived/model/model.h>
#include <core/entity/derived/shapes/baseshape.h>
#include <core/scene/camera/camera.h>
#include <core/scene/indirect_light/indirect_light.h>
#include <core/scene/light/light.h>
#include <core/scene/skybox/skybox.h>
#include <encodable_value.h>
#include <vector>

namespace plugin_filament_view {

class SceneTextDeserializer {
 public:
  explicit SceneTextDeserializer(const std::vector<uint8_t>& params);
  void vRunPostSetupLoad();

  virtual ~SceneTextDeserializer() {}

 private:
  std::vector<std::unique_ptr<Model>> models_;
  std::vector<std::unique_ptr<shapes::BaseShape>> shapes_;

  void vDeserializeRootLevel(const std::vector<uint8_t>& params,
                             const std::string& flutterAssetsPath);
  // This is called from vDeserializeRootLevel function when it hits a 'scene'
  // tag
  void vDeserializeSceneLevel(const flutter::EncodableValue& params,
                              const std::string& flutterAssetsPath);

  void setUpLoadingModels();
  void setUpSkybox();
  void setUpLight();
  void setUpIndirectLight();
  void setUpShapes();

  static void loadModel(Model* model);

  std::unique_ptr<Skybox> skybox_;
  std::unique_ptr<IndirectLight> indirect_light_;
  std::vector<std::unique_ptr<Light>> lights_;
  std::unique_ptr<Camera> camera_;
};

}  // namespace plugin_filament_view

#endif  // SCENE_TEXT_DESERIALIZER_H
