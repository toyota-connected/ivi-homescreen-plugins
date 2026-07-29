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

#include "layer_playground_view_plugin.h"

#include <flutter/standard_message_codec.h>
#include <memory>

#include "plugins/common/common.h"
#include "view/flutter_view.h"

#if BUILD_COMPOSITOR
#include "backend/backend.h"
#include "layer_playground_vulkan.h"

#if BUILD_COMPOSITOR
#include <cstdlib>
#include <cstring>
#include <string_view>

#include <gbm.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#endif

class FlutterView;
class Display;

namespace plugin_layer_playground_view {

void LayerPlaygroundViewPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrar* registrar,
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
    void* platform_view_context) {
  auto plugin = std::make_unique<LayerPlaygroundViewPlugin>(
      id, std::move(viewType), direction, top, left, width, height, params,
      assetDirectory, engine, addListener, removeListener,
      platform_view_context);
  registrar->AddPlugin(std::move(plugin));
}

LayerPlaygroundViewPlugin::LayerPlaygroundViewPlugin(
    int32_t id,
    std::string viewType,
    int32_t direction,
    double top,
    double left,
    double width,
    double height,
    const std::vector<uint8_t>& /* params */,
    const std::string& /* assetDirectory */,
    FlutterDesktopEngineState* state,
    PlatformViewAddListener addListener,
    PlatformViewRemoveListener removeListener,
    void* platform_view_context)
    : PlatformView(id,
                   std::move(viewType),
                   direction,
                   top,
                   left,
                   width,
                   height),
      id_(id),
      platformViewsContext_(platform_view_context),
      removeListener_(removeListener) {
  IHS_TRACE("++LayerPlaygroundViewPlugin::LayerPlaygroundViewPlugin");

#if BUILD_COMPOSITOR
  pending_width_ = static_cast<int32_t>(width);
  pending_height_ = static_cast<int32_t>(height);

  // Register so PresentLayers routes layer dispatch to OnPresent. GL state
  // is allocated lazily on the rasterizer thread the first time OnPresent
  // fires — the engine's context isn't current here on the platform thread.
  if (state && state->view_controller && state->view_controller->view) {
    auto* view = state->view_controller->view;
    view->RegisterCompositorSurface(
        id_, std::shared_ptr<ICompositorSurface>(this, [](ICompositorSurface*) {
          // Aliasing deleter: the plugin's lifetime is owned by the
          // PluginRegistrar, not by this shared_ptr. The compositor's
          // copy is dropped on UnregisterCompositorSurface.
        }));
    IHS_TRACE("[pv-trace] LayerPlaygroundView registered: id={} size={}x{}",
              id_, pending_width_.load(), pending_height_.load());

    // If the active backend is Vulkan there is no engine GL context, so the GL
    // path would fail. Reuse the backend's Vulkan device to render into a
    // VkImage instead. GL stays the path on EGL backends.
    if (auto* backend = view->GetBackend()) {
      // Capture the backend's gbm_device (DRM/KMS EGL backend only) so the
      // direct-scanout stub can allocate a scanout buffer without reaching
      // back through the view from the const GetDmabuf accessor.
      if (BackendEglContext egl{}; backend->GetEglContext(&egl)) {
        pv_gbm_device_ = static_cast<gbm_device*>(egl.gbm_device);
      }
      BackendVulkanContext vk{};
      if (backend->GetVulkanContext(&vk)) {
        auto renderer = std::make_unique<LayerPlaygroundVulkanRenderer>();
        if (renderer->Init(static_cast<VkInstance>(vk.instance),
                           static_cast<VkPhysicalDevice>(vk.physical_device),
                           static_cast<VkDevice>(vk.device),
                           static_cast<VkQueue>(vk.queue),
                           vk.queue_family_index)) {
          vulkan_renderer_ = std::move(renderer);
          IHS_TRACE(
              "[pv-trace] LayerPlaygroundView: Vulkan render path active "
              "(reusing Flutter's device) id={}",
              id_);
        }
      }
    }
  } else {
    IHS_TRACE(
        "[pv-trace] LayerPlaygroundView could NOT register (state/view null): "
        "id={}",
        id_);
  }
#else
  (void)state;
#endif

  addListener(platformViewsContext_, id, &platform_view_listener_, this);
  IHS_TRACE("--LayerPlaygroundViewPlugin::LayerPlaygroundViewPlugin");
}

