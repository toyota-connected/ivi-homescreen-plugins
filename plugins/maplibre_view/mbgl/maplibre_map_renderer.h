/*
 * Copyright 2026 Toyota Connected North America
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef FLUTTER_PLUGIN_MAPLIBRE_MAP_RENDERER_H_
#define FLUTTER_PLUGIN_MAPLIBRE_MAP_RENDERER_H_

#include <cstdint>
#include <memory>
#include <string>

// Vulkan-FREE boundary between the plugin and MapLibre-native. Everything mbgl
// and MapLibre's (older) vulkan.hpp lives behind this in the .cc, compiled in
// the isolated maplibre_mbgl sub-library. Vulkan handles cross as raw pointers
// (void*) and the image as a VkImage handle widened to uint64 —
// version-neutral.
namespace plugin_maplibre_view {

// The compositor's Vulkan handles (from Backend::GetVulkanContext), passed
// through to RendererBackend::initExternal.
struct MapLibreDeviceContext {
  void* instance = nullptr;
  void* physical_device = nullptr;
  void* device = nullptr;
  uint32_t graphics_queue_index = 0;
  void* queue = nullptr;
  void* vma_allocator = nullptr;  // optional; null => own VMA
  void* get_instance_proc_addr =
      nullptr;  // PFN_vkGetInstanceProcAddr; may be null
};

// Drives an mbgl::Map headlessly on the compositor's own Vulkan device and
// exposes the rendered color VkImage for the compositor to sample.
//
// THREAD AFFINITY: mbgl is thread-affine to the util::RunLoop created in Init.
// Init, Render, and destruction must all happen on the SAME thread (the
// rasterizer thread that calls the plugin's OnPresent).
class MapLibreMapRenderer {
 public:
  MapLibreMapRenderer();
  ~MapLibreMapRenderer();

  MapLibreMapRenderer(const MapLibreMapRenderer&) = delete;
  MapLibreMapRenderer& operator=(const MapLibreMapRenderer&) = delete;

  // Build the mbgl object graph (RunLoop + backend + Map + style) on the
  // current thread. Returns false on failure.
  bool Init(const MapLibreDeviceContext& ctx,
            int32_t width,
            int32_t height,
            const std::string& style_url,
            const std::string& api_key,
            const std::string& asset_path,
            const std::string& cache_path);

  // Render one frame at the given size; returns the color VkImage handle as
  // uint64 (0 if not ready). Same thread as Init.
  uint64_t Render(int32_t width, int32_t height);

  [[nodiscard]] bool ok() const;
  [[nodiscard]] int32_t width() const;
  [[nodiscard]] int32_t height() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace plugin_maplibre_view

#endif  // FLUTTER_PLUGIN_MAPLIBRE_MAP_RENDERER_H_
