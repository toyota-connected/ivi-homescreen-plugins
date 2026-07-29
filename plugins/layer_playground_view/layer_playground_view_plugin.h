/*
 * Copyright 2020-2023 Toyota Connected North America
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

#ifndef FLUTTER_PLUGIN_LAYER_PLAYGROUND_PLUGIN_H_
#define FLUTTER_PLUGIN_LAYER_PLAYGROUND_PLUGIN_H_

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <GLES2/gl2.h>
#include <flutter/plugin_registrar.h>

#include "config/common.h"

// This is a platform-view plugin: every piece of its rendering path
// (ICompositorSurface, RegisterCompositorSurface, GetGlTextureName /
// GetDmabuf) is gated on BUILD_COMPOSITOR (defined by the generated
// config/common.h above). Built without it, the plugin compiles to an inert
// stub — it registers but produces no native content — matching the other
// compositor plugins (comp_surf, comp_region). A hard #error here would break
// the CI static-analysis build (clang-tidy / CodeQL configure the compositor
// off), so the graceful #if BUILD_COMPOSITOR gating below is the convention.

#include "flutter_desktop_engine_state.h"
#include "flutter_homescreen.h"
#include "platform_views/platform_view.h"

#if BUILD_COMPOSITOR
#include "view/compositor_surface_interface.h"
#endif

struct gbm_device;
struct gbm_bo;

namespace plugin_layer_playground_view {

#if BUILD_COMPOSITOR
class LayerPlaygroundVulkanRenderer;
#endif

/**
 * Layer playground demo platform view.
 *
 * Renders a diagonal 3-stop gradient with a thin border and a faint grid
 * into an FBO-backed @c GL_TEXTURE_2D which the compositor composites into
 * the scene at the layer's @c offset / @c size each frame. The visual
 * design mirrors the GTK/Cairo @c simple_box companion plugin in the
 * layer_playground app (minus the text label). No Wayland subsurface, no
 * per-plugin EGL context — all GL state is created lazily in @c OnPresent
 * on the engine's rasterizer thread using the engine's GL context.
 */
class LayerPlaygroundViewPlugin : public flutter::Plugin,
                                  public PlatformView
#if BUILD_COMPOSITOR
    ,
                                  public ICompositorSurface
#endif
{
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrar* registrar,
                                    int32_t id,
                                    std::string viewType,
                                    int32_t direction,
                                    double top,
                                    double left,
                                    double width,
                                    double height,
                                    const std::vector<uint8_t>& params,
                                    const std::string& assetDirectory,
                                    FlutterDesktopEngineRef engine,
                                    PlatformViewAddListener addListener,
                                    PlatformViewRemoveListener removeListener,
                                    void* platform_view_context);

  LayerPlaygroundViewPlugin(int32_t id,
                            std::string viewType,
                            int32_t direction,
                            double top,
                            double left,
                            double width,
                            double height,
                            const std::vector<uint8_t>& params,
                            const std::string& assetDirectory,
                            FlutterDesktopEngineState* state,
                            PlatformViewAddListener addListener,
                            PlatformViewRemoveListener removeListener,
                            void* platform_view_context);

  ~LayerPlaygroundViewPlugin() override;

  LayerPlaygroundViewPlugin(const LayerPlaygroundViewPlugin&) = delete;
  LayerPlaygroundViewPlugin& operator=(const LayerPlaygroundViewPlugin&) =
      delete;