LayerPlaygroundViewPlugin::~LayerPlaygroundViewPlugin() {
  removeListener_(platformViewsContext_, id_);
#if BUILD_COMPOSITOR
  if (pv_dmabuf_fd_ >= 0) {
    ::close(pv_dmabuf_fd_);
    pv_dmabuf_fd_ = -1;
  }
  if (pv_dmabuf_bo_) {
    gbm_bo_destroy(pv_dmabuf_bo_);
    pv_dmabuf_bo_ = nullptr;
  }
#endif
}

void LayerPlaygroundViewPlugin::on_resize(double width,
                                          double height,
                                          void* data) {
  if (auto* plugin = static_cast<LayerPlaygroundViewPlugin*>(data)) {
    plugin->width_ = static_cast<int32_t>(width);
    plugin->height_ = static_cast<int32_t>(height);
#if BUILD_COMPOSITOR
    plugin->pending_width_ = static_cast<int32_t>(width);
    plugin->pending_height_ = static_cast<int32_t>(height);
#endif
    IHS_TRACE("Resize: {} {}", width, height);
  }
}

void LayerPlaygroundViewPlugin::on_set_direction(const int32_t direction,
                                                 void* data) {
  if (auto* plugin = static_cast<LayerPlaygroundViewPlugin*>(data)) {
    plugin->direction_ = direction;
    IHS_TRACE("SetDirection: {}", plugin->direction_);
  }
}

void LayerPlaygroundViewPlugin::on_set_offset(const double left,
                                              const double top,
                                              void* data) {
  // Offset positioning is handled by the compositor sequencer in
  // BUILD_COMPOSITOR mode; we just track the values for diagnostics.
  if (auto* plugin = static_cast<LayerPlaygroundViewPlugin*>(data)) {
    plugin->left_ = static_cast<int32_t>(left);
    plugin->top_ = static_cast<int32_t>(top);
    IHS_TRACE("SetOffset: {} {}", left, top);
  }
}

void LayerPlaygroundViewPlugin::on_touch(int32_t /* action */,
                                         int32_t /* point_count */,
                                         const size_t /* point_data_size */,
                                         const double* /* point_data */,
                                         void* /* data */) {}

void LayerPlaygroundViewPlugin::on_dispose(bool /* hybrid */, void* data) {
  // Compositor-owned shared_ptr drops via PlatformViewsHandler's
  // safety-net UnregisterCompositorSurface call. GL state is leaked
  // intentionally — destroying GL handles requires the engine's context
  // to be current on the rasterizer thread, and we have no way to
  // marshal that from the platform-thread dispose call. The engine's
  // context outlives the plugin and reclaims the resources at shutdown.
#if BUILD_COMPOSITOR
  // The Vulkan path, unlike GL, has no thread-affine context: its VkImage +
  // memory can be freed here (the device is still alive at dispose), which also
  // keeps them from outliving vkDestroyDevice. The images hold no in-flight GPU
  // work (CPU-filled), so this is safe from the platform thread.
  if (auto* plugin = static_cast<LayerPlaygroundViewPlugin*>(data)) {
    plugin->vulkan_renderer_.reset();
  }
#else
  (void)data;
#endif
}

const platform_view_listener
    LayerPlaygroundViewPlugin::platform_view_listener_ = {
        .resize = on_resize,
        .set_direction = on_set_direction,
        .set_offset = on_set_offset,
        .on_touch = on_touch,
        .dispose = on_dispose,
        .accept_gesture = nullptr,
        .reject_gesture = nullptr,
};

