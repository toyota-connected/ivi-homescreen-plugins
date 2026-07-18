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

#include "maplibre_vulkan_renderer.h"

#include <algorithm>
#include <cstdint>

#include "config/common.h"  // BUILD_BACKEND_*_VULKAN

// vulkan.hpp with the process-shared dynamic dispatcher. A Vulkan backend
// (wayland-vulkan / drm-kms-vulkan) normally owns the loader storage
// (vk::detail::defaultDispatchLoaderDynamic) and initializes it with the
// instance + device; this plugin is just another consumer of that same
// dispatcher. On a build with NO Vulkan backend nothing else defines the
// storage, so the plugin must — otherwise the link fails with an undefined
// reference to defaultDispatchLoaderDynamic. The guard keeps exactly one
// definition across configs. Headers come from ivi-homescreen's vendored
// third_party/Vulkan-Headers via the plugin's include path.
#define VULKAN_HPP_NO_EXCEPTIONS 1
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#if !BUILD_BACKEND_WAYLAND_VULKAN && !BUILD_BACKEND_DRM_KMS_VULKAN
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#endif

#include "logging/logging.h"

namespace plugin_maplibre_view {

namespace {

const auto& d() {
  return vk::detail::defaultDispatchLoaderDynamic;
}

struct Rgb {
  uint8_t r, g, b;
};

// A recognizable slippy-map placeholder: light blue-grey field, a graticule
// grid (minor every 64 px, major every 256 px), a dark frame, and a red centre
// marker. Distinct from the layer_playground gradient at a glance, and
// normalized so it reads the same at any scale the compositor applies.
Rgb PlaceholderPixel(int32_t x, int32_t y, int32_t w, int32_t h) {
  constexpr Rgb kField{176, 196, 208};
  constexpr Rgb kMinor{140, 160, 172};
  constexpr Rgb kMajor{96, 116, 128};
  constexpr Rgb kFrame{40, 52, 58};
  constexpr Rgb kMarker{214, 58, 62};

  // 2 px frame.
  if (x < 2 || y < 2 || x >= w - 2 || y >= h - 2) {
    return kFrame;
  }
  // Centre marker: a small filled diamond (|dx|+|dy| <= r).
  const int32_t cx = w / 2;
  const int32_t cy = h / 2;
  const int32_t r = std::max(6, std::min(w, h) / 24);
  if (std::abs(x - cx) + std::abs(y - cy) <= r) {
    return kMarker;
  }
  // Graticule.
  if ((x % 256 == 0) || (y % 256 == 0)) {
    return kMajor;
  }
  if ((x % 64 == 0) || (y % 64 == 0)) {
    return kMinor;
  }
  return kField;
}

uint32_t PickHostVisibleMemory(VkPhysicalDevice pd, uint32_t type_bits) {
  VkPhysicalDeviceMemoryProperties mp{};
  d().vkGetPhysicalDeviceMemoryProperties(pd, &mp);
  const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
    if ((type_bits & (1u << i)) &&
        (mp.memoryTypes[i].propertyFlags & want) == want) {
      return i;
    }
  }
  return UINT32_MAX;
}

}  // namespace

bool MapLibreVulkanRenderer::Init(VkInstance instance,
                                  VkPhysicalDevice physical_device,
                                  VkDevice device,
                                  VkQueue queue,
                                  uint32_t queue_family_index) {
  instance_ = instance;
  physical_device_ = physical_device;
  device_ = device;
  queue_ = queue;
  queue_family_index_ = queue_family_index;
  return device_ != VK_NULL_HANDLE;
}

void MapLibreVulkanRenderer::DestroyImage() {
  if (image_ != VK_NULL_HANDLE) {
    d().vkDestroyImage(device_, image_, nullptr);
    image_ = VK_NULL_HANDLE;
  }
  if (memory_ != VK_NULL_HANDLE) {
    d().vkFreeMemory(device_, memory_, nullptr);
    memory_ = VK_NULL_HANDLE;
  }
  painted_ = false;
}