#if BUILD_COMPOSITOR
  // ICompositorSurface
  bool OnCreateBackingStore(const FlutterBackingStoreConfig*,
                            FlutterBackingStore*) override {
    return false;  // engine provides backing store; we render into our own FBO
  }
  bool OnCollectBackingStore(const FlutterBackingStore*) override {
    return true;
  }
  bool OnPresent(const FlutterLayer* layer) override;
  [[nodiscard]] FlutterPlatformViewIdentifier GetIdentifier() const override {
    return id_;
  }
  void OnResize(int32_t w, int32_t h) override;

  [[nodiscard]] uint32_t GetGlTextureName() const override {
    return color_texture_;
  }
  [[nodiscard]] int32_t GetGlTextureWidth() const override {
    return tex_width_;
  }
  [[nodiscard]] int32_t GetGlTextureHeight() const override {
    return tex_height_;
  }
  // Vulkan path: hand the compositor the VkImage rendered on Flutter's device.
  [[nodiscard]] void* GetVulkanImage(int32_t* width,
                                     int32_t* height) const override;
  [[nodiscard]] uint32_t GetVulkanImageLayout() const override;
  void SetVulkanImageLayout(uint32_t layout) override;

  // Direct-scanout stub: when IVI_PV_DMABUF is set, expose a solid-color
  // dma-buf so the DRM compositor routes this view onto a KMS overlay plane
  // instead of GL-compositing it. Returns false (GL path) otherwise.
  [[nodiscard]] bool GetDmabuf(Dmabuf* out) const override;
#endif

 private:
  int32_t id_;
  void* platformViewsContext_;
  PlatformViewRemoveListener removeListener_;

#if BUILD_COMPOSITOR
  // Owned FBO state, lazily created on the rasterizer thread when
  // OnPresent first runs (so the engine's GL context is current).
  bool gl_initialized_{false};
  GLuint framebuffer_{0};
  GLuint color_texture_{0};
  GLuint program_{0};
  GLuint vbo_{0};
  GLint u_resolution_loc_{-1};
  GLint a_position_loc_{-1};
  int32_t tex_width_{0};
  int32_t tex_height_{0};
  // Pending size from on_resize / OnResize, applied next OnPresent.
  std::atomic<int32_t> pending_width_{0};
  std::atomic<int32_t> pending_height_{0};
  // Per-view size seen at the last present, so the trace/size-change check is
  // scoped to this view (not shared across every view on the rasterizer
  // thread).
  int32_t last_present_w_{0};
  int32_t last_present_h_{0};
  bool first_present_{true};

  // Optional Vulkan render path: non-null when the active backend is Vulkan
  // (no engine GL context). Reuses Flutter's device to write the gradient into
  // a VkImage instead of the GL FBO. See layer_playground_vulkan.h.
  std::unique_ptr<LayerPlaygroundVulkanRenderer> vulkan_renderer_;

  // Direct-scanout stub state (IVI_PV_DMABUF). A single solid-color scanout
  // buffer, lazily allocated from the backend's gbm_device on first GetDmabuf
  // and exported once as a stable dma-buf fd (so the compositor's per-fd KMS
  // framebuffer cache stays hot). Mutable: GetDmabuf is const but memoizes.
  mutable gbm_device* pv_gbm_device_{nullptr};
  mutable gbm_bo* pv_dmabuf_bo_{nullptr};
  mutable int pv_dmabuf_fd_{-1};
  mutable uint32_t pv_dmabuf_fourcc_{0};
  mutable uint64_t pv_dmabuf_modifier_{0};
  // Per-plane layout (one bo; NV12/P010 carry 2 planes at distinct offsets).
  mutable uint32_t pv_dmabuf_planes_{1};
  mutable uint32_t pv_dmabuf_stride_[4]{};
  mutable uint32_t pv_dmabuf_offset_[4]{};
  mutable uint8_t pv_dmabuf_color_space_{0};  // IhsColorSpace (YUV only)
  mutable uint8_t pv_dmabuf_color_range_{0};  // IhsColorRange
  mutable int32_t pv_dmabuf_w_{0};
  mutable int32_t pv_dmabuf_h_{0};
  mutable int pv_dmabuf_enabled_{-1};  // -1 unknown, 0 off, 1 on

  void EnsureGlState(int32_t w, int32_t h);
  void DestroyGlState();
  void DrawFrame() const;
#endif

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

}  // namespace plugin_layer_playground_view

#endif  // FLUTTER_PLUGIN_LAYER_PLAYGROUND_PLUGIN_H_