#if BUILD_COMPOSITOR

namespace {

GLuint LoadShader(const GLchar* src, GLenum type) {
  const GLuint shader = glCreateShader(type);
  if (!shader) {
    return 0;
  }
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);
  GLint compiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    GLint len = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
    std::string log(static_cast<size_t>(len > 0 ? len : 0), '\0');
    if (len > 0) {
      glGetShaderInfoLog(shader, len, nullptr, log.data());
    }
    ihs::log::error("LayerPlaygroundViewPlugin: shader compile failed: {}",
                    log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

// Mirrors the simple_box_plugin Cairo pattern: a diagonal 3-stop gradient
// (deep purple → warm coral → peach), a thin orange border, and a faint
// 20-pixel white grid. Label text from the GTK variant is omitted — GLES2
// font rendering would require a glyph atlas we don't want to pull in.
constexpr GLchar kVertSrc[] =
    "attribute vec2 aPosition;\n"
    "varying vec2 vUv;\n"
    "void main() {\n"
    "  vUv = aPosition * 0.5 + 0.5;\n"
    "  gl_Position = vec4(aPosition, 0.0, 1.0);\n"
    "}\n";

constexpr GLchar kFragSrc[] =
    "precision mediump float;\n"
    "uniform vec2 uResolution;\n"
    "varying vec2 vUv;\n"
    "vec3 gradient(float t) {\n"
    "  vec3 c0 = vec3(0.227, 0.110, 0.443);\n"  // #3A1C71
    "  vec3 c1 = vec3(0.843, 0.427, 0.467);\n"  // #D76D77
    "  vec3 c2 = vec3(1.000, 0.686, 0.482);\n"  // #FFAF7B
    "  return t < 0.5 ? mix(c0, c1, t * 2.0) : mix(c1, c2, (t - 0.5) * 2.0);\n"
    "}\n"
    "void main() {\n"
    "  vec2 px = vUv * uResolution;\n"
    "  float t = clamp((vUv.x + vUv.y) * 0.5, 0.0, 1.0);\n"
    "  vec3 color = gradient(t);\n"
    "  vec2 d = min(px, uResolution - px);\n"
    "  float edge = min(d.x, d.y);\n"
    "  if (edge < 2.0) {\n"
    "    color = mix(color, vec3(1.0, 0.65, 0.0), 0.9);\n"
    "  }\n"
    "  vec2 g = fract(px / 20.0) * 20.0;\n"
    "  float grid = step(g.x, 1.0) + step(g.y, 1.0);\n"
    "  color = mix(color, vec3(1.0), 0.12 * clamp(grid, 0.0, 1.0));\n"
    "  gl_FragColor = vec4(color, 1.0);\n"
    "}\n";

}  // namespace

void LayerPlaygroundViewPlugin::EnsureGlState(int32_t w, int32_t h) {
  if (gl_initialized_ && w == tex_width_ && h == tex_height_) {
    return;
  }

  // Re-create the texture / FBO on size change. Program is reused across
  // resizes.
  if (color_texture_) {
    glDeleteTextures(1, &color_texture_);
    color_texture_ = 0;
  }
  if (framebuffer_) {
    glDeleteFramebuffers(1, &framebuffer_);
    framebuffer_ = 0;
  }

  tex_width_ = w;
  tex_height_ = h;

  glGenTextures(1, &color_texture_);
  glBindTexture(GL_TEXTURE_2D, color_texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_width_, tex_height_, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);

  glGenFramebuffers(1, &framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         color_texture_, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    ihs::log::error("LayerPlaygroundViewPlugin: FBO incomplete at {}x{}", w, h);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (!program_) {
    const GLuint vs = LoadShader(kVertSrc, GL_VERTEX_SHADER);
    const GLuint fs = LoadShader(kFragSrc, GL_FRAGMENT_SHADER);
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glBindAttribLocation(program_, 0, "aPosition");
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) {
      GLint len = 0;
      glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &len);
      std::string log(static_cast<size_t>(len > 0 ? len : 0), '\0');
      if (len > 0) {
        glGetProgramInfoLog(program_, len, nullptr, log.data());
      }
      ihs::log::error("LayerPlaygroundViewPlugin: program link failed: {}",
                      log);
      glDeleteProgram(program_);
      program_ = 0;
    } else {
      u_resolution_loc_ = glGetUniformLocation(program_, "uResolution");
      a_position_loc_ = glGetAttribLocation(program_, "aPosition");
    }
  }

