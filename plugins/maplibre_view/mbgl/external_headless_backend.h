/*
 * Copyright 2026 Toyota Connected North America
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef FLUTTER_PLUGIN_MAPLIBRE_EXTERNAL_HEADLESS_BACKEND_H_
#define FLUTTER_PLUGIN_MAPLIBRE_EXTERNAL_HEADLESS_BACKEND_H_

// This header pulls MapLibre's (older) vulkan.hpp via mbgl headers. It must
// only be compiled in the isolated maplibre_mbgl sub-library, never mixed in a
// translation unit that also includes ivi-homescreen's (newer) Vulkan-Headers —
// the two vulkan.hpp versions use different dispatcher namespaces. The boundary
// to the rest of the plugin is the Vulkan-free maplibre_map_renderer.h.

#include <mbgl/vulkan/headless_backend.hpp>
#include <mbgl/vulkan/renderer_backend.hpp>

#include <memory>

namespace plugin_maplibre_view {

// A headless MapLibre Vulkan backend that ADOPTS the compositor's Vulkan device
// (via RendererBackend::initExternal) instead of creating its own, so the map
// renders on the exact device the compositor samples. Mirrors mbgl::vulkan::
// HeadlessBackend (which is `final` and hardcodes init()) but is surfaceless
// and external-device. The rendered color image is reached via colorImage().
class ExternalHeadlessBackend final : public mbgl::vulkan::RendererBackend,
                                      public mbgl::gfx::HeadlessBackend {
 public:
  ExternalHeadlessBackend(
      const mbgl::vulkan::RendererBackend::ExternalVulkanContext& ctx,
      mbgl::Size size);
  ~ExternalHeadlessBackend() override;

  ExternalHeadlessBackend(const ExternalHeadlessBackend&) = delete;
  ExternalHeadlessBackend& operator=(const ExternalHeadlessBackend&) = delete;

  // gfx::HeadlessBackend
  mbgl::gfx::Renderable& getDefaultRenderable() override;
  mbgl::PremultipliedImage readStillImage() override;
  mbgl::vulkan::RendererBackend* getRendererBackend() override;

  // The color VkImage the compositor samples (colorAllocations[acquired] of the
  // surfaceless renderable). Valid after a frame has been rendered. Returned as
  // the raw VkImage handle.
  VkImage colorImage();

 private:
  void activate() override;
  void deactivate() override;

  bool active_ = false;
};

}  // namespace plugin_maplibre_view

#endif  // FLUTTER_PLUGIN_MAPLIBRE_EXTERNAL_HEADLESS_BACKEND_H_
