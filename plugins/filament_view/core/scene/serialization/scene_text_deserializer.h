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
#include <core/scene/indirect_light/indirect_light.h>
#include <core/scene/skybox/skybox.h>
#include <encodable_value.h>
#include <vector>

namespace plugin_filament_view {
class Light;

class SceneTextDeserializer {
  public:
    explicit SceneTextDeserializer(const std::vector<uint8_t>& params);
    void RunPostSetupLoad();

    virtual ~SceneTextDeserializer() = default;

  private:
    smarter_raw_ptr<ECSManager> _ecs;

    std::vector<std::shared_ptr<Model>> models_;
    std::vector<std::shared_ptr<BaseShape>> shapes_;

    std::vector<std::shared_ptr<EntityObject>> entities_;

    void DeserializeRootLevel(
      const std::vector<uint8_t>& params,
      const std::string& flutterAssetsPath
    );
    // This is called from DeserializeRootLevel function when it hits a 'scene'
    // tag
    void DeserializeSceneLevel(const flutter::EncodableValue& params);

    void setUpLoadingModels();
    void setUpSkybox() const;
    void setUpLights();
    void setUpIndirectLight() const;
    void setUpShapes();
    void setUpEntities();

    void loadModel(std::shared_ptr<Model>& model);

    std::unique_ptr<Skybox> skybox_;
    std::unique_ptr<IndirectLight> indirect_light_;
    std::map<EntityGUID, std::shared_ptr<Light>> lights_;
};

}  // namespace plugin_filament_view

#endif  // SCENE_TEXT_DESERIALIZER_H