  // A VBO is required: under GLES3 the engine has a VAO bound, and
  // client-side vertex arrays are invalid unless the default VAO (0) is
  // current. A VBO works regardless of which VAO is bound.
  if (!vbo_ && program_) {
    static constexpr GLfloat kVerts[] = {
        -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f,
    };
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVerts), kVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  gl_initialized_ = (program_ != 0 && vbo_ != 0);
}

void LayerPlaygroundViewPlugin::DestroyGlState() {
  // Intentionally not called from the platform thread — see on_dispose.
  if (color_texture_) {
    glDeleteTextures(1, &color_texture_);
    color_texture_ = 0;
  }
  if (framebuffer_) {
    glDeleteFramebuffers(1, &framebuffer_);
    framebuffer_ = 0;
  }
  if (vbo_) {
    glDeleteBuffers(1, &vbo_);
    vbo_ = 0;
  }
  if (program_) {
    glDeleteProgram(program_);
    program_ = 0;
  }
  gl_initialized_ = false;
}

void LayerPlaygroundViewPlugin::DrawFrame() const {
  // Flutter's Skia backend leaves assorted GL state on: a bound VAO, depth
  // test, scissor test, blend, colour/depth masks. Reset everything we care
  // about so the draw lands in the FBO as expected.
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
  glViewport(0, 0, tex_width_, tex_height_);

  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_FALSE);

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(program_);
  if (u_resolution_loc_ >= 0) {
    glUniform2f(u_resolution_loc_, static_cast<GLfloat>(tex_width_),
                static_cast<GLfloat>(tex_height_));
  }
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  const GLuint loc =
      a_position_loc_ >= 0 ? static_cast<GLuint>(a_position_loc_) : 0u;
  glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(loc);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(loc);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool LayerPlaygroundViewPlugin::OnPresent(const FlutterLayer* layer) {
  // The engine supplies the composed layer size every frame in layer->size;
  // trust that over on_resize, which the embedder may never deliver while the
  // widget is laid out (the platform-views `create` message carries 1x1
  // placeholders). Fall back to pending_*/ atomic if the layer is absent.
  int32_t target_w = pending_width_.load();
  int32_t target_h = pending_height_.load();
  if (layer && layer->size.width > 0 && layer->size.height > 0) {
    target_w = static_cast<int32_t>(layer->size.width);
    target_h = static_cast<int32_t>(layer->size.height);
  }
  const bool size_changed =
      (target_w != last_present_w_) || (target_h != last_present_h_);
  if (first_present_ || size_changed) {
    IHS_TRACE(
        "[pv-trace] LayerPlaygroundView::OnPresent id={} target={}x{} "
        "tex={}x{} gl_init={} (first={}, size_changed={})",
        id_, target_w, target_h, tex_width_, tex_height_, gl_initialized_,
        first_present_, size_changed);
    first_present_ = false;
    last_present_w_ = target_w;
    last_present_h_ = target_h;
  }
  if (target_w <= 0 || target_h <= 0) {
    return true;
  }

  // Vulkan backend: render into a VkImage on Flutter's device instead of GL.
  if (vulkan_renderer_) {
    return vulkan_renderer_->Render(target_w, target_h);
  }

  EnsureGlState(target_w, target_h);
  if (!gl_initialized_) {
    IHS_TRACE(
        "[pv-trace] LayerPlaygroundView::OnPresent id={} gl NOT initialised "
        "(program link failed?); returning false",
        id_);
    return false;
  }
  DrawFrame();
  (void)layer;
  return true;
}

