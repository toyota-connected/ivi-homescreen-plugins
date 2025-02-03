#
# Copyright 2024 Toyota Connected North America
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

include_guard()

if (NOT EXISTS ${FILAMENT_INCLUDE_DIR})
    message(FATAL_ERROR "${FILAMENT_INCLUDE_DIR} does not exist")
endif ()
if (NOT EXISTS ${FILAMENT_LINK_LIBRARIES_DIR})
    message(FATAL_ERROR "${FILAMENT_LINK_LIBRARIES_DIR} does not exist")
endif ()

message(STATUS "  FILAMENT_INCLUDE_DIR: ${FILAMENT_INCLUDE_DIR}")
message(STATUS "  FILAMENT_LINK_LIBRARIES_DIR: ${FILAMENT_LINK_LIBRARIES_DIR}")

add_library(filament INTERFACE
        core/include/shapetypes.h)
target_compile_options(filament INTERFACE -isystem${FILAMENT_INCLUDE_DIR} -isystem${FILAMENT_INCLUDE_DIR}/filament)
target_link_options(filament INTERFACE -L${FILAMENT_LINK_LIBRARIES_DIR})
target_link_libraries(filament INTERFACE
        libbackend.a
        libbluevk.a
        libdracodec.a
        libfilamat.a
        libfilameshio.a
        libgltfio_core.a
        libimage.a
        libshaders.a
        libuberarchive.a
        libviewer.a
        libbasis_transcoder.a
        libcamutils.a
        libfilabridge.a
        libfilament.a
        libgeometry.a
        libibl.a
        libimageio.a
        libmeshoptimizer.a
        libsmol-v.a
        libuberzlib.a
        libvkshaders.a
        libcivetweb.a
        libtinyexr.a
        libfilaflat.a
        libfilament-iblprefilter.a
        libgltfio.a
        libibl-lite.a
        libktxreader.a
        libmikktspace.a
        libpng.a
        libstb.a
        libutils.a
        libzstd.a
        # libmatdbg.a # required for linking with debug libs

        z
        toolchain::toolchain
)

add_library(plugin_filament_view STATIC
        filament_view_plugin_c_api.cc
        filament_view_plugin.cc
        messages.g.cc

        core/scene/serialization/scene_text_deserializer.cc
        core/components/derived/basetransform.cc
        core/components/derived/commonrenderable.cc
        core/components/derived/collidable.cc
        core/components/derived/animation.cc
        core/components/derived/light.cc
        core/entity/derived/nonrenderable_entityobject.cc
        core/entity/derived/renderable_entityobject.cc
        core/systems/base/ecsystem.cc
        core/systems/ecsystems_manager.cc
        core/systems/derived/filament_system.cc
        core/systems/derived/model_system.cc
        core/entity/derived/model/model.cc
        core/scene/camera/camera.cc
        core/scene/camera/camera_manager.cc
        core/scene/camera/exposure.cc
        core/scene/camera/lens_projection.cc
        core/scene/camera/projection.cc
        core/entity/base/entityobject.cc
        core/scene/geometry/ray.cc
        core/scene/geometry/size.cc
        core/scene/serialization/scene_text_deserializer.cc
        core/systems/derived/collision_system.cc
        core/systems/derived/debug_lines_system.cc
        core/scene/indirect_light/indirect_light.cc
        core/systems/derived/indirect_light_system.cc
        core/utils/entitytransforms.cc
        core/utils/hdr_loader.cc
        core/systems/derived/light_system.cc
        core/scene/material/loader/material_loader.cc
        core/scene/material/loader/texture_loader.cc
        core/systems/derived/material_system.cc
        core/components/derived/material_definitions.cc
        core/scene/material/material_parameter.cc
        core/scene/material/texture/texture_definitions.cc
        core/scene/material/texture/texture_sampler.cc
        core/scene/skybox/skybox.cc
        core/systems/derived/skybox_system.cc
        core/systems/derived/animation_system.cc
        core/entity/derived/shapes/baseshape.cc
        core/entity/derived/shapes/cube.cc
        core/entity/derived/shapes/sphere.cc
        core/entity/derived/shapes/plane.cc
        core/systems/derived/shape_system.cc
        core/utils/deserialize.cc
        core/scene/view_target.cc
        core/systems/derived/view_target_system.cc
        core/systems/derived/entityobject_locator_system.cc
)

target_include_directories(plugin_filament_view PUBLIC
        .
        include
)

target_link_libraries(plugin_filament_view PUBLIC
        platform_homescreen
        plugin_common
        plugin_common_curl
        filament
        flutter
        asio
        toolchain::toolchain
)

#
# Filament MVP Example
#
pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
if (SDL2_FOUND)
if (NOT)
     add_executable(filament-mvp test/mvp.cc)
     target_link_libraries(filament-mvp PUBLIC
             PkgConfig::SDL2
             filament
             ${FILAMENT_LINK_LIBRARIES_DIR}/../../../../third_party/spirv-tools/source/opt/libSPIRV-Tools-opt.a
             ${FILAMENT_LINK_LIBRARIES_DIR}/../../../../third_party/spirv-cross/tnt/libspirv-cross-msl.a
             toolchain::toolchain
     )
     add_sanitizers(filament-mvp)
endif ()
endif ()
