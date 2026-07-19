/*
 * Copyright 2026 Toyota Connected North America
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 */

#include "maplibre_map_renderer.h"

#include <mbgl/gfx/backend_scope.hpp>
#include <mbgl/map/camera.hpp>
#include <mbgl/map/map.hpp>
#include <mbgl/map/map_observer.hpp>
#include <mbgl/map/map_options.hpp>
#include <mbgl/renderer/renderer.hpp>
#include <mbgl/renderer/renderer_frontend.hpp>
#include <mbgl/renderer/update_parameters.hpp>
#include <mbgl/storage/resource_options.hpp>
#include <mbgl/style/style.hpp>
#include <mbgl/util/async_task.hpp>
#include <mbgl/util/geo.hpp>
#include <mbgl/util/run_loop.hpp>

#include "external_headless_backend.h"

namespace plugin_maplibre_view {

namespace {

mbgl::vulkan::RendererBackend::ExternalVulkanContext ToExternal(
    const MapLibreDeviceContext& c) {
  mbgl::vulkan::RendererBackend::ExternalVulkanContext e;
  e.instance = static_cast<VkInstance>(c.instance);
  e.physicalDevice = static_cast<VkPhysicalDevice>(c.physical_device);
  e.device = static_cast<VkDevice>(c.device);
  e.graphicsQueueIndex = c.graphics_queue_index;
  e.graphicsQueue = static_cast<VkQueue>(c.queue);
  e.allocator = static_cast<VmaAllocator>(c.vma_allocator);
  e.getInstanceProcAddr =
      reinterpret_cast<PFN_vkGetInstanceProcAddr>(c.get_instance_proc_addr);
  return e;
}

}  // namespace

// RendererFrontend owning the external backend + Renderer. Mirrors
// mbgl::HeadlessFrontend, but render() returns the color VkImage the compositor
// samples instead of doing a CPU readback.
class MapLibreFrontend final : public mbgl::RendererFrontend {
 public:
  MapLibreFrontend(
      const mbgl::vulkan::RendererBackend::ExternalVulkanContext& ctx,
      mbgl::Size size,
      float pixelRatio)
      : backend_(std::make_unique<ExternalHeadlessBackend>(ctx, size)),
        asyncInvalidate_([this] { renderFrame(); }),
        renderer_(std::make_unique<mbgl::Renderer>(*backend_, pixelRatio)) {}

  ~MapLibreFrontend() override = default;

  void reset() override { renderer_.reset(); }

  void update(std::shared_ptr<mbgl::UpdateParameters> params) override {
    // Pull model: store the parameters; the render is driven explicitly per
    // compositor frame in pumpAndRender (not via asyncInvalidate), so the
    // rasterizer thread is never blocked waiting for network tiles.
    updateParameters_ = std::move(params);
  }

  [[nodiscard]] const mbgl::TaggedScheduler& getThreadPool() const override {
    return backend_->getRendererBackend()->getThreadPool();
  }

  void setObserver(mbgl::RendererObserver& observer) override {
    if (renderer_) {
      renderer_->setObserver(&observer);
    }
  }

  void setSize(mbgl::Size size) { backend_->setSize(size); }

  // Non-blocking pull for a compositor: pump the run loop a bounded number of
  // times to advance async style/tile loading and apply Map updates, render the
  // current state on this thread, and return the latest color image. The map
  // fills in progressively over successive calls as tiles arrive — the
  // rasterizer thread is never blocked.
  VkImage pumpAndRender() {
    for (int i = 0; i < 16; ++i) {
      mbgl::util::RunLoop::Get()->runOnce();
    }
    // Only return an image once a real render has happened; before the style
    // loads there is nothing to draw, and handing back an unrendered image (in
    // an undefined layout) would corrupt the compositor.
    if (renderer_ && updateParameters_) {
      mbgl::gfx::BackendScope guard{*backend_};
      auto params = updateParameters_;
      renderer_->render(params);
      return backend_->colorImage();
    }
    return VK_NULL_HANDLE;
  }

