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

#include "layer_playground_vulkan.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

#include "config/common.h"  // BUILD_BACKEND_*_VULKAN

// vulkan.hpp with the process-shared dynamic dispatcher. A Vulkan backend
// (wayland-vulkan / drm-kms-vulkan) normally owns the loader storage
// (vk::detail::defaultDispatchLoaderDynamic) and initializes it with the
// instance + device; this plugin is just another consumer of that same
// dispatcher. But on a build with NO Vulkan backend (e.g. drm-kms-egl only,
// where this plugin still renders via GL/dma-buf), nothing else defines the
// storage, so the plugin must — otherwise the link fails with an undefined
// reference to defaultDispatchLoaderDynamic. The guard keeps exactly one
// definition: the plugin provides it only when no backend does. Headers come
// from ivi-homescreen's vendored third_party/Vulkan-Headers via the plugin's
// include path.
#define VULKAN_HPP_NO_EXCEPTIONS 1
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#if !BUILD_BACKEND_WAYLAND_VULKAN && !BUILD_BACKEND_DRM_KMS_VULKAN
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
#endif

#include "logging/logging.h"

namespace plugin_layer_playground_view {

namespace {

const auto& d() {
  return vk::detail::defaultDispatchLoaderDynamic;
}

// Diagonal 3-stop gradient matching the GL path's visual (deep blue -> violet
// -> warm amber), plus a thin border. B8G8R8A8 byte order (BGRA), which the
// compositor treats as XRGB8888.
struct Rgb {
  uint8_t r, g, b;
};
constexpr std::array<Rgb, 3> kStops = {
    {{30, 40, 120}, {130, 60, 160}, {235, 170, 60}}};

Rgb Lerp(const Rgb& a, const Rgb& b, float t) {
  const auto mix = [t](uint8_t x, uint8_t y) {
    return static_cast<uint8_t>(
        static_cast<float>(x) +
        (static_cast<float>(y) - static_cast<float>(x)) * t);
  };
  return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)};
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

bool LayerPlaygroundVulkanRenderer::Init(VkInstance instance,
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

void LayerPlaygroundVulkanRenderer::DestroyImage() {
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

bool LayerPlaygroundVulkanRenderer::Render(int32_t width, int32_t height) {
  if (device_ == VK_NULL_HANDLE || width <= 0 || height <= 0) {
    return false;
  }
  ++gen_;
  ReapRetired();  // free aged-out images off the hot path (no device drain)

  // Grow-only: the compositor scales the whole image into the exact layer rect
  // and the gradient is normalized (looks identical at any scale), so a larger
  // image serves any smaller box. Reuse while the current image is big enough;
  // only a box that exceeds it triggers a reallocation. An animated shrink — or
  // a grow within the current size — does no work, so a preset transition no
  // longer reallocates (and stalls) every frame.
  const bool fits =
      image_ != VK_NULL_HANDLE && width <= width_ && height <= height_;
  if (fits) {
    return painted_;
  }

  const int32_t new_w =
      image_ != VK_NULL_HANDLE ? std::max(width, width_) : width;
  const int32_t new_h =
      image_ != VK_NULL_HANDLE ? std::max(height, height_) : height;

  // Retire the outgoing image instead of draining the device: a prior frame's
  // compositor read may still be in flight, so hand it to ReapRetired to free a
  // few generations later.
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
  // PREINITIALIZED so the host writes are preserved into the first GPU use
  // (the backend transitions PREINITIALIZED -> TRANSFER_SRC and blits it). No
  // external/dma-buf memory: the plugin renders on the backend's own device,
  // so the compositor reads this VkImage directly — dma-buf would force
  // initialLayout=UNDEFINED, which discards the host-written contents (black).
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
    ihs::log::error("LayerPlaygroundVulkan: vkCreateImage (linear) failed");
    image_ = VK_NULL_HANDLE;
    return false;
  }

  VkMemoryRequirements req{};
  d().vkGetImageMemoryRequirements(device_, image_, &req);
  const uint32_t mt =
      PickHostVisibleMemory(physical_device_, req.memoryTypeBits);
  if (mt == UINT32_MAX) {
    ihs::log::error("LayerPlaygroundVulkan: no host-visible memory type");
    DestroyImage();
    return false;
  }

  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = mt;
  if (d().vkAllocateMemory(device_, &mai, nullptr, &memory_) != VK_SUCCESS) {
    ihs::log::error("LayerPlaygroundVulkan: vkAllocateMemory failed");
    DestroyImage();
    return false;
  }
  if (d().vkBindImageMemory(device_, image_, memory_, 0) != VK_SUCCESS) {
    ihs::log::error("LayerPlaygroundVulkan: vkBindImageMemory failed");
    DestroyImage();
    return false;
  }

  // Row pitch of the linear layout (bytes between rows).
  VkImageSubresource sub{};
  sub.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  VkSubresourceLayout sl{};
  d().vkGetImageSubresourceLayout(device_, image_, &sub, &sl);
  row_pitch_ = sl.rowPitch;

  // CPU-fill the gradient (host-coherent, so no flush needed).
  void* mapped = nullptr;
  if (d().vkMapMemory(device_, memory_, 0, VK_WHOLE_SIZE, 0, &mapped) !=
      VK_SUCCESS) {
    ihs::log::error("LayerPlaygroundVulkan: vkMapMemory failed");
    DestroyImage();
    return false;
  }
  auto* image_base = static_cast<uint8_t*>(mapped) + sl.offset;
  const auto fw = static_cast<float>(width_ - 1 > 0 ? width_ - 1 : 1);
  const auto fh = static_cast<float>(height_ - 1 > 0 ? height_ - 1 : 1);
  for (int32_t y = 0; y < height_; ++y) {
    auto* row = image_base + static_cast<size_t>(y) * row_pitch_;
    for (int32_t x = 0; x < width_; ++x) {
      const float t =
          (static_cast<float>(x) / fw + static_cast<float>(y) / fh) * 0.5f;
      Rgb c = t < 0.5f ? Lerp(kStops[0], kStops[1], t * 2.0f)
                       : Lerp(kStops[1], kStops[2], (t - 0.5f) * 2.0f);
      // Thin 2px border.
      const bool border = x < 2 || y < 2 || x >= width_ - 2 || y >= height_ - 2;
      if (border) {
        c = {235, 235, 235};
      }
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
      "LayerPlaygroundVulkan: rendered {}x{} VkImage (pitch={}) on Flutter's "
      "device",
      width_, height_, row_pitch_);
  return true;
}

void LayerPlaygroundVulkanRenderer::ReapRetired() {
  // A retired image can still be read by a compositor submit in flight when it
  // is superseded; freeing it kRetireDepth generations later clears any such
  // read (the dma-buf present path keeps only a few frames outstanding). Runs
  // on the rasterizer thread that issues those submits, so no device drain
  // needed.
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

LayerPlaygroundVulkanRenderer::~LayerPlaygroundVulkanRenderer() {
  if (device_ != VK_NULL_HANDLE) {
    for (const auto& r : retired_) {
      d().vkDestroyImage(device_, r.image, nullptr);
      d().vkFreeMemory(device_, r.memory, nullptr);
    }
    retired_.clear();
    DestroyImage();
  }
}

}  // namespace plugin_layer_playground_view