void* LayerPlaygroundViewPlugin::GetVulkanImage(int32_t* width,
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

uint32_t LayerPlaygroundViewPlugin::GetVulkanImageLayout() const {
  return vulkan_renderer_ ? vulkan_renderer_->layout() : 0;
}

void LayerPlaygroundViewPlugin::SetVulkanImageLayout(uint32_t layout) {
  if (vulkan_renderer_) {
    vulkan_renderer_->set_layout(layout);
  }
}

bool LayerPlaygroundViewPlugin::GetDmabuf(Dmabuf* out) const {
  if (!out) {
    return false;
  }
  // Opt-in gate so the default GL-composite path is unchanged. Evaluated once.
  if (pv_dmabuf_enabled_ < 0) {
    const char* env = std::getenv("IVI_PV_DMABUF");
    pv_dmabuf_enabled_ = (env && env[0] == '1') ? 1 : 0;
    IHS_TRACE("[pv-trace] GetDmabuf gate id={} enabled={} gbm_device={}", id_,
              pv_dmabuf_enabled_, static_cast<const void*>(pv_gbm_device_));
  }
  if (pv_dmabuf_enabled_ == 0 || !pv_gbm_device_) {
    return false;
  }

  // Target the view's current extent. Flutter registers platform views at
  // 1x1 and resizes them once laid out; a 1x1 scanout buffer is rejected by
  // AddFB2, so skip (GL-composite this frame) until the real size arrives,
  // and rebuild if the view later resizes.
  const int32_t w = pending_width_.load();
  const int32_t h = pending_height_.load();
  if (w <= 1 || h <= 1) {
    return false;
  }
  if (pv_dmabuf_bo_ && (pv_dmabuf_w_ != w || pv_dmabuf_h_ != h)) {
    if (pv_dmabuf_fd_ >= 0) {
      ::close(pv_dmabuf_fd_);
      pv_dmabuf_fd_ = -1;
    }
    gbm_bo_destroy(pv_dmabuf_bo_);
    pv_dmabuf_bo_ = nullptr;
  }

  // Lazily (re)allocate one solid-color scanout buffer and export a stable
  // dma-buf fd. LINEAR + SCANOUT keeps it plane-scannable everywhere (incl.
  // vkms) and CPU-mappable so we can fill it without a GPU.
  if (!pv_dmabuf_bo_) {
    // Format select: LP_PV_DMABUF_FOURCC=nv12 exercises the YUV plane path
    // (ContentType::Video + plane CSC); default XRGB8888 is the RGB path.
    // GBM_FORMAT_* == DRM_FORMAT_* (same fourcc), so the KMS AddFB2 in the
    // compositor sees exactly this format.
    const char* fmt_env = std::getenv("LP_PV_DMABUF_FOURCC");
    const bool want_nv12 =
        fmt_env != nullptr && (std::string_view(fmt_env) == "nv12" ||
                               std::string_view(fmt_env) == "NV12");
    uint32_t gbm_fmt = want_nv12 ? GBM_FORMAT_NV12 : GBM_FORMAT_XRGB8888;
    gbm_bo* bo = gbm_bo_create(pv_gbm_device_, static_cast<uint32_t>(w),
                               static_cast<uint32_t>(h), gbm_fmt,
                               GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
    bool is_nv12 = want_nv12;
    if (!bo && want_nv12) {
      IHS_TRACE(
          "[pv-trace] GetDmabuf NV12 unsupported by gbm id={}; XRGB fallback",
          id_);
      is_nv12 = false;
      bo = gbm_bo_create(pv_gbm_device_, static_cast<uint32_t>(w),
                         static_cast<uint32_t>(h), GBM_FORMAT_XRGB8888,
                         GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
    }
    if (!bo) {
      IHS_TRACE("[pv-trace] GetDmabuf gbm_bo_create failed id={} {}x{}", id_, w,
                h);
      pv_dmabuf_enabled_ = 0;  // don't retry every present
      return false;
    }

    // A distinct opaque color per view id, so each native box is visually
    // identifiable and the on-screen z-order is legible. 0xXX_RR_GG_BB.
    static constexpr uint32_t kPalette[] = {
        0xFFFF0000u,  // 0 red
        0xFF00C000u,  // 1 green
        0xFF0000FFu,  // 2 blue
        0xFFFFC000u,  // 3 amber
        0xFFFF00FFu,  // 4 magenta
        0xFF00E0E0u,  // 5 cyan
        0xFFFF8000u,  // 6 orange
        0xFF8000FFu,  // 7 violet
        0xFFFFFFFFu,  // 8 white
        0xFF808080u,  // 9 gray
    };
    const uint32_t palette_n =
        static_cast<uint32_t>(sizeof(kPalette) / sizeof(kPalette[0]));
    const uint32_t rgb = kPalette[static_cast<uint32_t>(id_) % palette_n];

    // Per-plane layout from gbm (XRGB: 1 plane; NV12: Y + interleaved UV).
    const uint32_t planes = static_cast<uint32_t>(gbm_bo_get_plane_count(bo));
    pv_dmabuf_planes_ = planes < 4 ? planes : 4;
    for (uint32_t p = 0; p < pv_dmabuf_planes_; ++p) {
      pv_dmabuf_stride_[p] = gbm_bo_get_stride_for_plane(bo, p);
      pv_dmabuf_offset_[p] = gbm_bo_get_offset(bo, p);
    }
    pv_dmabuf_bo_ = bo;
    pv_dmabuf_fd_ = gbm_bo_get_fd(bo);  // persistent exported fd

    if (!is_nv12) {
      const uint32_t border = 0xFF202020u;  // ~1px darker frame, edge-legible
      uint32_t map_stride = 0;
      void* map_data = nullptr;
      if (void* px = gbm_bo_map(bo, 0, 0, static_cast<uint32_t>(w),
                                static_cast<uint32_t>(h), GBM_BO_TRANSFER_WRITE,
                                &map_stride, &map_data)) {
        auto* base = static_cast<uint8_t*>(px);
        for (int32_t y = 0; y < h; ++y) {
          auto* row = reinterpret_cast<uint32_t*>(base + y * map_stride);
          const bool edge_row = (y < 2 || y >= h - 2);
          for (int32_t x = 0; x < w; ++x) {
            const bool edge = edge_row || x < 2 || x >= w - 2;
            row[x] = edge ? border : rgb;
          }
        }
        gbm_bo_unmap(bo, map_data);
      }
      pv_dmabuf_color_space_ = 0;  // IHS_COLOR_SPACE_DEFAULT (RGB)
      pv_dmabuf_color_range_ = 0;
    } else {
      // Solid-color NV12: fill Y + interleaved UV by mmap'ing the LINEAR
      // dma-buf directly (gbm_bo_map's plane-0 window can't reach the UV
      // plane). Convert the palette RGB -> BT.709 limited Y'CbCr so the plane
      // CSC decodes it back to that color — validating the CSC, not just
      // placement.
      const double r = static_cast<double>((rgb >> 16) & 0xFF);
      const double g = static_cast<double>((rgb >> 8) & 0xFF);
      const double b = static_cast<double>(rgb & 0xFF);
      auto clamp8 = [](double v) {
        return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
      };
      const uint8_t yv = clamp8(16.0 + 0.1826 * r + 0.6142 * g + 0.0620 * b);
      const uint8_t uv_u = clamp8(128.0 - 0.1006 * r - 0.3386 * g + 0.4392 * b);
      const uint8_t uv_v = clamp8(128.0 + 0.4392 * r - 0.3989 * g - 0.0403 * b);
      const uint32_t sy = pv_dmabuf_stride_[0];
      const uint32_t suv = pv_dmabuf_stride_[1];
      const uint32_t oy = pv_dmabuf_offset_[0];
      const uint32_t ouv = pv_dmabuf_offset_[1];
      const size_t size = ouv + static_cast<size_t>(suv) * (h / 2);
      void* m = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       pv_dmabuf_fd_, 0);
      if (m != MAP_FAILED) {
        dma_buf_sync sync{};
        sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE;
        ::ioctl(pv_dmabuf_fd_, DMA_BUF_IOCTL_SYNC, &sync);
        auto* base = static_cast<uint8_t*>(m);
        for (int32_t y = 0; y < h; ++y) {
          std::memset(base + oy + static_cast<size_t>(y) * sy, yv,
                      static_cast<size_t>(w));
        }
        for (int32_t y = 0; y < h / 2; ++y) {
          uint8_t* uvrow = base + ouv + static_cast<size_t>(y) * suv;
          for (int32_t x = 0; x < w / 2; ++x) {
            uvrow[2 * x] = uv_u;
            uvrow[2 * x + 1] = uv_v;
          }
        }
        sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
        ::ioctl(pv_dmabuf_fd_, DMA_BUF_IOCTL_SYNC, &sync);
        ::munmap(m, size);
      } else {
        IHS_TRACE("[pv-trace] GetDmabuf NV12 mmap failed id={}", id_);
      }
      pv_dmabuf_color_space_ = 2;  // IHS_COLOR_SPACE_BT709
      pv_dmabuf_color_range_ = 2;  // IHS_COLOR_RANGE_LIMITED
    }

    pv_dmabuf_fourcc_ = gbm_bo_get_format(bo);
    pv_dmabuf_modifier_ = gbm_bo_get_modifier(bo);
    pv_dmabuf_w_ = w;
    pv_dmabuf_h_ = h;
    IHS_TRACE(
        "[pv-trace] GetDmabuf allocated {} scanout buffer id={} {}x{} fd={} "
        "fourcc=0x{:08x} mod=0x{:016x} planes={} cs={}",
        is_nv12 ? "NV12" : "XRGB", id_, w, h, pv_dmabuf_fd_, pv_dmabuf_fourcc_,
        pv_dmabuf_modifier_, pv_dmabuf_planes_, pv_dmabuf_color_space_);
  }

  if (pv_dmabuf_fd_ < 0) {
    return false;
  }
  for (uint32_t i = 0; i < pv_dmabuf_planes_; ++i) {
    out->fd[i] = pv_dmabuf_fd_;  // one bo; planes differ by offset/stride
    out->offset[i] = pv_dmabuf_offset_[i];
    out->stride[i] = pv_dmabuf_stride_[i];
  }
  out->fourcc = pv_dmabuf_fourcc_;
  out->modifier = pv_dmabuf_modifier_;
  out->width = static_cast<uint32_t>(pv_dmabuf_w_);
  out->height = static_cast<uint32_t>(pv_dmabuf_h_);
  out->plane_count = pv_dmabuf_planes_;
  out->color_space = pv_dmabuf_color_space_;  // 0 (DEFAULT) for RGB
  out->color_range = pv_dmabuf_color_range_;
  out->acquire_fence_fd = -1;  // CPU-filled, already visible
  return true;
}

void LayerPlaygroundViewPlugin::OnResize(int32_t w, int32_t h) {
  pending_width_ = w;
  pending_height_ = h;
}

#endif  // BUILD_COMPOSITOR

}  // namespace plugin_layer_playground_view
