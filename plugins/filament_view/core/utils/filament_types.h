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

#include <filament/utils/Entity.h>
#include <filament/utils/EntityInstance.h>

#include <filament/RenderableManager.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>

#include <map>

namespace plugin_filament_view {

/// Represents an entity in the Filament engine.
/// Lightweight object: can be passed around by value (it's just an ID).
using FilamentEntity = utils::Entity;

/// Represents a Filament entity instance.
/// Lightweight object: can be passed around by value (it's just an ID).
template<typename T> using FilamentEntityInstance = utils::EntityInstance<T>;
using FilamentTransformInstance = FilamentEntityInstance<filament::TransformManager>;
using FilamentRenderableInstance = FilamentEntityInstance<filament::RenderableManager>;
using FilamentCameraComponent = filament::Camera;

using Texture = Resource<::filament::Texture*>;
using TextureMap = std::map<std::string, Texture>;
using MinFilter = filament::TextureSampler::MinFilter;
using MagFilter = filament::TextureSampler::MagFilter;

using MaterialDefinition = Resource<::filament::Material*>;
using MaterialInstance = Resource<::filament::MaterialInstance*>;

};  // namespace plugin_filament_view
