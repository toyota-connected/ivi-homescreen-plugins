/*
 * Copyright 2026 Toyota Connected North America
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

#ifndef FLUTTER_PLUGIN_MAPLIBRE_VIEW_PLUGIN_H_
#define FLUTTER_PLUGIN_MAPLIBRE_VIEW_PLUGIN_H_

#include <atomic>
#include <cstdint>
#include <memory>

#include "config/common.h"

// A platform-view plugin's entire rendering path is gated on BUILD_COMPOSITOR;
// built without it the plugin registers a factory but produces no native
// content and the compositor has nothing to place — a silent no-op at runtime.
// Fail loudly at build time instead. (BUILD_COMPOSITOR is defined by the
// generated config/common.h above; an undefined macro also trips this.)
#if !BUILD_COMPOSITOR
#error \
    "maplibre_view requires BUILD_COMPOSITOR=1 (build with -DBUILD_COMPOSITOR=ON); a platform-view plugin does nothing without the compositor."
#endif

#include "flutter_desktop_engine_state.h"
#include "platform_views/platform_view.h"
#include "platform_views/platform_view_registry.h"
#include "view/compositor_surface_interface.h"

#if defined(MAPLIBRE_VIEW_HAVE_MBGL) && MAPLIBRE_VIEW_HAVE_MBGL
#include "mbgl/maplibre_map_renderer.h"  // Vulkan-free boundary (isolated sub-lib)
#endif

namespace plugin_maplibre_view {

class MapLibreVulkanRenderer;

// MapLibre Native platform view (Vulkan / Mode A).
//
// Registered through the PlatformViewRegistry factory seam (not the legacy
// generated dispatch): MapLibreViewPlugin::Create is installed as the factory
// for viewType "views/maplibre-view", so the registry owns the instance and
// destroys it on dispose. The view reuses the compositor backend's own Vulkan
// device (Backend::GetVulkanContext) to render into a VkImage the compositor
// samples directly via GetVulkanImage — no second device, no dma-buf.
//
// The renderer is a placeholder until MapLibre-native (mbgl::Map on the
// injected device) is wired in; the plugin plumbing — factory registration,
// compositor surface, resize/touch/dispose — is the real, final shape.
class MapLibreViewPlugin final : public PlatformView,
                                 public ICompositorSurface {
 public:
  // Factory installed via PlatformViewRegistry::RegisterFactory. Constructs the
  // view, registers its callback table, and returns it for the registry to own.
  static std::unique_ptr<PlatformView> Create(
      FlutterDesktopEngineState* state,
      PlatformViewRegistry& registry,
      const PlatformViewRegistry::CreateRequest& request);

  MapLibreViewPlugin(FlutterDesktopEngineState* state,
                     const PlatformViewRegistry::CreateRequest& request);
  ~MapLibreViewPlugin() override;

  MapLibreViewPlugin(const MapLibreViewPlugin&) = delete;
  MapLibreViewPlugin& operator=(const MapLibreViewPlugin&) = delete;

  // ICompositorSurface — a Vulkan producer (no backing store, no GL texture).
  bool OnCreateBackingStore(const FlutterBackingStoreConfig*,
                            FlutterBackingStore*) override {
    return false;
  }
  bool OnCollectBackingStore(const FlutterBackingStore*) override {
    return true;
  }
  bool OnPresent(const FlutterLayer* layer) override;
  [[nodiscard]] FlutterPlatformViewIdentifier GetIdentifier() const override {
    return id_;
  }
  void OnResize(int32_t w, int32_t h) override;

  [[nodiscard]] void* GetVulkanImage(int32_t* width,
                                     int32_t* height) const override;
  [[nodiscard]] uint32_t GetVulkanImageLayout() const override;
  void SetVulkanImageLayout(uint32_t layout) override;

 private:
  int32_t id_;

  // Vulkan render path on the compositor's own device. Null if the active
  // backend is not Vulkan (GetVulkanContext failed) — the placeholder needs
  // Vulkan today; a GL/dma-buf path is future work.
  std::unique_ptr<MapLibreVulkanRenderer> vulkan_renderer_;

#if defined(MAPLIBRE_VIEW_HAVE_MBGL) && MAPLIBRE_VIEW_HAVE_MBGL
  // Real MapLibre map renderer (Mode A). mbgl is affine to a RunLoop created on
  // the rasterizer thread, so it is lazily constructed in OnPresent; the device
  // context captured on the platform-thread ctor is stored here until then.
  // When the map renders, it supersedes the placeholder.
  MapLibreDeviceContext map_ctx_{};
  bool map_ctx_valid_{false};
  bool map_init_attempted_{false};
  uint64_t map_image_{0};
  std::unique_ptr<MapLibreMapRenderer> map_renderer_;
  // For requesting the next frame while the map streams tiles (the Flutter UI
  // is otherwise static). Owned by the engine, which outlives this view.
  FlutterDesktopEngineState* engine_state_{nullptr};
#endif

  std::atomic<int32_t> pending_width_{0};
  std::atomic<int32_t> pending_height_{0};
  bool first_present_{true};
  int32_t last_present_w_{-1};
  int32_t last_present_h_{-1};

  // platform_view_listener C callbacks (registry drives these). `data` is the
  // MapLibreViewPlugin* registered as the listener context.
  static void on_resize(double width, double height, void* data);
  static void on_set_direction(int32_t direction, void* data);
  static void on_set_offset(double left, double top, void* data);
  static void on_touch(int32_t action,
                       int32_t point_count,
                       size_t point_data_size,
                       const double* point_data,
                       void* data);
  static void on_dispose(bool hybrid, void* data);

  static const struct platform_view_listener platform_view_listener_;
};

}  // namespace plugin_maplibre_view

#endif  // FLUTTER_PLUGIN_MAPLIBRE_VIEW_PLUGIN_H_
