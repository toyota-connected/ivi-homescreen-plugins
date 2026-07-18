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

#include "maplibre_view_plugin.h"

#include "backend/backend.h"
#include "logging/logging.h"
#include "maplibre_vulkan_renderer.h"
#include "view/flutter_view.h"

namespace plugin_maplibre_view {

// static
const platform_view_listener MapLibreViewPlugin::platform_view_listener_ = {
    .resize = MapLibreViewPlugin::on_resize,
    .set_direction = MapLibreViewPlugin::on_set_direction,
    .set_offset = MapLibreViewPlugin::on_set_offset,
    .on_touch = MapLibreViewPlugin::on_touch,
    .dispose = MapLibreViewPlugin::on_dispose,
    .accept_gesture = nullptr,
    .reject_gesture = nullptr,
};

// static
std::unique_ptr<PlatformView> MapLibreViewPlugin::Create(
    FlutterDesktopEngineState* state,
    PlatformViewRegistry& registry,
    const PlatformViewRegistry::CreateRequest& request) {
  auto plugin = std::make_unique<MapLibreViewPlugin>(state, request);

  // Register the callback table so the registry can drive resize/touch/dispose.
  // CreateViaFactory attaches ownership to this same id after we return. The
  // context is the raw instance pointer (the registry owns lifetime).
  registry.RegisterListener(request.id, &platform_view_listener_, plugin.get());
  return plugin;
}

MapLibreViewPlugin::MapLibreViewPlugin(
    FlutterDesktopEngineState* state,
    const PlatformViewRegistry::CreateRequest& request)
    : PlatformView(request.id,
                   request.view_type,
                   request.direction,
                   request.left,
                   request.top,
                   request.width,
                   request.height),
      id_(request.id) {
  pending_width_ = static_cast<int32_t>(request.width);
  pending_height_ = static_cast<int32_t>(request.height);

  // Register the compositor surface so PresentLayers routes layer dispatch to
  // OnPresent, and reuse the backend's Vulkan device for rendering. Both need
  // the live view; if it is absent (headless) the view still exists as a
  // registry entry but produces nothing.
  if (state && state->view_controller && state->view_controller->view) {
    auto* view = state->view_controller->view;
    view->RegisterCompositorSurface(
        id_, std::shared_ptr<ICompositorSurface>(this, [](ICompositorSurface*) {
          // Aliasing deleter: lifetime is owned by the PlatformViewRegistry
          // (this instance's owning unique_ptr), not by the compositor's copy,
          // which is dropped on UnregisterCompositorSurface during dispose.
        }));

    if (auto* backend = view->GetBackend()) {
      BackendVulkanContext vk{};
      if (backend->GetVulkanContext(&vk)) {
        auto renderer = std::make_unique<MapLibreVulkanRenderer>();
        if (renderer->Init(static_cast<VkInstance>(vk.instance),
                           static_cast<VkPhysicalDevice>(vk.physical_device),
                           static_cast<VkDevice>(vk.device),
                           static_cast<VkQueue>(vk.queue),
                           vk.queue_family_index)) {
          vulkan_renderer_ = std::move(renderer);
          ihs::log::debug(
              "[maplibre] Vulkan render path active (reusing the compositor's "
              "device) id={}",
              id_);
        }
      } else {
        ihs::log::warn(
            "[maplibre] backend has no Vulkan context; id={} will not render "
            "(the placeholder needs a Vulkan backend)",
            id_);
      }
    }
  } else {
    ihs::log::warn(
        "[maplibre] no live view (headless?); id={} registered but inert", id_);
  }
}

MapLibreViewPlugin::~MapLibreViewPlugin() {
  // The registry drops the compositor surface (UnregisterCompositorSurface)
  // before this runs, so no more OnPresent calls arrive. Releasing the renderer
  // frees its VkImages on the borrowed device.
  vulkan_renderer_.reset();
}

bool MapLibreViewPlugin::OnPresent(const FlutterLayer* layer) {
  // The engine supplies the composed layer size every frame in layer->size;
  // trust that over on_resize, which the embedder may never deliver (the
  // platform-views `create` message carries 1x1 placeholders). Fall back to the
  // pending atomics if the layer is absent.
  int32_t target_w = pending_width_.load();
  int32_t target_h = pending_height_.load();
  if (layer && layer->size.width > 0 && layer->size.height > 0) {
    target_w = static_cast<int32_t>(layer->size.width);
    target_h = static_cast<int32_t>(layer->size.height);
  }
  const bool size_changed =
      (target_w != last_present_w_) || (target_h != last_present_h_);
  if (first_present_ || size_changed) {
    ihs::log::debug("[maplibre] OnPresent id={} target={}x{} (first={})", id_,
                    target_w, target_h, first_present_);
    first_present_ = false;
    last_present_w_ = target_w;
    last_present_h_ = target_h;
  }
  if (target_w <= 0 || target_h <= 0) {
    return true;
  }

  if (vulkan_renderer_) {
    return vulkan_renderer_->Render(target_w, target_h);
  }
  return false;
}

void MapLibreViewPlugin::OnResize(int32_t w, int32_t h) {
  pending_width_ = w;
  pending_height_ = h;
}

void* MapLibreViewPlugin::GetVulkanImage(int32_t* width,
                                         int32_t* height) const {
  if (!vulkan_renderer_ || vulkan_renderer_->image() == VK_NULL_HANDLE) {
    return nullptr;
  }
  if (width) {
    *width = vulkan_renderer_->width();
  }
  if (height) {
    *height = vulkan_renderer_->height();
  }
  return reinterpret_cast<void*>(vulkan_renderer_->image());
}

uint32_t MapLibreViewPlugin::GetVulkanImageLayout() const {
  return vulkan_renderer_ ? vulkan_renderer_->layout() : 0;
}

void MapLibreViewPlugin::SetVulkanImageLayout(uint32_t layout) {
  if (vulkan_renderer_) {
    vulkan_renderer_->set_layout(layout);
  }
}

// static
void MapLibreViewPlugin::on_resize(double width, double height, void* data) {
  if (auto* self = static_cast<MapLibreViewPlugin*>(data)) {
    self->OnResize(static_cast<int32_t>(width), static_cast<int32_t>(height));
  }
}

// static
void MapLibreViewPlugin::on_set_direction(int32_t direction, void* data) {
  (void)direction;
  (void)data;
}

// static
void MapLibreViewPlugin::on_set_offset(double left, double top, void* data) {
  (void)left;
  (void)top;
  (void)data;
}

// static
void MapLibreViewPlugin::on_touch(int32_t action,
                                  int32_t point_count,
                                  size_t point_data_size,
                                  const double* point_data,
                                  void* data) {
  // Pan/zoom lands with mbgl (Inc3); the placeholder ignores input.
  (void)action;
  (void)point_count;
  (void)point_data_size;
  (void)point_data;
  (void)data;
}

// static
void MapLibreViewPlugin::on_dispose(bool hybrid, void* data) {
  // The registry erases (and destroys) the owned instance and drops the
  // compositor surface after this returns; nothing to do here for the
  // placeholder.
  (void)hybrid;
  (void)data;
}

}  // namespace plugin_maplibre_view
