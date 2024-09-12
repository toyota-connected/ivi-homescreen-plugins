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

namespace plugin_filament_view {

static constexpr char kId[] = "id";
static constexpr char kShapeType[] = "shapeType";
static constexpr char kSize[] = "size";
static constexpr char kCenterPosition[] = "centerPosition";
static constexpr char kStartingPosition[] = "startingPosition";
static constexpr char kNormal[] = "normal";
static constexpr char kScale[] = "scale";
static constexpr char kRotation[] = "rotation";
static constexpr char kMaterial[] = "material";
static constexpr char kDoubleSided[] = "doubleSided";
static constexpr char kCullingEnabled[] = "cullingEnabled";
static constexpr char kReceiveShadows[] = "receiveShadows";
static constexpr char kCastShadows[] = "castShadows";
static constexpr char kDirection[] = "direction";
static constexpr char kLength[] = "length";

// specific collidable values:
static constexpr char kCollidableShapeType[] = "collidable_shapeType";
static constexpr char kCollidableExtents[] = "collidable_extentsSize";
static constexpr char kCollidableIsStatic[] = "collidable_isStatic";
static constexpr char kCollidableLayer[] = "collidable_layer";
static constexpr char kCollidableMask[] = "collidable_mask";
static constexpr char kCollidableShouldMatchAttachedObject[] =
    "collidable_shouldMatchAttachedObject";

}  // namespace plugin_filament_view