 private:
  void renderFrame() {
    if (renderer_ && updateParameters_) {
      mbgl::gfx::BackendScope guard{*backend_};
      auto params = updateParameters_;
      renderer_->render(params);
    }
  }

  std::unique_ptr<ExternalHeadlessBackend> backend_;
  mbgl::util::AsyncTask asyncInvalidate_;
  std::unique_ptr<mbgl::Renderer> renderer_;
  std::shared_ptr<mbgl::UpdateParameters> updateParameters_;
};

struct MapLibreMapRenderer::Impl {
  std::unique_ptr<mbgl::util::RunLoop> runLoop;
  std::unique_ptr<MapLibreFrontend> frontend;
  std::unique_ptr<mbgl::Map> map;
  int32_t width = 0;
  int32_t height = 0;
  bool ok = false;
};

MapLibreMapRenderer::MapLibreMapRenderer() : impl_(std::make_unique<Impl>()) {}

MapLibreMapRenderer::~MapLibreMapRenderer() = default;

bool MapLibreMapRenderer::Init(const MapLibreDeviceContext& ctx,
                               int32_t width,
                               int32_t height,
                               const std::string& style_url,
                               const std::string& api_key,
                               const std::string& asset_path,
                               const std::string& cache_path) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  const float pixelRatio = 1.0f;
  const mbgl::Size size{static_cast<uint32_t>(width),
                        static_cast<uint32_t>(height)};

  // The RunLoop must live on the calling (render) thread; mbgl is affine to it.
  impl_->runLoop = std::make_unique<mbgl::util::RunLoop>();
  impl_->frontend =
      std::make_unique<MapLibreFrontend>(ToExternal(ctx), size, pixelRatio);

  mbgl::MapOptions mapOptions;
  // Continuous: render-on-demand each compositor frame; tiles stream in over
  // frames instead of a blocking still-render.
  mapOptions.withMapMode(mbgl::MapMode::Continuous)
      .withSize(size)
      .withPixelRatio(pixelRatio);

  mbgl::ResourceOptions resourceOptions;
  resourceOptions.withApiKey(api_key)
      .withCachePath(cache_path)
      .withAssetPath(asset_path);

  impl_->map = std::make_unique<mbgl::Map>(*impl_->frontend,
                                           mbgl::MapObserver::nullObserver(),
                                           mapOptions, resourceOptions);
  if (!style_url.empty()) {
    impl_->map->getStyle().loadURL(style_url);
  }
  // FAKE fixed location until the GPS/location service (gpsd + geoclue) is
  // wired — Toyota Connected North America, Plano TX. Replace
  // withCenter/withZoom with the live fix once the location plugin feeds a
  // position. zoom 4: demotiles (the free no-API-key style) only has data at
  // low zooms
  // (~0-6, country/region outlines) — at city zoom it is just the background.
  // A real street-level style + API key would use a higher zoom.
  impl_->map->jumpTo(mbgl::CameraOptions()
                         .withCenter(mbgl::LatLng{33.0198, -96.6989})
                         .withZoom(4.0));

  impl_->width = width;
  impl_->height = height;
  impl_->ok = true;
  return true;
}

uint64_t MapLibreMapRenderer::Render(int32_t width, int32_t height) {
  if (!impl_->ok || width <= 0 || height <= 0) {
    return 0;
  }
  if (width != impl_->width || height != impl_->height) {
    const mbgl::Size size{static_cast<uint32_t>(width),
                          static_cast<uint32_t>(height)};
    impl_->frontend->setSize(size);
    impl_->map->setSize(size);
    impl_->width = width;
    impl_->height = height;
  }
  const VkImage image = impl_->frontend->pumpAndRender();
  return reinterpret_cast<uint64_t>(image);
}

bool MapLibreMapRenderer::ok() const {
  return impl_ && impl_->ok;
}
int32_t MapLibreMapRenderer::width() const {
  return impl_ ? impl_->width : 0;
}
int32_t MapLibreMapRenderer::height() const {
  return impl_ ? impl_->height : 0;
}

}  // namespace plugin_maplibre_view