bool MapLibreVulkanRenderer::Render(int32_t width, int32_t height) {
  if (device_ == VK_NULL_HANDLE || width <= 0 || height <= 0) {
    return false;
  }
  ++gen_;
  ReapRetired();  // free aged-out images off the hot path (no device drain)

  // Grow-only: reuse while the current image is big enough.
  const bool fits =
      image_ != VK_NULL_HANDLE && width <= width_ && height <= height_;
  if (fits) {
    return painted_;
  }

  const int32_t new_w =
      image_ != VK_NULL_HANDLE ? std::max(width, width_) : width;
  const int32_t new_h =
      image_ != VK_NULL_HANDLE ? std::max(height, height_) : height;

  // Retire the outgoing image instead of draining the device.
  if (image_ != VK_NULL_HANDLE) {
    retired_.push_back({image_, memory_, gen_});
    image_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
    painted_ = false;
  }
  layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
  width_ = new_w;
  height_ = new_h;

  // Plain LINEAR host-visible image the CPU fills directly. initialLayout =
  // PREINITIALIZED so the host writes are preserved into the first GPU use (the
  // backend transitions PREINITIALIZED -> TRANSFER_SRC and blits/samples it).
  // No external/dma-buf memory: the plugin renders on the backend's own device,
  // so the compositor reads this VkImage directly.
  VkImageCreateInfo ic{};
  ic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ic.imageType = VK_IMAGE_TYPE_2D;
  ic.format = VK_FORMAT_B8G8R8A8_UNORM;
  ic.extent = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_),
               1};
  ic.mipLevels = 1;
  ic.arrayLayers = 1;
  ic.samples = VK_SAMPLE_COUNT_1_BIT;
  ic.tiling = VK_IMAGE_TILING_LINEAR;
  ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  ic.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
  if (d().vkCreateImage(device_, &ic, nullptr, &image_) != VK_SUCCESS) {
    ihs::log::error("MapLibreVulkan: vkCreateImage (linear) failed");
    image_ = VK_NULL_HANDLE;
    return false;
  }

  VkMemoryRequirements req{};
  d().vkGetImageMemoryRequirements(device_, image_, &req);
  const uint32_t mt =
      PickHostVisibleMemory(physical_device_, req.memoryTypeBits);
  if (mt == UINT32_MAX) {
    ihs::log::error("MapLibreVulkan: no host-visible memory type");
    DestroyImage();
    return false;
  }

  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = mt;
  if (d().vkAllocateMemory(device_, &mai, nullptr, &memory_) != VK_SUCCESS) {
    ihs::log::error("MapLibreVulkan: vkAllocateMemory failed");
    DestroyImage();
    return false;
  }
  if (d().vkBindImageMemory(device_, image_, memory_, 0) != VK_SUCCESS) {
    ihs::log::error("MapLibreVulkan: vkBindImageMemory failed");
    DestroyImage();
    return false;
  }

  VkImageSubresource sub{};
  sub.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  VkSubresourceLayout sl{};
  d().vkGetImageSubresourceLayout(device_, image_, &sub, &sl);
  row_pitch_ = sl.rowPitch;

  void* mapped = nullptr;
  if (d().vkMapMemory(device_, memory_, 0, VK_WHOLE_SIZE, 0, &mapped) !=
      VK_SUCCESS) {
    ihs::log::error("MapLibreVulkan: vkMapMemory failed");
    DestroyImage();
    return false;
  }
  auto* image_base = static_cast<uint8_t*>(mapped) + sl.offset;
  for (int32_t y = 0; y < height_; ++y) {
    auto* row = image_base + static_cast<size_t>(y) * row_pitch_;
    for (int32_t x = 0; x < width_; ++x) {
      const Rgb c = PlaceholderPixel(x, y, width_, height_);
      auto* px = row + static_cast<size_t>(x) * 4u;
      px[0] = c.b;
      px[1] = c.g;
      px[2] = c.r;
      px[3] = 0xff;
    }
  }
  d().vkUnmapMemory(device_, memory_);
  painted_ = true;
  layout_ = VK_IMAGE_LAYOUT_PREINITIALIZED;  // host writes preserved

  ihs::log::debug(
      "MapLibreVulkan: rendered {}x{} placeholder VkImage (pitch={}) on the "
      "compositor's device",
      width_, height_, row_pitch_);
  return true;
}

void MapLibreVulkanRenderer::ReapRetired() {
  constexpr uint64_t kRetireDepth = 4;
  retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                [this](const Retired& r) {
                                  if (gen_ - r.gen < kRetireDepth) {
                                    return false;
                                  }
                                  d().vkDestroyImage(device_, r.image, nullptr);
                                  d().vkFreeMemory(device_, r.memory, nullptr);
                                  return true;
                                }),
                 retired_.end());
}

MapLibreVulkanRenderer::~MapLibreVulkanRenderer() {
  if (device_ != VK_NULL_HANDLE) {
    for (const auto& r : retired_) {
      d().vkDestroyImage(device_, r.image, nullptr);
      d().vkFreeMemory(device_, r.memory, nullptr);
    }
    retired_.clear();
    DestroyImage();
  }
}

}  // namespace plugin_maplibre_view
