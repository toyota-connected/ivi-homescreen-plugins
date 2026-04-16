/*
 * Copyright 2020-2024 Toyota Connected North America
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

#include "video_player.h"

#include "dmabuf_frame.h"

#include <flutter/event_channel.h>
#include <flutter/event_stream_handler.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/plugin_registrar_homescreen.h>
#include <flutter/standard_method_codec.h>

#include <atomic>
#include <chrono>
#include <climits>
#include <cstring>
#include <optional>

#include <GLES2/gl2ext.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/app/gstappsink.h>
#include <gst/audio/audio.h>
#include <gst/tag/tag.h>

#include <backend/backend.h>
#include <flutter_homescreen.h>
#include <plugins/common/common.h>
#include <utility>

#define GSTREAMER_DEBUG 0

namespace video_player_linux {

// Serialize access to the shared EGL texture context across all players.
// EGL contexts can only be current on one thread at a time.
static std::mutex g_texture_context_mutex;

typedef enum {
  GST_PLAY_FLAG_AUDIO = 1 << 0,
  GST_PLAY_FLAG_VIDEO = 1 << 1,
  GST_PLAY_FLAG_TEXT = 1 << 2
} GstPlayFlags;

// Audio-only synthetic player IDs start at 0x7F000000 to avoid colliding
// with GL texture IDs (which are small positive integers).
static std::atomic<int64_t> g_audio_player_id_counter{0x7F000000};

void VideoPlayer::CreateSharedGlContext() {
  FlutterDesktopEglContext engine_ctx{};
  if (!FlutterDesktopPluginRegistrarGetEglContext(m_raw_registrar,
                                                  &engine_ctx)) {
    SPDLOG_DEBUG(
        "[VideoPlayer] Embedder refused EGL share handles (non-EGL backend?); "
        "falling back to legacy context path");
    return;
  }
  auto display = static_cast<EGLDisplay>(engine_ctx.display);
  auto config = static_cast<EGLConfig>(engine_ctx.config);
  auto share = static_cast<EGLContext>(engine_ctx.share_context);
  if (display == EGL_NO_DISPLAY || share == EGL_NO_CONTEXT) {
    spdlog::warn("[VideoPlayer] Embedder returned incomplete EGL handles");
    return;
  }

  // Request ES 3.0 — matches the GLES3 headers we build against and what
  // the shader program needs.
  const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  EGLContext ctx = eglCreateContext(display, config, share, ctx_attrs);
  if (ctx == EGL_NO_CONTEXT) {
    spdlog::warn(
        "[VideoPlayer] eglCreateContext failed (0x{:X}); using legacy path",
        eglGetError());
    return;
  }

  // 1x1 pbuffer because some drivers refuse eglMakeCurrent with
  // EGL_NO_SURFACE even when EGL_KHR_surfaceless_context is present. We
  // never draw into this surface — all output is to FBOs.
  const EGLint pbuf_attrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  EGLSurface surf = eglCreatePbufferSurface(display, config, pbuf_attrs);
  if (surf == EGL_NO_SURFACE) {
    spdlog::warn(
        "[VideoPlayer] eglCreatePbufferSurface failed (0x{:X}); using legacy "
        "path",
        eglGetError());
    eglDestroyContext(display, ctx);
    return;
  }

  egl_display_ = display;
  egl_context_ = ctx;
  egl_surface_ = surf;
  use_legacy_context_ = false;
  SPDLOG_DEBUG("[VideoPlayer] Shared EGL context created (share=0x{:x})",
               reinterpret_cast<uintptr_t>(share));
}

void VideoPlayer::DestroySharedGlContext() {
  if (egl_display_ == EGL_NO_DISPLAY) {
    return;
  }
  if (egl_surface_ != EGL_NO_SURFACE) {
    eglDestroySurface(egl_display_, egl_surface_);
    egl_surface_ = EGL_NO_SURFACE;
  }
  if (egl_context_ != EGL_NO_CONTEXT) {
    eglDestroyContext(egl_display_, egl_context_);
    egl_context_ = EGL_NO_CONTEXT;
  }
  egl_display_ = EGL_NO_DISPLAY;
  use_legacy_context_ = true;
}

void VideoPlayer::MakeContextCurrent() {
  // handoff_handler releases our context at the end of each frame so the
  // next frame — potentially on a different streaming thread — can bind
  // cleanly without EGL_BAD_ACCESS. The early-return when already current
  // still helps the ctor/Dispose paths that make multiple GL calls in a
  // row on the same thread.
  if (eglGetCurrentContext() == egl_context_) {
    return;
  }
  if (!eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_)) {
    spdlog::error("[VideoPlayer] eglMakeCurrent failed: 0x{:X}", eglGetError());
  }
}

namespace {
bool has_egl_extension(EGLDisplay dpy, const char* name) {
  if (dpy == EGL_NO_DISPLAY || !name) {
    return false;
  }
  const char* exts = eglQueryString(dpy, EGL_EXTENSIONS);
  if (!exts) {
    return false;
  }
  // Extensions are space-separated; match whole tokens so "EGL_KHR_image"
  // doesn't false-match "EGL_KHR_image_base".
  const size_t len = std::strlen(name);
  const char* p = exts;
  while ((p = std::strstr(p, name)) != nullptr) {
    const bool left_ok = (p == exts) || p[-1] == ' ';
    const bool right_ok = p[len] == ' ' || p[len] == '\0';
    if (left_ok && right_ok) {
      return true;
    }
    p += len;
  }
  return false;
}

// Map GstVideoFormat to a DRM FourCC. Only formats we can plausibly
// import via EGL_LINUX_DMA_BUF_EXT are listed; anything else forces
// the fallback to the CPU NV12 upload path.
guint32 GstVideoFormatToDrmFourcc(GstVideoFormat fmt) {
  switch (fmt) {
    case GST_VIDEO_FORMAT_NV12:
      return Fourcc('N', 'V', '1', '2');
    case GST_VIDEO_FORMAT_NV21:
      return Fourcc('N', 'V', '2', '1');
    case GST_VIDEO_FORMAT_I420:
      return Fourcc('Y', 'U', '1', '2');
    case GST_VIDEO_FORMAT_YV12:
      return Fourcc('Y', 'V', '1', '2');
    case GST_VIDEO_FORMAT_BGRA:
      return Fourcc('B', 'G', 'R', 'A');
    case GST_VIDEO_FORMAT_BGRx:
      return Fourcc('B', 'G', 'R', 'X');
    default:
      return 0;
  }
}

// Build a DmabufFrame from a GstSample. Returns nullopt when the
// sample isn't backed by dmabuf, when its caps describe a format we
// don't know how to map to DRM FourCC, or when its memory layout
// doesn't make sense (too many planes, non-dmabuf memory mixed in).
// The returned frame borrows FDs from the sample's GstMemory — the
// caller must keep the sample alive until the FDs are consumed
// (EGLImage creation dup()s them in Phase 1.3).
std::optional<DmabufFrame> ExtractDmabufFrame(GstSample* sample) {
  if (!sample) {
    return std::nullopt;
  }
  GstBuffer* buf = gst_sample_get_buffer(sample);
  GstCaps* caps = gst_sample_get_caps(sample);
  if (!buf || !caps) {
    return std::nullopt;
  }

  GstVideoInfo vi;
  gst_video_info_init(&vi);
  if (!gst_video_info_from_caps(&vi, caps)) {
    return std::nullopt;
  }

  DmabufFrame frame;
  frame.width = GST_VIDEO_INFO_WIDTH(&vi);
  frame.height = GST_VIDEO_INFO_HEIGHT(&vi);
  frame.n_planes = GST_VIDEO_INFO_N_PLANES(&vi);
  if (frame.n_planes == 0 || frame.n_planes > frame.planes.size()) {
    return std::nullopt;
  }
  frame.drm_fourcc = GstVideoFormatToDrmFourcc(GST_VIDEO_INFO_FORMAT(&vi));
  if (frame.drm_fourcc == 0) {
    return std::nullopt;
  }

  // Modifier extraction: GStreamer 1.24 encodes modifiers in a
  // `drm-format` caps field as "NV12:0x200000000b". We don't hard-
  // require 1.24 yet, so absence of the field just means linear.
  // Tiled/compressed formats tracked by Phase 3 will plumb the real
  // modifier through here.
  frame.drm_modifier = kDrmFormatModLinear;
  const GstStructure* s = gst_caps_get_structure(caps, 0);
  if (s) {
    const gchar* drm_format = gst_structure_get_string(s, "drm-format");
    if (drm_format) {
      const gchar* colon = std::strchr(drm_format, ':');
      if (colon) {
        const guint64 mod = g_ascii_strtoull(colon + 1, nullptr, 0);
        if (mod != 0) {
          frame.drm_modifier = mod;
        }
      }
    }
  }

  const guint n_mem = gst_buffer_n_memory(buf);
  if (n_mem == 0) {
    return std::nullopt;
  }
  const bool single_fd_layout = (n_mem == 1 && frame.n_planes > 1);

  for (unsigned i = 0; i < frame.n_planes; ++i) {
    GstMemory* mem = nullptr;
    gsize plane_offset_in_mem = 0;
    if (single_fd_layout) {
      // All planes packed into one GstMemory; per-plane offsets come
      // from GstVideoInfo.
      mem = gst_buffer_peek_memory(buf, 0);
      plane_offset_in_mem = GST_VIDEO_INFO_PLANE_OFFSET(&vi, i);
    } else if (i < n_mem) {
      // Each plane has its own GstMemory / FD. plane_offset_in_mem
      // stays 0 because the plane starts at the beginning of its
      // own memory block.
      mem = gst_buffer_peek_memory(buf, i);
    } else {
      return std::nullopt;  // fewer mem chunks than declared planes
    }
    if (!mem || !gst_is_dmabuf_memory(mem)) {
      return std::nullopt;
    }
    frame.planes[i].fd = gst_dmabuf_memory_get_fd(mem);
    // GstMemory's offset is the offset of this memory block into its
    // underlying FD; the plane's offset within the block adds on top.
    frame.planes[i].offset =
        static_cast<guint32>(mem->offset + plane_offset_in_mem);
    frame.planes[i].stride =
        static_cast<guint32>(GST_VIDEO_INFO_PLANE_STRIDE(&vi, i));
  }

  return frame;
}
}  // namespace

VideoPlayer::RenderPath VideoPlayer::ProbeRenderPath() const {
  // Env-var kill switch. Useful for A/B'ing in the field and for
  // bypassing the zero-copy path on drivers where EGLImage import from
  // dmabuf is advertised but broken.
  if (std::getenv("VIDEO_PLAYER_DISABLE_DMABUF")) {
    SPDLOG_DEBUG("[VideoPlayer] Dmabuf path disabled via env var");
    return RenderPath::PboShaderUpload;
  }

  // The dmabuf path relies on our per-player shared EGL context owning the
  // EGLImage → GL_TEXTURE_2D binding. On the legacy TextureMakeCurrent
  // fallback we don't have a context of our own to bind the image into.
  if (use_legacy_context_) {
    SPDLOG_DEBUG(
        "[VideoPlayer] Dmabuf path unavailable: legacy context in use");
    return RenderPath::PboShaderUpload;
  }

  // Core EGL extension for dmabuf import. Modifier support
  // (EGL_EXT_image_dma_buf_import_modifiers) is checked where relevant
  // in the tiled-format code paths; absence only disables tiled support,
  // not linear NV12.
  if (!has_egl_extension(egl_display_, "EGL_EXT_image_dma_buf_import")) {
    SPDLOG_DEBUG(
        "[VideoPlayer] Dmabuf path unavailable: EGL_EXT_image_dma_buf_import "
        "missing");
    return RenderPath::PboShaderUpload;
  }

  // GL_OES_EGL_image (for glEGLImageTargetTexture2DOES) is checked at
  // bind time in Phase 1.3 once a context is current; probing it here
  // would require making our context current just for the query, which
  // we haven't done yet at ctor time. The runtime-downgrade path handles
  // the "advertised but unusable" case.

  return RenderPath::DmabufZeroCopy;
}

namespace {
// Entrypoints for the zero-copy path. Resolved lazily via
// eglGetProcAddress the first time a dmabuf frame needs to land; the
// static function pointers are thread-safe under the typical
// initialize-once-from-one-thread pattern we actually hit (the
// streaming thread is the sole caller).
using EglCreateImageKHRFn = EGLImageKHR (*)(EGLDisplay,
                                            EGLContext,
                                            EGLenum,
                                            EGLClientBuffer,
                                            const EGLint*);
using EglDestroyImageKHRFn = EGLBoolean (*)(EGLDisplay, EGLImageKHR);
using GlEGLImageTargetTexture2DOESFn = void (*)(GLenum target,
                                                GLeglImageOES image);

EglCreateImageKHRFn g_eglCreateImageKHR = nullptr;
EglDestroyImageKHRFn g_eglDestroyImageKHR = nullptr;
GlEGLImageTargetTexture2DOESFn g_glEGLImageTargetTexture2DOES = nullptr;

bool ResolveEglImageEntrypoints() {
  if (g_eglCreateImageKHR && g_eglDestroyImageKHR &&
      g_glEGLImageTargetTexture2DOES) {
    return true;
  }
  g_eglCreateImageKHR = reinterpret_cast<EglCreateImageKHRFn>(
      eglGetProcAddress("eglCreateImageKHR"));
  g_eglDestroyImageKHR = reinterpret_cast<EglDestroyImageKHRFn>(
      eglGetProcAddress("eglDestroyImageKHR"));
  g_glEGLImageTargetTexture2DOES =
      reinterpret_cast<GlEGLImageTargetTexture2DOESFn>(
          eglGetProcAddress("glEGLImageTargetTexture2DOES"));
  if (!g_eglCreateImageKHR || !g_eglDestroyImageKHR ||
      !g_glEGLImageTargetTexture2DOES) {
    spdlog::warn(
        "[VideoPlayer] Missing EGL/GL entrypoints for dmabuf import: "
        "eglCreateImageKHR={} eglDestroyImageKHR={} "
        "glEGLImageTargetTexture2DOES={}",
        g_eglCreateImageKHR ? "ok" : "missing",
        g_eglDestroyImageKHR ? "ok" : "missing",
        g_glEGLImageTargetTexture2DOES ? "ok" : "missing");
    return false;
  }
  return true;
}

// Map a GStreamer colorimetry string (e.g. "bt709:16-235",
// "bt2020-10:16-235") to the EGL_*_HINT_EXT attribute pair. Returns
// true when both hints were populated; false when the string is
// unknown and callers should skip the hint attrs entirely.
bool ColorspaceHintsForCaps(const std::string& caps,
                            EGLint* out_matrix,
                            EGLint* out_range) {
  if (caps.empty() || caps == "unknown") {
    return false;
  }
  if (caps.find("bt2020") != std::string::npos) {
    *out_matrix = EGL_ITU_REC2020_EXT;
  } else if (caps.find("bt601") != std::string::npos) {
    *out_matrix = EGL_ITU_REC601_EXT;
  } else {
    // Default everything else (bt709, unknown-modern) to 709.
    *out_matrix = EGL_ITU_REC709_EXT;
  }
  if (caps.find("0-255") != std::string::npos ||
      caps.find(":full") != std::string::npos) {
    *out_range = EGL_YUV_FULL_RANGE_EXT;
  } else {
    *out_range = EGL_YUV_NARROW_RANGE_EXT;
  }
  return true;
}
}  // namespace

bool VideoPlayer::ImportDmabufFrame(const DmabufFrame& frame,
                                    GstSample* sample) {
  if (!ResolveEglImageEntrypoints()) {
    return false;
  }
  if (egl_display_ == EGL_NO_DISPLAY || !shader_ || !sample) {
    return false;
  }

  // Build the attribute list. Reserve enough room for the worst case —
  // 4 planes with modifier attrs plus YUV hints + NONE terminator.
  constexpr size_t kMaxAttrs = 4 + 2         // width + height
                               + 2           // fourcc
                               + 4 * 3 * 2   // fd/off/pitch * 4 plane
                               + 4 * 2 * 2   // modifier lo/hi * 4 plane
                               + 2 * 2 + 1;  // colour + range + NONE
  EGLint attrs[kMaxAttrs];
  size_t i = 0;

  attrs[i++] = EGL_WIDTH;
  attrs[i++] = frame.width;
  attrs[i++] = EGL_HEIGHT;
  attrs[i++] = frame.height;
  attrs[i++] = EGL_LINUX_DRM_FOURCC_EXT;
  attrs[i++] = static_cast<EGLint>(frame.drm_fourcc);

  // Per-plane FD/offset/pitch. EGL defines the PLANE0/1/2/3 attribute
  // triplets with adjacent values (0x3272..0x327a), so we can derive
  // each plane's attribute constants from PLANE0 + plane_index * 3.
  const EGLint kPlane0Attrs[] = {
      EGL_DMA_BUF_PLANE0_FD_EXT,     EGL_DMA_BUF_PLANE0_OFFSET_EXT,
      EGL_DMA_BUF_PLANE0_PITCH_EXT,  EGL_DMA_BUF_PLANE1_FD_EXT,
      EGL_DMA_BUF_PLANE1_OFFSET_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT,
  };
  for (unsigned p = 0; p < frame.n_planes; ++p) {
    attrs[i++] = kPlane0Attrs[p * 3 + 0];
    attrs[i++] = frame.planes[p].fd;
    attrs[i++] = kPlane0Attrs[p * 3 + 1];
    attrs[i++] = static_cast<EGLint>(frame.planes[p].offset);
    attrs[i++] = kPlane0Attrs[p * 3 + 2];
    attrs[i++] = static_cast<EGLint>(frame.planes[p].stride);
  }

  // Non-linear modifier requires the modifier extension. If the
  // driver doesn't advertise it, we can still import LINEAR buffers
  // by omitting the modifier attrs entirely.
  const bool has_modifier = frame.drm_modifier != kDrmFormatModLinear &&
                            frame.drm_modifier != kDrmFormatModInvalid;
  if (has_modifier) {
    if (!egl_dmabuf_modifiers_ok_) {
      spdlog::warn(
          "[VideoPlayer] dmabuf frame has non-linear modifier 0x{:x} but "
          "EGL_EXT_image_dma_buf_import_modifiers is not available; import "
          "will fail",
          frame.drm_modifier);
      return false;
    }
    const EGLint kMod0Attrs[] = {
        EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
        EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
        EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
        EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT,
    };
    for (unsigned p = 0; p < frame.n_planes; ++p) {
      attrs[i++] = kMod0Attrs[p * 2 + 0];
      attrs[i++] = static_cast<EGLint>(frame.drm_modifier & 0xFFFFFFFFu);
      attrs[i++] = kMod0Attrs[p * 2 + 1];
      attrs[i++] =
          static_cast<EGLint>((frame.drm_modifier >> 32) & 0xFFFFFFFFu);
    }
  }

  // YUV color-space + range hints let the driver perform correct
  // YUV→RGB on sample. Derived from the GstVideoColorimetry we stored
  // at prepare() time.
  std::string cs;
  {
    std::lock_guard meta_lock(stats_.meta_mutex);
    cs = stats_.negotiated_colorspace;
  }
  EGLint matrix_hint = EGL_ITU_REC709_EXT;
  EGLint range_hint = EGL_YUV_NARROW_RANGE_EXT;
  if (ColorspaceHintsForCaps(cs, &matrix_hint, &range_hint)) {
    attrs[i++] = EGL_YUV_COLOR_SPACE_HINT_EXT;
    attrs[i++] = matrix_hint;
    attrs[i++] = EGL_SAMPLE_RANGE_HINT_EXT;
    attrs[i++] = range_hint;
  }
  attrs[i++] = EGL_NONE;

  EGLImageKHR new_image =
      g_eglCreateImageKHR(egl_display_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                          /*clientbuffer=*/nullptr, attrs);
  if (new_image == EGL_NO_IMAGE_KHR) {
    spdlog::error("[VideoPlayer] eglCreateImageKHR failed: 0x{:x}",
                  eglGetError());
    return false;
  }

  // Re-specify shader_->textureId's storage as the new EGL image.
  // shader_->textureId keeps the same GL name Flutter samples; the
  // underlying dmabuf replaces the RGBA FBO attachment the Shader
  // class set up.
  glBindTexture(GL_TEXTURE_2D, shader_->textureId);
  g_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, new_image);
  const GLenum gl_err = glGetError();
  if (gl_err != GL_NO_ERROR) {
    spdlog::error(
        "[VideoPlayer] glEGLImageTargetTexture2DOES failed: GL error 0x{:x}",
        gl_err);
    // Bail before taking ownership of the sample — caller still
    // owns its reference and can fall back / unref.
    g_eglDestroyImageKHR(egl_display_, new_image);
    return false;
  }

  // Bind succeeded. Push the new frame onto the in-flight deque,
  // taking ownership of the sample ref. Evict the oldest entry if
  // we're over the cap — that frame is no longer what the compositor
  // is sampling (the just-bound image is) so it's safe to reclaim.
  InFlightFrame popped{};
  {
    std::lock_guard<std::mutex> lock(in_flight_mutex_);
    in_flight_frames_.push_back(
        InFlightFrame{new_image, gst_sample_ref(sample)});
    if (in_flight_frames_.size() > kMaxInFlight) {
      popped = in_flight_frames_.front();
      in_flight_frames_.pop_front();
    }
  }
  if (popped.image != EGL_NO_IMAGE_KHR) {
    g_eglDestroyImageKHR(egl_display_, popped.image);
  }
  if (popped.sample) {
    gst_sample_unref(popped.sample);
  }
  return true;
}

void VideoPlayer::DrainInFlightFrames() {
  std::deque<InFlightFrame> drained;
  {
    std::lock_guard<std::mutex> lock(in_flight_mutex_);
    drained.swap(in_flight_frames_);
  }
  // Drop EGL images under the player's context (caller's contract).
  // Samples can be unref'd from any thread — gst_sample_unref is
  // thread-safe — so we do both in the same loop for simplicity.
  for (auto& frame : drained) {
    if (frame.image != EGL_NO_IMAGE_KHR && g_eglDestroyImageKHR &&
        egl_display_ != EGL_NO_DISPLAY) {
      g_eglDestroyImageKHR(egl_display_, frame.image);
    }
    if (frame.sample) {
      gst_sample_unref(frame.sample);
    }
  }
}

VideoPlayer::VideoPlayer(flutter::PluginRegistrarDesktop* registrar,
                         FlutterDesktopPluginRegistrarRef raw_registrar,
                         std::string uri,
                         std::map<std::string, std::string> http_headers,
                         const MediaInfo& info)
    : m_registrar(registrar),
      m_raw_registrar(raw_registrar),
      uri_(std::move(uri)),
      http_headers_(std::move(http_headers)),
      width_(info.width),
      height_(info.height),
      duration_(info.duration),
      has_video_(info.has_video),
      initial_album_art_(info.album_art),
      initial_album_art_mime_(info.album_art_mime),
      title_(info.title),
      artist_(info.artist),
      album_(info.album),
      album_artist_(info.album_artist),
      genre_(info.genre),
      track_number_(info.track_number),
      audio_codec_(info.audio_codec),
      audio_channels_(info.audio_channels),
      audio_sample_rate_(info.audio_sample_rate) {
  SPDLOG_DEBUG(
      "[VideoPlayer] uri: {}, http_headers: {}, size: {} x {}, duration: {}, "
      "has_video: {}",
      uri_.c_str(), http_headers_.size(), width_, height_, duration_,
      has_video_);

  gst_video_info_init(&info_);

  // Validate dimensions only when video is expected.
  if (has_video_ && (width_ <= 0 || height_ <= 0)) {
    SPDLOG_ERROR("[VideoPlayer] Invalid dimensions: {}x{}", width_, height_);
    m_valid = false;
    return;
  }

  std::lock_guard buffer_lock(buffer_mutex_);

  if (has_video_) {
    // Double-buffer can be enabled via env var for flicker-sensitive cases.
    const bool double_buf =
        std::getenv("VIDEO_PLAYER_DOUBLE_BUFFER") != nullptr;

    // Prefer a dedicated shared EGL context so this player's GL work can
    // run in parallel with other players and without the per-frame
    // context-switch cost. Non-EGL backends (Vulkan, headless) fail the
    // embedder call — fall back to the legacy global-mutex path there.
    CreateSharedGlContext();

    if (!use_legacy_context_) {
      MakeContextCurrent();
      shader_ = std::make_unique<nv12::Shader>(width_, height_, double_buf);
      m_texture_id = shader_->textureId;
      // Bind the VAO for the rest of this context's life — no other code
      // touches this context, so the VAO state is stable and we can skip
      // the defensive per-frame rebind in handoff_handler.
      glBindVertexArray(shader_->vertex_arr_id_);
      eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                     EGL_NO_CONTEXT);
    } else {
      std::lock_guard ctx_lock(g_texture_context_mutex);
      m_registrar->texture_registrar()->TextureMakeCurrent();
      shader_ = std::make_unique<nv12::Shader>(width_, height_, double_buf);
      m_texture_id = shader_->textureId;
      m_registrar->texture_registrar()->TextureClearCurrent();
    }
    stats_.uses_shared_gl_context.store(!use_legacy_context_,
                                        std::memory_order_relaxed);

    // Decide which render path this player will use for the rest of its
    // lifetime. PboShaderUpload is the existing path; DmabufZeroCopy
    // swaps fakesink for appsink with dmabuf caps (Phase 1.1+).
    render_path_ = ProbeRenderPath();
    stats_.uses_dmabuf.store(render_path_ == RenderPath::DmabufZeroCopy,
                             std::memory_order_relaxed);
    SPDLOG_DEBUG("[VideoPlayer] render path: {}",
                 render_path_ == RenderPath::DmabufZeroCopy ? "dmabuf-zerocopy"
                                                            : "pbo-shader");
    // Cache the modifier-extension state now so the hot path in
    // ImportDmabufFrame doesn't need to query eglQueryString per frame.
    egl_dmabuf_modifiers_ok_ = has_egl_extension(
        egl_display_, "EGL_EXT_image_dma_buf_import_modifiers");

    /// Setup GL Texture 2D
    m_descriptor.struct_size = sizeof(FlutterDesktopGpuSurfaceDescriptor);
    m_descriptor.handle = &m_texture_id;
    m_descriptor.width = static_cast<size_t>(width_);
    m_descriptor.height = static_cast<size_t>(height_);
    m_descriptor.visible_width = static_cast<size_t>(width_);
    m_descriptor.visible_height = static_cast<size_t>(height_);
    m_descriptor.format = kFlutterDesktopPixelFormatRGBA8888;
    m_descriptor.release_callback = [](void* /* release_context */) {};
    m_descriptor.release_context = this;

    gpu_surface_texture_ = std::make_unique<flutter::GpuSurfaceTexture>(
        kFlutterDesktopGpuSurfaceTypeGlTexture2D,
        [&](size_t /* width */,
            size_t /* height */) -> const FlutterDesktopGpuSurfaceDescriptor* {
          return &m_descriptor;
        });

    flutter::TextureVariant texture = *gpu_surface_texture_;
    m_registrar->texture_registrar()->RegisterTexture(&texture);
    SPDLOG_DEBUG("[VideoPlayer] Registered texture_id={}", m_texture_id);
  } else {
    // Audio-only: synthesise a player ID outside the GL texture ID range.
    m_texture_id = g_audio_player_id_counter.fetch_add(1);
    SPDLOG_DEBUG("[VideoPlayer] Audio-only player_id={}", m_texture_id);
  }

  /// Setup GST Pipeline

  context_ = g_main_context_get_thread_default();

  playbin_ = gst_element_factory_make("playbin", nullptr);
  if (!playbin_) {
    SPDLOG_ERROR("[VideoPlayer] Failed to create playbin element");
    m_valid = false;
    return;
  }
  g_object_set(playbin_, "uri", uri_.c_str(), nullptr);

  if (!http_headers_.empty()) {
    GstStructure* extraHeaders = gst_structure_new_empty("extra-headers");
    for (const auto& [key, value] : http_headers_) {
      gst_structure_set(extraHeaders, key.c_str(), G_TYPE_STRING, value.c_str(),
                        nullptr);
      SPDLOG_DEBUG("extra-header: {}:{}", key, value);
    }
    g_object_set(playbin_, "extra-headers", extraHeaders, nullptr);
    gst_structure_free(extraHeaders);
  }

  // Connect source-setup so we can configure souphttpsrc / rtspsrc as soon
  // as playbin instantiates the source element. The handler is safe across
  // source types because it probes for properties before setting them.
  source_setup_id_ =
      g_signal_connect(playbin_, "source-setup",
                       reinterpret_cast<GCallback>(OnSourceSetup), this);

  // deep-element-added fires for every element auto-plugged into playbin's
  // nested uridecodebin. Used to discover which decoder was actually
  // selected so the stats channel can surface it. Harmless if nobody
  // subscribes to stats.
  deep_element_added_id_ =
      g_signal_connect(playbin_, "deep-element-added",
                       reinterpret_cast<GCallback>(OnDeepElementAdded), this);

  // Custom audio sink bin: audioconvert → audioresample → capsfilter → sink.
  // Even without app-level controls this improves codec/sample-rate
  // compatibility and is a prerequisite for channel/EQ work.
  if (GstElement* audio_bin = BuildAudioSinkBin()) {
    audio_bin_ = audio_bin;
    g_object_set(playbin_, "audio-sink", audio_bin_, nullptr);
    audio_upgraded_ = true;
  } else {
    spdlog::warn("[VideoPlayer] Audio sink bin build failed; using fakesink");
    GstElement* fake = gst_element_factory_make("fakesink", "fake-audio");
    if (fake) {
      g_object_set(fake, "sync", TRUE, nullptr);
      g_object_set(playbin_, "audio-sink", fake, nullptr);
    }
  }

  gint flags = 0;
  g_object_get(playbin_, "flags", &flags, nullptr);
  // Always enable both AUDIO and VIDEO flags. For audio-only sources the
  // VIDEO flag is harmless (uridecodebin won't find a video pad), but
  // clearing it has been observed to leave playsink in a state where it
  // also refuses the audio pad ("1 pending" forever, then GST_FLOW_NOT_LINKED
  // on the source). Provide a fake video-sink below so playbin never tries
  // to instantiate autovideosink either.
  flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO;
  flags &= ~GST_PLAY_FLAG_TEXT;
  g_object_set(playbin_, "flags", flags, nullptr);

  // Audio-only: install a bare fakesink as video-sink so playbin never
  // instantiates its autovideosink default. Without this, playbin's
  // playsink will refuse to link the audio pad on headless / embedded
  // contexts where autovideosink can't grab a real display surface.
  if (!has_video_) {
    GstElement* fake_video = gst_element_factory_make("fakesink", "fake-video");
    if (fake_video) {
      g_object_set(fake_video, "sync", TRUE, "async", FALSE, nullptr);
      g_object_set(playbin_, "video-sink", fake_video, nullptr);
    }
  }

  int connection_speed = 10000;
  if (const char* env = std::getenv("VIDEO_PLAYER_CONNECTION_SPEED")) {
    char* end = nullptr;
    const long val = std::strtol(env, &end, 10);
    if (end != env && val > 0 && val <= INT_MAX) {
      connection_speed = static_cast<int>(val);
    }
  }
  g_object_set(playbin_, "connection-speed", connection_speed, nullptr);
  g_object_set(playbin_, "volume", volume_, nullptr);

  // Buffer tuning (Phase 2). Defaults are GStreamer's; only override on
  // explicit env vars to keep behaviour stable for unconstrained targets.
  if (const char* env = std::getenv("VIDEO_PLAYER_BUFFER_SIZE")) {
    char* end = nullptr;
    const long val = std::strtol(env, &end, 10);
    if (end != env && val > 0) {
      g_object_set(playbin_, "buffer-size", static_cast<int>(val), nullptr);
    }
  }
  if (const char* env = std::getenv("VIDEO_PLAYER_BUFFER_DURATION")) {
    char* end = nullptr;
    const long long val = std::strtoll(env, &end, 10);
    if (end != env && val > 0) {
      g_object_set(playbin_, "buffer-duration",
                   static_cast<gint64>(val) * GST_SECOND, nullptr);
    }
  }

  if (has_video_) {
    if (render_path_ == RenderPath::DmabufZeroCopy) {
      // Phase 1.1 — appsink replaces fakesink on the zero-copy path.
      // Caps accept dmabuf *or* raw NV12 so non-dmabuf-capable decoders
      // (openh264dec, avdec_*) still negotiate. The OnNewSample handler
      // routes dmabuf buffers through the EGLImage path (Phase 1.2/1.3)
      // and raw buffers through the existing NV12 shader upload.
      sink_ = gst_element_factory_make("appsink", nullptr);
      if (!sink_) {
        SPDLOG_ERROR("[VideoPlayer] Failed to create appsink element");
        m_valid = false;
        return;
      }
      // `video/x-raw(memory:DMABuf)` first so upstream picks it when
      // possible; fall back to plain raw NV12 otherwise. Accept the
      // same NV12 format on both features so the downstream shader
      // preset stays valid.
      GstCaps* sink_caps = gst_caps_from_string(
          "video/x-raw(memory:DMABuf), format=(string)NV12; "
          "video/x-raw, format=(string)NV12");
      g_object_set(sink_, "caps", sink_caps, "emit-signals", TRUE,
                   "max-buffers", 2, "drop", TRUE, "sync", TRUE, nullptr);
      gst_caps_unref(sink_caps);
      handoff_handler_id_ = g_signal_connect(
          sink_, "new-sample", reinterpret_cast<GCallback>(OnNewSample), this);
    } else {
      sink_ = gst_element_factory_make("fakesink", nullptr);
      if (!sink_) {
        SPDLOG_ERROR("[VideoPlayer] Failed to create fakesink element");
        m_valid = false;
        return;
      }
      g_object_set(sink_, "sync", TRUE, nullptr);
      g_object_set(sink_, "signal-handoffs", TRUE, nullptr);
      // Explicit push mode. Pull mode changes upstream delivery and was
      // observed to stop buffer flow to the sink after ~5 frames; see
      // Phase 0.x follow-up.
      g_object_set(sink_, "can-activate-pull", FALSE, nullptr);
      handoff_handler_id_ = g_signal_connect(
          sink_, "handoff", reinterpret_cast<GCallback>(handoff_handler), this);
    }

    video_convert_ = gst_element_factory_make("videoconvert", nullptr);
    if (!video_convert_) {
      SPDLOG_ERROR("[VideoPlayer] Failed to create videoconvert element");
      m_valid = false;
      return;
    }

    GstCaps* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
                                        "NV12", nullptr);

    video_scale_ = gst_element_factory_make("videoscale", nullptr);
    if (!video_scale_) {
      SPDLOG_ERROR("[VideoPlayer] Failed to create videoscale element");
      m_valid = false;
      return;
    }
    // Phase 2: scaling algorithm via env var. 1=bilinear (default).
    int scale_method = 1;
    if (const char* env = std::getenv("VIDEO_PLAYER_SCALE_METHOD")) {
      char* end = nullptr;
      const long v = std::strtol(env, &end, 10);
      if (end != env && v >= 0 && v <= 9)
        scale_method = static_cast<int>(v);
    }
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(video_scale_),
                                     "method")) {
      g_object_set(video_scale_, "method", scale_method, nullptr);
    }

    GstCaps* scale =
        gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, width_,
                            "height", G_TYPE_INT, height_, nullptr);

    pipeline_ = gst_bin_new(nullptr);

    gst_bin_add_many(GST_BIN(pipeline_), video_convert_, video_scale_, sink_,
                     nullptr);

    if (!gst_element_link_filtered(video_convert_, video_scale_, caps)) {
      SPDLOG_ERROR("[VideoPlayer] Failed to link videoconvert with videoscale");
    }
    gst_caps_unref(caps);

    if (!gst_element_link_filtered(video_scale_, sink_, scale)) {
      SPDLOG_ERROR("[VideoPlayer] Failed to link videoscale with fakesink");
    }
    gst_caps_unref(scale);

    // Ghost pad so playbin can link its decoded output into this bin
    GstPad* pad = gst_element_get_static_pad(video_convert_, "sink");
    GstPad* ghost_pad = gst_ghost_pad_new("sink", pad);
    gst_pad_set_active(ghost_pad, TRUE);
    gst_element_add_pad(pipeline_, ghost_pad);
    gst_object_unref(pad);

    g_object_set(playbin_, "video-sink", pipeline_, nullptr);
  }

  bus_ = gst_element_get_bus(playbin_);
  GSource* bus_source = gst_bus_create_watch(bus_);
  // GstBus source dispatch calls the callback with (GstBus*, GstMessage*,
  // gpointer) arguments matching gst_bus_async_signal_func's signature.
  // Cast through void* to avoid -Wcast-function-type-mismatch.
  g_source_set_callback(bus_source,
                        reinterpret_cast<GSourceFunc>(
                            reinterpret_cast<void*>(gst_bus_async_signal_func)),
                        nullptr, nullptr);
  g_source_attach(bus_source, context_);
  g_source_unref(bus_source);
  on_bus_msg_id_ = g_signal_connect(
      bus_, "message", reinterpret_cast<GCallback>(OnBusMessage), this);
}

VideoPlayer::~VideoPlayer() {
  if (m_valid) {
    Dispose();
  }
}

void VideoPlayer::Dispose() {
  SPDLOG_DEBUG("[VideoPlayer] Dispose");
  m_valid = false;
  is_initialized_ = false;
  StopAudioMonitor();

  std::lock_guard buffer_lock(buffer_mutex_);

  if (bus_ && on_bus_msg_id_) {
    g_signal_handler_disconnect(G_OBJECT(bus_), on_bus_msg_id_);
  }
  if (sink_ && handoff_handler_id_) {
    g_signal_handler_disconnect(G_OBJECT(sink_), handoff_handler_id_);
  }
  if (playbin_ && source_setup_id_) {
    g_signal_handler_disconnect(G_OBJECT(playbin_), source_setup_id_);
    source_setup_id_ = 0;
  }
  if (playbin_ && deep_element_added_id_) {
    g_signal_handler_disconnect(G_OBJECT(playbin_), deep_element_added_id_);
    deep_element_added_id_ = 0;
  }

  if (stats_tick_source_id_) {
    g_source_remove(stats_tick_source_id_);
    stats_tick_source_id_ = 0;
  }

  if (playbin_) {
    gst_element_set_state(playbin_, GST_STATE_NULL);
  }

  if (has_video_) {
    // Ensure no in-flight handoff callback is using the shader. set_state
    // NULL above has already joined the streaming thread, so this lock is
    // just paranoia against late signal dispatch.
    std::lock_guard gst_lock(gst_mutex_);
    if (shader_) {
      if (!use_legacy_context_) {
        MakeContextCurrent();
        DrainInFlightFrames();
        shader_.reset();
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
      } else {
        std::lock_guard ctx_lock(g_texture_context_mutex);
        m_registrar->texture_registrar()->TextureMakeCurrent();
        shader_.reset();
        m_registrar->texture_registrar()->TextureClearCurrent();
      }
    }
    DestroySharedGlContext();
  }

  if (has_video_ && m_texture_id != 0) {
    SPDLOG_DEBUG("[VideoPlayer] Unregistering texture_id={}", m_texture_id);
    m_registrar->texture_registrar()->UnregisterTexture(m_texture_id);
    m_texture_id = 0;
  }

  if (bus_) {
    gst_object_unref(bus_);
    bus_ = nullptr;
  }

  {
    std::lock_guard event_lock(event_mutex_);
    event_sink_ = nullptr;
  }
  event_channel_ = nullptr;

  {
    std::lock_guard stats_lock(stats_event_mutex_);
    stats_event_sink_ = nullptr;
  }
  stats_event_channel_ = nullptr;
}

void VideoPlayer::SetLooping(const bool isLooping) {
  SPDLOG_DEBUG("[VideoPlayer] SetLooping: {}", is_looping_.load());
  is_looping_ = isLooping;
}

void VideoPlayer::SetVolume(const double volume) {
  SPDLOG_DEBUG("[VideoPlayer] SetVolume: {}", volume);
  volume_ = volume;
  g_object_set(playbin_, "volume", volume, nullptr);
}

void VideoPlayer::SetPlaybackSpeed(const double playbackSpeed) {
  // Store the desired rate so it can be applied when the pipeline is ready
  pending_rate_.store(playbackSpeed);

  // Seek events require the pipeline to be in PAUSED or PLAYING state
  GstState state;
  gst_element_get_state(playbin_, &state, nullptr, 0);
  if (state < GST_STATE_PAUSED) {
    SPDLOG_DEBUG(
        "[VideoPlayer] Deferring playback speed {} until pipeline is ready",
        playbackSpeed);
    return;
  }

  ApplyPlaybackSpeed();
}

void VideoPlayer::Play() {
  target_state_ = GST_STATE_PLAYING;
  const GstStateChangeReturn ret =
      gst_element_set_state(playbin_, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    spdlog::error("[VideoPlayer] Failed to set state GST_STATE_PLAYING.");
  } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
    is_live_ = TRUE;
    SPDLOG_DEBUG("[VideoPlayer] Pipeline is live");
  }
}

void VideoPlayer::Pause() {
  GstState state;
  gst_element_get_state(playbin_, &state, nullptr, GST_SECOND);
  if (state != GST_STATE_NULL) {
    target_state_ = GST_STATE_PAUSED;
    const GstStateChangeReturn ret =
        gst_element_set_state(playbin_, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
      std::lock_guard event_lock(event_mutex_);
      if (event_sink_) {
        event_sink_->Error("[VideoPlayer] Unable to Pause Transport.");
      }
      return;
    }
    SPDLOG_DEBUG("[VideoPlayer] Transport Paused.");
  }
}

int64_t VideoPlayer::GetPosition() {
  gint64 pos = 0;
  if (gst_element_query_position(playbin_, GST_FORMAT_TIME, &pos)) {
    position_ = pos;
    SPDLOG_TRACE("[VideoPlayer] Position: {}", pos);
  }
  const gint64 current = position_.load();
  return current >= 0 ? current / GST_MSECOND : 0;
}

void VideoPlayer::SendBufferingUpdate() {
  std::lock_guard event_lock(event_mutex_);
  if (!event_sink_) {
    return;
  }
  auto values = flutter::EncodableList();

  GstQuery* query = gst_query_new_buffering(GST_FORMAT_TIME);
  if (gst_element_query(playbin_, query)) {
    const guint n_ranges = gst_query_get_n_buffering_ranges(query);
    for (guint i = 0; i < n_ranges; i++) {
      gint64 start = 0, stop = 0;
      if (gst_query_parse_nth_buffering_range(query, i, &start, &stop)) {
        values.emplace_back(
            std::in_place_type<flutter::EncodableList>,
            flutter::EncodableList{
                flutter::EncodableValue(std::in_place_type<int64_t>,
                                        start / GST_MSECOND),
                flutter::EncodableValue(std::in_place_type<int64_t>,
                                        stop / GST_MSECOND)});
      }
    }
  }
  gst_query_unref(query);

  auto res = flutter::EncodableMap(
      {{flutter::EncodableValue("event"),
        flutter::EncodableValue("bufferingUpdate")},
       {flutter::EncodableValue("values"),
        flutter::EncodableValue(std::in_place_type<flutter::EncodableList>,
                                std::move(values))}});
  event_sink_->Success(flutter::EncodableValue(
      std::in_place_type<flutter::EncodableMap>, std::move(res)));
}

void VideoPlayer::SeekTo(const int64_t seek) {
  const gint64 position = seek * GST_MSECOND;
  SPDLOG_DEBUG("[VideoPlayer] SeekTo: {} -> {}", seek, position);

  if (!gst_element_seek_simple(
          playbin_, GST_FORMAT_TIME,
          static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH |
                                    GST_SEEK_FLAG_KEY_UNIT),
          position)) {
    SPDLOG_ERROR("[VideoPlayer] Seek Failed");
  }
  gint64 pos;
  gst_element_query_position(playbin_, GST_FORMAT_TIME, &pos);
  position_ = pos;
  SPDLOG_DEBUG("[VideoPlayer] SeekTo: {} -> {}", seek, pos);

  // gst_element_seek_simple resets the segment rate to 1.0. If the user
  // had set a non-default rate, re-apply it now so dragging the scrubber
  // doesn't silently drop the rate back to 1×.
  if (pending_rate_.load() != 1.0) {
    rate_.store(
        -2.0);  // sentinel; forces ApplyPlaybackSpeed to send a real seek
    ApplyPlaybackSpeed();
  }
}

bool VideoPlayer::IsValid() {
  return m_valid;
}

void VideoPlayer::Init(flutter::BinaryMessenger* messenger) {
  if (is_initialized_) {
    return;
  }

  SPDLOG_DEBUG("[VideoPlayer] Init");
  event_channel_ = std::make_unique<flutter::EventChannel<>>(
      messenger,
      std::string("flutter.io/videoPlayer/videoEvents") +
          std::to_string(m_texture_id),
      &flutter::StandardMethodCodec::GetInstance());

  event_channel_->SetStreamHandler(
      std::make_unique<flutter::StreamHandlerFunctions<>>(
          [this](const flutter::EncodableValue* /* arguments */,
                 std::unique_ptr<flutter::EventSink<>>&& events)
              -> std::unique_ptr<flutter::StreamHandlerError<>> {
            {
              std::lock_guard event_lock(event_mutex_);
              event_sink_ = std::move(events);
            }
            // Send `initialized` as soon as the event sink attaches, using
            // the GstDiscoverer metadata already captured at construction.
            // Waiting for PLAYING deadlocks the MiniController — Dart awaits
            // the `initialized` event inside `initialize()`, and only then
            // calls `play()` which is what transitions the pipeline to
            // PLAYING. Upstream video_player on Android/iOS emits
            // `initialized` off the source metadata, not a rendered frame,
            // for the same reason. `sent_initialized_` guards against the
            // handoff path (or the PLAYING state-change handler for
            // audio-only) re-emitting after the first frame lands.
            if (!sent_initialized_) {
              sent_initialized_ = true;
              SendInitialized();
            }
            SendMediaMetadata();
            if (!initial_album_art_.empty()) {
              SendAlbumArt(initial_album_art_, initial_album_art_mime_);
            }
            SendAudioInfo();
            return nullptr;
          },
          [this](const flutter::EncodableValue* /* arguments */)
              -> std::unique_ptr<flutter::StreamHandlerError<>> {
            std::lock_guard event_lock(event_mutex_);
            event_sink_ = nullptr;
            return nullptr;
          }));

  // Phase 0.2 — stats event channel. One per-player channel keyed by
  // texture_id. Starts a 1Hz push only when someone listens; silent and
  // zero-cost otherwise.
  stats_event_channel_ = std::make_unique<flutter::EventChannel<>>(
      messenger,
      std::string("video_player_linux/stats/") + std::to_string(m_texture_id),
      &flutter::StandardMethodCodec::GetInstance());

  stats_event_channel_->SetStreamHandler(
      std::make_unique<flutter::StreamHandlerFunctions<>>(
          [this](const flutter::EncodableValue* /* arguments */,
                 std::unique_ptr<flutter::EventSink<>>&& events)
              -> std::unique_ptr<flutter::StreamHandlerError<>> {
            {
              std::lock_guard stats_lock(stats_event_mutex_);
              stats_event_sink_ = std::move(events);
            }
            // Emit the current snapshot immediately so a subscriber doesn't
            // have to wait a full second for the first reading.
            EmitStats();
            if (!stats_tick_source_id_) {
              stats_tick_source_id_ =
                  g_timeout_add_seconds(1, &VideoPlayer::OnStatsTick, this);
            }
            return nullptr;
          },
          [this](const flutter::EncodableValue* /* arguments */)
              -> std::unique_ptr<flutter::StreamHandlerError<>> {
            if (stats_tick_source_id_) {
              g_source_remove(stats_tick_source_id_);
              stats_tick_source_id_ = 0;
            }
            std::lock_guard stats_lock(stats_event_mutex_);
            stats_event_sink_ = nullptr;
            return nullptr;
          }));
}

gboolean VideoPlayer::OnStatsTick(gpointer user_data) {
  auto* self = static_cast<VideoPlayer*>(user_data);
  if (!self->m_valid) {
    return G_SOURCE_REMOVE;
  }
  self->EmitStats();
  return G_SOURCE_CONTINUE;
}

void VideoPlayer::EmitStats() {
  std::lock_guard stats_lock(stats_event_mutex_);
  if (!stats_event_sink_) {
    return;
  }

  // Snapshot integer counters (atomics). The string metadata has its own
  // mutex because std::string assignments aren't atomic.
  const uint64_t recv = stats_.frames_received.load(std::memory_order_relaxed);
  const uint64_t rend = stats_.frames_rendered.load(std::memory_order_relaxed);
  const uint64_t drop = stats_.frames_dropped.load(std::memory_order_relaxed);
  const uint64_t late = stats_.frames_late.load(std::memory_order_relaxed);
  const uint64_t upload_ns =
      stats_.upload_ns_total.load(std::memory_order_relaxed);
  const uint64_t render_ns =
      stats_.render_ns_total.load(std::memory_order_relaxed);
  const int64_t last_pts =
      stats_.last_frame_pts_ns.load(std::memory_order_relaxed);
  const int64_t last_wall =
      stats_.last_wallclock_ns.load(std::memory_order_relaxed);

  std::string fmt, colorspace, decoder;
  {
    std::lock_guard meta_lock(stats_.meta_mutex);
    fmt = stats_.negotiated_format;
    colorspace = stats_.negotiated_colorspace;
    decoder = stats_.decoder_name;
  }

  // EncodableValue stores ints as int64; the load values can exceed int64
  // only after decades of 4K60, so the reinterpret is safe in practice.
  auto map = flutter::EncodableMap{
      {flutter::EncodableValue("frames_received"),
       flutter::EncodableValue(static_cast<int64_t>(recv))},
      {flutter::EncodableValue("frames_rendered"),
       flutter::EncodableValue(static_cast<int64_t>(rend))},
      {flutter::EncodableValue("frames_dropped"),
       flutter::EncodableValue(static_cast<int64_t>(drop))},
      {flutter::EncodableValue("frames_late"),
       flutter::EncodableValue(static_cast<int64_t>(late))},
      {flutter::EncodableValue("upload_ns_total"),
       flutter::EncodableValue(static_cast<int64_t>(upload_ns))},
      {flutter::EncodableValue("render_ns_total"),
       flutter::EncodableValue(static_cast<int64_t>(render_ns))},
      {flutter::EncodableValue("last_frame_pts_ns"),
       flutter::EncodableValue(last_pts)},
      {flutter::EncodableValue("last_wallclock_ns"),
       flutter::EncodableValue(last_wall)},
      {flutter::EncodableValue("negotiated_format"),
       flutter::EncodableValue(fmt)},
      {flutter::EncodableValue("negotiated_colorspace"),
       flutter::EncodableValue(colorspace)},
      {flutter::EncodableValue("decoder_name"),
       flutter::EncodableValue(decoder)},
      {flutter::EncodableValue("uses_dmabuf"),
       flutter::EncodableValue(
           stats_.uses_dmabuf.load(std::memory_order_relaxed))},
      {flutter::EncodableValue("uses_hw_decoder"),
       flutter::EncodableValue(
           stats_.uses_hw_decoder.load(std::memory_order_relaxed))},
      {flutter::EncodableValue("uses_shared_gl_context"),
       flutter::EncodableValue(
           stats_.uses_shared_gl_context.load(std::memory_order_relaxed))},
  };
  stats_event_sink_->Success(flutter::EncodableValue(
      std::in_place_type<flutter::EncodableMap>, std::move(map)));
}

void VideoPlayer::OnDeepElementAdded(GstBin* /* bin */,
                                     GstBin* /* sub_bin */,
                                     GstElement* element,
                                     gpointer user_data) {
  if (!element) {
    return;
  }
  GstElementFactory* factory = gst_element_get_factory(element);
  if (!factory) {
    return;
  }

  // Restrict decoder detection to video decoders. "Codec/Decoder/Video" is
  // the standard klass string for both software decoders (avdec_*) and
  // stateless/stateful V4L2 decoders (v4l2h264dec, v4l2slh264dec), VA-API
  // decoders, and vendor elements (vpudec, omxh264dec, mppvideodec).
  // Classification lets us ignore sinks, demuxers and parsers that also
  // hit this callback.
  const gchar* klass =
      gst_element_factory_get_metadata(factory, GST_ELEMENT_METADATA_KLASS);
  if (!klass || !g_strrstr(klass, "Decoder") || !g_strrstr(klass, "Video")) {
    return;
  }
  const gchar* name = GST_OBJECT_NAME(factory);
  auto* self = static_cast<VideoPlayer*>(user_data);
  {
    std::lock_guard meta_lock(self->stats_.meta_mutex);
    self->stats_.decoder_name = name ? name : "";
  }
  // Heuristic: decoder names starting with "v4l2", "omx", "mpp", "vpu",
  // "vaapi", "nvh264" are hardware-backed. Cheap and good enough for the
  // stats channel; precise hardware flag comes from Phase 2+ backends.
  const bool hw =
      name &&
      (g_str_has_prefix(name, "v4l2") || g_str_has_prefix(name, "omx") ||
       g_str_has_prefix(name, "mpp") || g_str_has_prefix(name, "vpu") ||
       g_str_has_prefix(name, "vaapi") || g_str_has_prefix(name, "nvh264") ||
       g_str_has_prefix(name, "msdk"));
  self->stats_.uses_hw_decoder.store(hw, std::memory_order_relaxed);
  SPDLOG_DEBUG("[VideoPlayer] Decoder: {} (hw={})", name ? name : "?", hw);
}

void VideoPlayer::SetBuffering(const bool buffering) {
  std::lock_guard event_lock(event_mutex_);
  if (event_sink_) {
    SPDLOG_DEBUG("[VideoPlayer] SetBuffering: {}", buffering);
    auto res = flutter::EncodableMap(
        {{flutter::EncodableValue("event"),
          flutter::EncodableValue(buffering ? "bufferingStart"
                                            : "bufferingEnd")}});
    event_sink_->Success(flutter::EncodableValue(
        std::in_place_type<flutter::EncodableMap>, std::move(res)));
  }
}

void VideoPlayer::ApplyPlaybackSpeed() {
  const double pending = pending_rate_.load();
  if (pending == rate_.load()) {
    return;
  }

  // First-time rate application uses a NON-flushing seek. A flush-seek at
  // the first PLAYING transition resets souphttpsrc, forcing it to
  // re-fetch the entire stream from the start — which stalls cold HTTP
  // playback at frame #1 until the re-download catches up. The -2.0
  // sentinel still exists to ensure rate is explicitly established (to
  // defeat stray inherited segment rates), but the NONE flag lets
  // in-flight buffers continue flowing while the new rate propagates.
  // Mid-stream rate changes after the first call keep the FLUSH+ACCURATE
  // flags so the pipeline immediately snaps to the new rate.
  const bool first_time = (rate_.load() == -2.0);
  const auto playbackSpeed = pending;
  gint64 pos = 0;
  if (!gst_element_query_position(playbin_, GST_FORMAT_TIME, &pos)) {
    pos = position_.load();
  }
  const GstSeekFlags flush_flags =
      first_time ? GST_SEEK_FLAG_NONE
                 : static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH |
                                             GST_SEEK_FLAG_ACCURATE);
  GstEvent* seek_event = nullptr;
  if (playbackSpeed > 0) {
    // Canonical rate-only seek: SET start at the current position, NONE
    // for the stop. Earlier we passed END/0 for the stop which on some
    // sinks collapses the segment to zero length and silently squashes
    // the rate change.
    seek_event = gst_event_new_seek(playbackSpeed, GST_FORMAT_TIME, flush_flags,
                                    GST_SEEK_TYPE_SET, pos, GST_SEEK_TYPE_NONE,
                                    static_cast<gint64>(GST_CLOCK_TIME_NONE));
  } else {
    // Reverse playback: walk from start to the current position.
    seek_event =
        gst_event_new_seek(playbackSpeed, GST_FORMAT_TIME, flush_flags,
                           GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, pos);
  }

  if (!gst_element_send_event(playbin_, seek_event)) {
    SPDLOG_ERROR("[VideoPlayer] Failed to set playback speed: {}",
                 playbackSpeed);
    return;
  }
  rate_.store(playbackSpeed);
  SPDLOG_DEBUG("[VideoPlayer] Playback speed -> {}", playbackSpeed);
}

gboolean VideoPlayer::OnAudioRecovery(gpointer user_data) {
  auto* self = static_cast<VideoPlayer*>(user_data);
  SPDLOG_DEBUG("[VideoPlayer] Audio recovery: restarting without audio");

  gint flags = 0;
  g_object_get(self->playbin_, "flags", &flags, nullptr);
  flags &= ~GST_PLAY_FLAG_AUDIO;
  g_object_set(self->playbin_, "flags", flags, nullptr);

  self->is_buffering_ = false;
  self->is_initialized_ = false;
  self->sent_initialized_ = false;

  // Pipeline restart resets segment rate; force ApplyPlaybackSpeed to
  // re-seek when we reach PLAYING again.
  self->rate_.store(-2.0);
  gst_element_set_state(self->playbin_, GST_STATE_NULL);
  gst_element_set_state(self->playbin_,
                        static_cast<GstState>(self->target_state_.load()));
  return G_SOURCE_REMOVE;
}

void VideoPlayer::StartAudioMonitor() {
  if (udev_mon_)
    return;  // already running

  udev_ = udev_new();
  if (!udev_)
    return;

  udev_mon_ = udev_monitor_new_from_netlink(udev_, "udev");
  if (!udev_mon_) {
    udev_unref(udev_);
    udev_ = nullptr;
    return;
  }

  udev_monitor_filter_add_match_subsystem_devtype(udev_mon_, "sound", nullptr);
  udev_monitor_enable_receiving(udev_mon_);

  int fd = udev_monitor_get_fd(udev_mon_);
  udev_channel_ = g_io_channel_unix_new(fd);
  udev_watch_id_ = g_io_add_watch(udev_channel_, G_IO_IN, OnUdevEvent, this);

  SPDLOG_DEBUG("[VideoPlayer] Audio hotplug monitor started");

  // Check if a PCM device already exists (device may have appeared
  // before the monitor was set up).
  struct udev_enumerate* enumerate = udev_enumerate_new(udev_);
  udev_enumerate_add_match_subsystem(enumerate, "sound");
  udev_enumerate_scan_devices(enumerate);
  struct udev_list_entry* entry;
  udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate)) {
    const char* path = udev_list_entry_get_name(entry);
    struct udev_device* dev = udev_device_new_from_syspath(udev_, path);
    if (dev) {
      const char* sysname = udev_device_get_sysname(dev);
      if (sysname && strncmp(sysname, "pcmC", 4) == 0) {
        SPDLOG_DEBUG("[VideoPlayer] Audio device already present: {}", sysname);
        udev_device_unref(dev);
        // Schedule upgrade immediately. Track the GSource id so we can
        // cancel it in StopAudioMonitor if Dispose runs first.
        if (!audio_upgrade_idle_id_) {
          GSource* idle = g_idle_source_new();
          g_source_set_callback(idle, OnAudioUpgrade, this, nullptr);
          audio_upgrade_idle_id_ = g_source_attach(idle, context_);
          g_source_unref(idle);
        }
        break;
      }
      udev_device_unref(dev);
    }
  }
  udev_enumerate_unref(enumerate);
}

void VideoPlayer::StopAudioMonitor() {
  // Cancel any pending OnAudioUpgrade idle source first so it can't fire
  // on a half-torn-down player.
  if (audio_upgrade_idle_id_) {
    g_source_remove(audio_upgrade_idle_id_);
    audio_upgrade_idle_id_ = 0;
  }
  if (udev_watch_id_) {
    g_source_remove(udev_watch_id_);
    udev_watch_id_ = 0;
  }
  if (udev_channel_) {
    g_io_channel_unref(udev_channel_);
    udev_channel_ = nullptr;
  }
  if (udev_mon_) {
    udev_monitor_unref(udev_mon_);
    udev_mon_ = nullptr;
  }
  if (udev_) {
    udev_unref(udev_);
    udev_ = nullptr;
  }
}

gboolean VideoPlayer::OnUdevEvent(GIOChannel* /*channel*/,
                                  GIOCondition /*cond*/,
                                  gpointer user_data) {
  auto* self = static_cast<VideoPlayer*>(user_data);
  struct udev_device* dev = udev_monitor_receive_device(self->udev_mon_);
  if (!dev)
    return G_SOURCE_CONTINUE;

  const char* action = udev_device_get_action(dev);
  const char* sysname = udev_device_get_sysname(dev);

  if (action && sysname && strcmp(action, "add") == 0 &&
      strncmp(sysname, "pcmC", 4) == 0 && !self->audio_upgraded_ &&
      !self->audio_upgrade_idle_id_) {
    SPDLOG_DEBUG("[VideoPlayer] Audio device hotplugged: {}", sysname);
    // Schedule the upgrade on an idle callback to avoid reentrancy.
    // Track the source id so it can be cancelled if Dispose runs first.
    GSource* idle = g_idle_source_new();
    g_source_set_callback(idle, OnAudioUpgrade, self, nullptr);
    self->audio_upgrade_idle_id_ = g_source_attach(idle, self->context_);
    g_source_unref(idle);
  }

  udev_device_unref(dev);
  return G_SOURCE_CONTINUE;
}

gboolean VideoPlayer::OnAudioUpgrade(gpointer user_data) {
  auto* self = static_cast<VideoPlayer*>(user_data);
  // Clear the tracked source id first — once we return REMOVE, the
  // GSource is gone, and StopAudioMonitor must not call g_source_remove
  // on a stale id.
  self->audio_upgrade_idle_id_ = 0;
  if (self->audio_upgraded_ || !self->m_valid) {
    return G_SOURCE_REMOVE;
  }

  // Try to create a real audio sink.  If it can reach READY, swap it in.
  GstElement* real_sink = gst_element_factory_make("autoaudiosink", nullptr);
  if (!real_sink) {
    spdlog::warn("[VideoPlayer] Audio upgrade: autoaudiosink unavailable");
    return G_SOURCE_REMOVE;
  }

  if (gst_element_set_state(real_sink, GST_STATE_READY) ==
      GST_STATE_CHANGE_FAILURE) {
    gst_element_set_state(real_sink, GST_STATE_NULL);
    gst_object_unref(real_sink);
    spdlog::warn("[VideoPlayer] Audio upgrade: sink not ready yet");
    return G_SOURCE_REMOVE;  // udev will trigger us again on next hotplug
  }
  gst_element_set_state(real_sink, GST_STATE_NULL);

  // Hot-swap: pause playbin, swap audio-sink, resume
  SPDLOG_DEBUG("[VideoPlayer] Audio upgrade: swapping to real audio sink");
  GstState cur_state = GST_STATE_NULL;
  gst_element_get_state(self->playbin_, &cur_state, nullptr, 0);

  // READY round-trip resets segment rate; invalidate cache so the next
  // PLAYING transition re-applies pending_rate_.
  self->rate_.store(-2.0);
  gst_element_set_state(self->playbin_, GST_STATE_READY);
  gst_element_get_state(self->playbin_, nullptr, nullptr, 2 * GST_SECOND);

  g_object_set(self->playbin_, "audio-sink", real_sink, nullptr);

  gst_element_set_state(self->playbin_, cur_state);
  self->audio_upgraded_ = true;
  self->StopAudioMonitor();
  SPDLOG_DEBUG("[VideoPlayer] Audio upgrade: done");
  return G_SOURCE_REMOVE;
}

void VideoPlayer::OnPlaybackEnded() {
  if (is_looping_) {
    SPDLOG_DEBUG("[VideoPlayer] Looping: restarting pipeline");
    is_buffering_ = false;
    // The READY round-trip resets the pipeline's segment rate to 1.0.
    // Invalidate our cached rate so the next ApplyPlaybackSpeed call
    // (fired from OnMediaStateChange on PLAYING) re-sends the user's
    // chosen pending_rate_.
    rate_.store(-2.0);
    gst_element_set_state(playbin_, GST_STATE_READY);
    gst_element_set_state(playbin_, GST_STATE_PLAYING);
    return;
  }
  std::lock_guard event_lock(event_mutex_);
  if (event_sink_) {
    SPDLOG_DEBUG("[VideoPlayer] OnPlaybackEnded");
    auto res = flutter::EncodableMap({{flutter::EncodableValue("event"),
                                       flutter::EncodableValue("completed")}});
    event_sink_->Success(flutter::EncodableValue(
        std::in_place_type<flutter::EncodableMap>, std::move(res)));
  }
}

void VideoPlayer::OnMediaInitialized() {}

void VideoPlayer::OnMediaStateChange(const GstState state) {
  if (state == GST_STATE_NULL) {
    SetBuffering(true);
    SendBufferingUpdate();
  } else {
    SetBuffering(false);

    if (state == GST_STATE_PLAYING) {
      SPDLOG_DEBUG("[VideoPlayer] message state changed, start playing {}",
                   m_texture_id);
      audio_recovery_ = false;
      if (has_video_) {
        prepare(this);
      }
      is_initialized_ = true;
      ever_played_ = true;
      ApplyPlaybackSpeed();

      // For audio-only there are no video frames, so the handoff path will
      // never fire SendInitialized — emit it directly on first PLAYING.
      if (!has_video_ && !sent_initialized_) {
        sent_initialized_ = true;
        SendInitialized();
      }
      // Forward seeded discoverer metadata + album art on each PLAYING
      // transition. Idempotent on the Dart side.
      SendMediaMetadata();
      if (!initial_album_art_.empty()) {
        SendAlbumArt(initial_album_art_, initial_album_art_mime_);
      }
      SendAudioInfo();

      // If audio was initially a fakesink, monitor for real device hotplug.
      if (!audio_upgraded_) {
        StartAudioMonitor();
      }
    } else if (state == GST_STATE_READY) {
      SPDLOG_DEBUG("[VideoPlayer] message state changed, ready {}",
                   m_texture_id);
    }
  }
}

void VideoPlayer::OnMediaError(GstMessage* msg) {
  GError* err;
  gchar* debug_info;
  gst_message_parse_error(msg, &err, &debug_info);
  const std::string error_msg = err->message ? err->message : "Unknown error";
  spdlog::error("[VideoPlayer] Error: {}:{}", GST_OBJECT_NAME(msg->src),
                error_msg);
  if (debug_info) {
    spdlog::error("[VideoPlayer] {}", debug_info);
    g_free(debug_info);
  }
  g_clear_error(&err);

  std::lock_guard event_lock(event_mutex_);
  if (event_sink_) {
    event_sink_->Error("VideoPlayerError", error_msg);
  }
}

void VideoPlayer::OnMediaDurationChange() {
  GstQuery* query = gst_query_new_duration(GST_FORMAT_TIME);
  if (gst_element_query(playbin_, query)) {
    gst_query_parse_duration(query, nullptr, &duration_);
  }
  gst_query_unref(query);
}

void VideoPlayer::SendInitialized() {
  std::lock_guard event_lock(event_mutex_);
  if (!event_sink_) {
    return;
  }
  auto event = flutter::EncodableMap(
      {{flutter::EncodableValue("event"),
        flutter::EncodableValue("initialized")},
       {flutter::EncodableValue("duration"),
        flutter::EncodableValue(
            std::in_place_type<int64_t>,
            static_cast<int64_t>(duration_ / GST_MSECOND))}});

  event.insert({flutter::EncodableValue("width"),
                flutter::EncodableValue(std::in_place_type<int32_t>,
                                        static_cast<int32_t>(width_))});
  event.insert({flutter::EncodableValue("height"),
                flutter::EncodableValue(std::in_place_type<int32_t>,
                                        static_cast<int32_t>(height_))});
  event.insert({flutter::EncodableValue("isAudioOnly"),
                flutter::EncodableValue(!has_video_)});

  event_sink_->Success(flutter::EncodableValue(
      std::in_place_type<flutter::EncodableMap>, std::move(event)));
}

void VideoPlayer::OnTag(const GstTagList* list,
                        const gchar* tag,
                        gpointer /* user_data */) {
  const std::string tag_str = tag;
  if (const auto type = gst_tag_get_type(tag);
      tag_str == "audio-codec" && type == 64) {
    gchar* value = nullptr;
    if (gst_tag_list_get_string(list, tag, &value) && value) {
      spdlog::debug("[VideoPlayer] audio-codec: {}", value);
      g_free(value);
    }
  } else if (tag_str == "video-codec" && type == 64) {
    gchar* value = nullptr;
    if (gst_tag_list_get_string(list, tag, &value) && value) {
      spdlog::debug("[VideoPlayer] video-codec: {}", value);
      g_free(value);
    }
  } else if (tag_str == "maximum-bitrate" && type == 28) {
    guint value = 0;
    if (gst_tag_list_get_uint(list, tag, &value)) {
      spdlog::debug("[VideoPlayer] maximum-bitrate: {}", value);
    }
  } else if (tag_str == "minimum-bitrate" && type == 28) {
    guint value = 0;
    if (gst_tag_list_get_uint(list, tag, &value)) {
      spdlog::debug("[VideoPlayer] minimum-bitrate: {}", value);
    }
  } else if (tag_str == "bitrate" && type == 28) {
    guint value = 0;
    if (gst_tag_list_get_uint(list, tag, &value)) {
      spdlog::debug("[VideoPlayer] bitrate: {}", value);
    }
  }
}

GstFlowReturn VideoPlayer::OnNewSample(void* appsink_ptr, void* user_data) {
  auto* self = static_cast<VideoPlayer*>(user_data);
  auto* appsink = static_cast<GstAppSink*>(appsink_ptr);

  GstSample* sample = gst_app_sink_pull_sample(appsink);
  if (!sample) {
    // Either EOS or shutdown. Let playbin handle it via the bus.
    return GST_FLOW_EOS;
  }

  GstBuffer* buf = gst_sample_get_buffer(sample);
  if (!buf) {
    gst_sample_unref(sample);
    return GST_FLOW_ERROR;
  }

  // Phase 1.2 — try to extract dmabuf frame metadata. On success we log
  // the import-shape for visibility (Phase 1.3 will replace the fall-
  // through with an EGLImage import); on failure (software decoder,
  // unsupported format, weird memory layout) we leave stats_.uses_dmabuf
  // cleared and route the buffer through the existing CPU upload path.
  const bool n_mem_ok = gst_buffer_n_memory(buf) > 0;
  const bool mem0_is_dmabuf =
      n_mem_ok && gst_is_dmabuf_memory(gst_buffer_peek_memory(buf, 0));
  std::optional<DmabufFrame> dmabuf;
  if (mem0_is_dmabuf) {
    dmabuf = ExtractDmabufFrame(sample);
  }

  const bool uses_dmabuf_now = dmabuf.has_value();
  const bool uses_dmabuf_prev =
      self->stats_.uses_dmabuf.load(std::memory_order_relaxed);
  if (uses_dmabuf_now != uses_dmabuf_prev) {
    self->stats_.uses_dmabuf.store(uses_dmabuf_now, std::memory_order_relaxed);
    if (uses_dmabuf_now) {
      const auto fourcc = dmabuf->drm_fourcc;
      SPDLOG_DEBUG(
          "[VideoPlayer] dmabuf sample: {}x{} fourcc={:c}{:c}{:c}{:c} "
          "modifier=0x{:x} planes={} fd0={} off0={} stride0={}",
          dmabuf->width, dmabuf->height, fourcc & 0xff, (fourcc >> 8) & 0xff,
          (fourcc >> 16) & 0xff, (fourcc >> 24) & 0xff, dmabuf->drm_modifier,
          dmabuf->n_planes, dmabuf->planes[0].fd, dmabuf->planes[0].offset,
          dmabuf->planes[0].stride);
    } else if (mem0_is_dmabuf) {
      SPDLOG_DEBUG(
          "[VideoPlayer] Appsink sample is dmabuf but extraction failed "
          "(unsupported format or memory layout); CPU upload fallback");
    } else {
      SPDLOG_DEBUG(
          "[VideoPlayer] Appsink delivering raw (non-dmabuf) NV12; staying "
          "on CPU upload path");
    }
  }

  if (dmabuf.has_value() && self->render_path_ == RenderPath::DmabufZeroCopy) {
    // Zero-copy path — Phases 1.3/1.4. Make our EGL context current
    // on this thread, import the dmabuf as an EGLImage, bind to the
    // shader's texture name, and tell Flutter a new frame is
    // available. The sample pointer is handed off to ImportDmabufFrame
    // which ref's it into the in-flight deque; on success it owns the
    // ref and we must NOT unref it here.
    std::lock_guard lock(self->gst_mutex_);
    if (self->m_valid && self->is_initialized_ && self->has_video_) {
      self->MakeContextCurrent();
      const bool ok = self->ImportDmabufFrame(*dmabuf, sample);
      glFlush();
      eglMakeCurrent(self->egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                     EGL_NO_CONTEXT);
      if (ok) {
        self->stats_.frames_rendered.fetch_add(1, std::memory_order_relaxed);
        if (!self->sent_initialized_) {
          self->sent_initialized_ = true;
          self->SendInitialized();
        }
        self->m_registrar->texture_registrar()->MarkTextureFrameAvailable(
            self->m_texture_id);
        // Sample ref is owned by the in-flight deque now.
        return GST_FLOW_OK;
      }
      // Import failed — fall through to CPU upload so the user still
      // sees something. Next frame will try again; if the failure is
      // systemic we're effectively downgraded without tearing down.
      self->stats_.frames_dropped.fetch_add(1, std::memory_order_relaxed);
    }
  }

  // CPU fall-through: either the sample isn't dmabuf (software
  // decoder), extraction failed (unsupported format / weird layout),
  // or EGLImage import failed (driver quirk). handoff_handler's first
  // arg is unused; passing the appsink as the element is fine.
  handoff_handler(reinterpret_cast<GstElement*>(appsink), buf, nullptr, self);

  gst_sample_unref(sample);
  return GST_FLOW_OK;
}

void VideoPlayer::handoff_handler(GstElement* /* fakesink */,
                                  GstBuffer* buffer,
                                  GstPad* /* pad */,
                                  void* user_data) {
  const auto obj = static_cast<VideoPlayer*>(user_data);
  if (!obj->m_valid || !obj->is_initialized_ || !obj->has_video_) {
    return;
  }

  obj->stats_.frames_received.fetch_add(1, std::memory_order_relaxed);

  std::lock_guard lock(obj->gst_mutex_);
  if (obj->info_.finfo == nullptr || !obj->shader_) {
    obj->stats_.frames_dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  // The target caps on the fakesink pin NV12 at the player's configured
  // width/height. If something renegotiated behind our back (stream switch,
  // resolution change), the shader's textures/FBO are sized for the wrong
  // geometry and we'd either sample garbage or crash in glTexImage2D. Fail
  // loud instead — the caller is expected to tear down and rebuild the
  // player on resolution change, not patch it mid-frame.
  if (GST_VIDEO_INFO_FORMAT(&obj->info_) != GST_VIDEO_FORMAT_NV12 ||
      GST_VIDEO_INFO_N_PLANES(&obj->info_) != 2 ||
      GST_VIDEO_INFO_WIDTH(&obj->info_) != obj->width_ ||
      GST_VIDEO_INFO_HEIGHT(&obj->info_) != obj->height_) {
    spdlog::error(
        "[VideoPlayer] Unexpected frame geometry: format={} planes={} "
        "{}x{} (expected NV12 {}x{}); dropping frame",
        gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&obj->info_)),
        static_cast<unsigned>(GST_VIDEO_INFO_N_PLANES(&obj->info_)),
        static_cast<int>(GST_VIDEO_INFO_WIDTH(&obj->info_)),
        static_cast<int>(GST_VIDEO_INFO_HEIGHT(&obj->info_)), obj->width_,
        obj->height_);
    obj->stats_.frames_dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  GstVideoFrame frame;
  if (gst_video_frame_map(&frame, &obj->info_, buffer, GST_MAP_READ)) {
    using clock = std::chrono::steady_clock;
    const auto now = clock::now();

    // Late-frame heuristic: compare inter-frame wallclock delta against the
    // inter-frame PTS delta; if wallclock outran PTS by > 33ms this frame
    // got here later than the pipeline clock wanted. First frame (last_pts=0)
    // has nothing to compare against and is never marked late.
    const auto buf_pts = static_cast<int64_t>(GST_BUFFER_PTS(buffer));
    const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               now.time_since_epoch())
                               .count();
    const int64_t prev_pts =
        obj->stats_.last_frame_pts_ns.load(std::memory_order_relaxed);
    const int64_t prev_wall =
        obj->stats_.last_wallclock_ns.load(std::memory_order_relaxed);
    if (prev_pts > 0 && buf_pts > prev_pts && prev_wall > 0) {
      const int64_t wall_delta = now_ns - prev_wall;
      const int64_t pts_delta = buf_pts - prev_pts;
      if (wall_delta - pts_delta > 33'000'000) {
        obj->stats_.frames_late.fetch_add(1, std::memory_order_relaxed);
      }
    }
    obj->stats_.last_frame_pts_ns.store(buf_pts, std::memory_order_relaxed);
    obj->stats_.last_wallclock_ns.store(now_ns, std::memory_order_relaxed);

    const auto gl_work = [&]() {
      const auto upload_start = clock::now();
      obj->shader_->load_pixels(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0),
                                GST_VIDEO_FRAME_PLANE_DATA(&frame, 1),
                                GST_VIDEO_FRAME_COMP_PSTRIDE(&frame, 0),
                                GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0),
                                GST_VIDEO_FRAME_COMP_PSTRIDE(&frame, 1),
                                GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 1));
      const auto upload_end = clock::now();
      gst_video_frame_unmap(&frame);

      // Render NV12→RGBA into the render target
      glBindFramebuffer(GL_FRAMEBUFFER, obj->shader_->render_target());
      obj->shader_->draw_core();

      // When double-buffered, blit back→front to avoid tearing
      if (obj->shader_->double_buffer) {
        obj->shader_->blit_to_front();
      }
      const auto render_end = clock::now();

      obj->stats_.upload_ns_total.fetch_add(
          static_cast<uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(upload_end -
                                                                   upload_start)
                  .count()),
          std::memory_order_relaxed);
      obj->stats_.render_ns_total.fetch_add(
          static_cast<uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(render_end -
                                                                   upload_end)
                  .count()),
          std::memory_order_relaxed);
    };

    if (obj->use_legacy_context_) {
      // Legacy: serialize all players through the global mutex and round-trip
      // the engine's texture context on every frame. Other plugins share
      // this context, so the VAO rebind is load-bearing.
      std::lock_guard ctx_lock(g_texture_context_mutex);
      obj->m_registrar->texture_registrar()->TextureMakeCurrent();
      glBindVertexArray(obj->shader_->vertex_arr_id_);
      gl_work();
      obj->m_registrar->texture_registrar()->TextureClearCurrent();
    } else {
      // New: bind our shared context on this streaming thread for the
      // duration of the frame's GL work, then release it before returning.
      //
      // The "keep-current-across-frames" optimization the plan describes
      // only works when the handoff thread never changes. In practice the
      // GStreamer pipeline recycles streaming threads on state transitions
      // (PAUSED→PLAYING preroll, flush-seek, buffering), and an EGL
      // context can only be current on one thread at a time — calling
      // eglMakeCurrent on thread B while it's still current on thread A
      // returns EGL_BAD_ACCESS (0x3002). Releasing after each frame lets
      // the next frame bind cleanly on whichever thread shows up.
      obj->MakeContextCurrent();
      gl_work();
      glFlush();
      eglMakeCurrent(obj->egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                     EGL_NO_CONTEXT);
    }

    obj->stats_.frames_rendered.fetch_add(1, std::memory_order_relaxed);

    // Send initialized event after first frame so the Dart Texture widget
    // doesn't display stale GL content before real video arrives.
    if (!obj->sent_initialized_) {
      obj->sent_initialized_ = true;
      obj->SendInitialized();
    }

    obj->m_registrar->texture_registrar()->MarkTextureFrameAvailable(
        obj->m_texture_id);
    SPDLOG_TRACE("[VideoPlayer] frame");
  } else {
    obj->stats_.frames_dropped.fetch_add(1, std::memory_order_relaxed);
    SPDLOG_ERROR("[VideoPlayer] Cannot read video frame out from buffer");
  }
}

gboolean VideoPlayer::OnBusMessage(GstBus* /* bus */,
                                   GstMessage* msg,
                                   void* user_data) {
  auto obj = static_cast<VideoPlayer*>(user_data);
  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR:
      obj->OnMediaError(msg);
      return FALSE;
    case GST_MESSAGE_EOS:
    case GST_MESSAGE_SEGMENT_DONE: {
      SPDLOG_DEBUG(
          "[VideoPlayer] {}: texture_id: {}",
          (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS ? "EOS" : "Segment done"),
          obj->m_texture_id);
      obj->OnPlaybackEnded();
      break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state, pending_state;
      gst_message_parse_state_changed(msg, &old_state, &new_state,
                                      &pending_state);
      if (GST_MESSAGE_SRC(msg) == GST_OBJECT(obj->playbin_)) {
        obj->OnMediaStateChange(new_state);
      }
      break;
    }
    case GST_MESSAGE_DURATION_CHANGED: {
      obj->OnMediaDurationChange();
      break;
    }
#if 0
    case GST_MESSAGE_LATENCY: {
      auto src = GST_MESSAGE_SRC(msg);
      SPDLOG_DEBUG("[VideoPlayer] Latency: {}", src->name);
      GstQuery* query;
      gboolean res;
      query = gst_query_new_latency();
      res = gst_element_query(obj->playbin_, query);
      if (res) {
        GstClockTime min_latency;
        GstClockTime max_latency;
        gst_query_parse_latency(query, &obj->is_live_, &min_latency,
                                &max_latency);
      }
      gst_query_unref(query);
      break;
    }
#endif
    case GST_MESSAGE_WARNING: {
      GError* warn_err = nullptr;
      gchar* warn_debug = nullptr;
      gst_message_parse_warning(msg, &warn_err, &warn_debug);
      spdlog::warn("[VideoPlayer] Warning: {}:{} debug={}",
                   GST_OBJECT_NAME(msg->src), warn_err ? warn_err->message : "",
                   warn_debug ? warn_debug : "");
      g_clear_error(&warn_err);
      g_free(warn_debug);
      break;
    }
    case GST_MESSAGE_ASYNC_DONE: {
      SPDLOG_DEBUG("[VideoPlayer] Async Done");
      // bufferingEnd
      break;
    }
    case GST_MESSAGE_NEW_CLOCK: {
      GstClock* clock;
      gst_message_parse_new_clock(msg, &clock);
      const GstClockTime time = gst_clock_get_time(clock);
      (void)time;
      SPDLOG_DEBUG("[VideoPlayer] New Clock: {}", time);
      break;
    }
    case GST_MESSAGE_BUFFERING: {
      // Live pipelines never buffer.
      if (obj->is_live_)
        break;

      gint percent;
      gst_message_parse_buffering(msg, &percent);
      SPDLOG_DEBUG("[VideoPlayer] Buffering: {}% texture_id: {}", percent,
                   obj->m_texture_id);

      obj->SendBufferingUpdate();

      // Always surface the bufferingStart/End edge to Dart via the
      // is_buffering_ flag so UI can reflect it. Use atomic::exchange
      // for a single-shot edge under concurrent bus dispatch.
      if (percent == 100) {
        if (obj->is_buffering_.exchange(false)) {
          obj->SetBuffering(false);
        }
      } else {
        if (!obj->is_buffering_.exchange(true)) {
          obj->SetBuffering(true);
        }
      }

      // Force pipeline state transitions ONLY during the cold-start
      // buffering ramp, before the pipeline has ever reached PLAYING.
      //
      // After first-play, playbin manages mid-stream buffering
      // internally — fakesink with sync=TRUE naturally stalls on PTS
      // when the decoder runs dry, so an extra forced PAUSED from us
      // is redundant. It also actively breaks short HTTP streams:
      // playbin's queue2 drops below low-percent immediately after EOS
      // as the decoder drains it, which would thrash us back to PAUSED
      // every frame. Gating on ever_played_ contains our state
      // interference to the initial fill where it actually matters.
      if (obj->ever_played_) {
        break;
      }
      if (percent == 100) {
        if (obj->target_state_ == GST_STATE_PLAYING) {
          gst_element_set_state(obj->playbin_, GST_STATE_PLAYING);
        }
      } else if (obj->target_state_ == GST_STATE_PLAYING) {
        // Pause only on the first sub-100% of the cold-start fill, not
        // on every subsequent oscillation message from multiple
        // internal queue2 instances.
        GstState cur = GST_STATE_VOID_PENDING;
        gst_element_get_state(obj->playbin_, &cur, nullptr, 0);
        if (cur == GST_STATE_PLAYING) {
          gst_element_set_state(obj->playbin_, GST_STATE_PAUSED);
        }
      }
      break;
    }
#if GSTREAMER_DEBUG
    case GST_MESSAGE_STREAM_STATUS: {
      GstStreamStatusType type;
      GstElement* owner;
      const GValue* val;
      gchar* path;
      GstTask* task = nullptr;

      SPDLOG_DEBUG("STREAM_STATUS:");
      gst_message_parse_stream_status(msg, &type, &owner);

      val = gst_message_get_stream_status_object(msg);

      SPDLOG_DEBUG("\ttype:   {}", static_cast<guint>(type));
      path = gst_object_get_path_string(GST_MESSAGE_SRC(msg));
      SPDLOG_DEBUG("\tsource: {}", path);
      g_free(path);
      path = gst_object_get_path_string(GST_OBJECT(owner));
      SPDLOG_DEBUG("\towner:  {}", path);
      g_free(path);
      SPDLOG_DEBUG("\tobject: type {}, value {}", G_VALUE_TYPE_NAME(val),
                   g_value_get_object(val));

      /* see if we know how to deal with this object */
      if (G_VALUE_TYPE(val) == GST_TYPE_TASK) {
        task = GST_TASK(g_value_get_object(val));
      }

      switch (type) {
        case GST_STREAM_STATUS_TYPE_CREATE:
          SPDLOG_DEBUG("Created task: {}", fmt::ptr(task));
          break;
        case GST_STREAM_STATUS_TYPE_ENTER:
          SPDLOG_DEBUG("raising task priority");
          // setpriority (PRIO_PROCESS, 0, -10);
          break;
        case GST_STREAM_STATUS_TYPE_LEAVE:
        default:
          break;
      }
      break;
    }
    case GST_MESSAGE_RESET_TIME: {
      GstClockTime running_time;
      gst_message_parse_reset_time(msg, &running_time);
      if (running_time > 0) {
        g_print("reset-time: %" GST_TIME_FORMAT, GST_TIME_ARGS(running_time));
      }
      break;
    }
    // element specific message
    case GST_MESSAGE_ELEMENT: {
      SPDLOG_DEBUG("message-element: {}",
                   gst_structure_get_name(gst_message_get_structure(msg)));
      break;
    }
#endif
    case GST_MESSAGE_TAG: {
      GstTagList* tags = nullptr;
      gst_message_parse_tag(msg, &tags);
      if (tags) {
        // Embedded album art (front cover preferred, then preview).
        GstSample* image_sample = nullptr;
        if (gst_tag_list_get_sample(tags, GST_TAG_IMAGE, &image_sample) ||
            gst_tag_list_get_sample(tags, GST_TAG_PREVIEW_IMAGE,
                                    &image_sample)) {
          obj->HandleAlbumArt(image_sample);
          gst_sample_unref(image_sample);
        }

        // Update text metadata if present and re-emit.
        bool changed = false;
        gchar* val = nullptr;
        if (gst_tag_list_get_string(tags, GST_TAG_TITLE, &val) && val) {
          obj->title_ = val;
          g_free(val);
          val = nullptr;
          changed = true;
        }
        if (gst_tag_list_get_string(tags, GST_TAG_ARTIST, &val) && val) {
          obj->artist_ = val;
          g_free(val);
          val = nullptr;
          changed = true;
        }
        if (gst_tag_list_get_string(tags, GST_TAG_ALBUM, &val) && val) {
          obj->album_ = val;
          g_free(val);
          val = nullptr;
          changed = true;
        }
        if (gst_tag_list_get_string(tags, GST_TAG_ALBUM_ARTIST, &val) && val) {
          obj->album_artist_ = val;
          g_free(val);
          val = nullptr;
          changed = true;
        }
        if (gst_tag_list_get_string(tags, GST_TAG_GENRE, &val) && val) {
          obj->genre_ = val;
          g_free(val);
          val = nullptr;
          changed = true;
        }
        if (gst_tag_list_get_string(tags, GST_TAG_AUDIO_CODEC, &val) && val) {
          obj->audio_codec_ = val;
          g_free(val);
          val = nullptr;
          changed = true;
        }
        guint track_num = 0;
        if (gst_tag_list_get_uint(tags, GST_TAG_TRACK_NUMBER, &track_num)) {
          obj->track_number_ = static_cast<int>(track_num);
          changed = true;
        }
        if (changed) {
          obj->SendMediaMetadata();
        }
        gst_tag_list_unref(tags);
      }
      break;
    }
    default:
#if GSTREAMER_DEBUG
    {
      SPDLOG_DEBUG("GST Message Type: {}",
                   gst_message_type_get_name(GST_MESSAGE_TYPE(msg)));
      break;
    }
#else
        ;
#endif
  }
  return TRUE;
}

void VideoPlayer::prepare(VideoPlayer* user_data) {
  SPDLOG_DEBUG("[VideoPlayer] prepare");
  g_object_get(user_data->playbin_, "n-video", &user_data->n_video_, nullptr);
  SPDLOG_DEBUG("[VideoPlayer] {} video streams", user_data->n_video_);
  g_object_get(user_data->playbin_, "current-video", &user_data->current_video_,
               nullptr);
  GstPad* pad = nullptr;
  g_signal_emit_by_name(user_data->playbin_, "get-video-pad",
                        user_data->current_video_, &pad);
  if (!pad) {
    SPDLOG_ERROR(
        "[VideoPlayer] Failed to get video pad, stream number might not exist");
    // TODO g_main_loop_quit(obj->main_loop_);
    return;
  }
  GstCaps* caps = gst_pad_get_current_caps(pad);
  if (!caps) {
    SPDLOG_ERROR("[VideoPlayer] Failed to get caps from video pad");
    gst_object_unref(pad);
    return;
  }
  std::lock_guard lock(user_data->gst_mutex_);
  if (!gst_video_info_from_caps(&user_data->info_, caps)) {
    SPDLOG_ERROR("[VideoPlayer] Fail to get video info from the cap");
  }
  gst_caps_unref(caps);
  SPDLOG_DEBUG("[VideoPlayer] original video width: {}, height: {}",
               user_data->info_.width, user_data->info_.height);

  // Capture the source colorimetry before set_format clobbers it with the
  // generic NV12 default (BT.601 limited). HD is typically BT.709 limited,
  // UHD/HDR is BT.2020. Picking the right matrix stops R/B from being
  // subtly wrong on modern content.
  const GstVideoColorimetry src_colorimetry = user_data->info_.colorimetry;
  if (user_data->shader_) {
    nv12::ColorSpace cs = nv12::ColorSpace::kBt709Limited;
    const bool full = src_colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255;
    switch (src_colorimetry.matrix) {
      case GST_VIDEO_COLOR_MATRIX_BT601:
        cs = full ? nv12::ColorSpace::kBt601Full
                  : nv12::ColorSpace::kBt601Limited;
        break;
      case GST_VIDEO_COLOR_MATRIX_BT2020:
        cs = full ? nv12::ColorSpace::kBt2020Full
                  : nv12::ColorSpace::kBt2020Limited;
        break;
      case GST_VIDEO_COLOR_MATRIX_BT709:
      default:
        cs = full ? nv12::ColorSpace::kBt709Full
                  : nv12::ColorSpace::kBt709Limited;
        break;
    }
    user_data->shader_->SetColorSpace(cs);
    SPDLOG_DEBUG("[VideoPlayer] Colorimetry: matrix={} range={} -> preset={}",
                 static_cast<int>(src_colorimetry.matrix),
                 static_cast<int>(src_colorimetry.range), static_cast<int>(cs));
  }

  // Snapshot format + colorspace strings for the stats channel. The source
  // caps tell us what the decoder produced, not what we render in — after
  // set_format() info_ always reports NV12 so we capture this now.
  {
    std::lock_guard meta_lock(user_data->stats_.meta_mutex);
    const auto* finfo = user_data->info_.finfo;
    user_data->stats_.negotiated_format =
        finfo ? GST_VIDEO_FORMAT_INFO_NAME(finfo) : "unknown";
    gchar* colorimetry_str = gst_video_colorimetry_to_string(&src_colorimetry);
    user_data->stats_.negotiated_colorspace =
        colorimetry_str ? colorimetry_str : "unknown";
    g_free(colorimetry_str);
  }

  // set to the target
  if (!gst_video_info_set_format(&user_data->info_, GST_VIDEO_FORMAT_NV12,
                                 static_cast<guint>(user_data->width_),
                                 static_cast<guint>(user_data->height_))) {
    SPDLOG_ERROR("[VideoPlayer] Failed to set the video info to target NV12");
  }
}

// ────────────────────────────────────────────────────────────────────────────
// Phase 1 — Foundation: audio sink bin, audio control, source-setup, album art
// ────────────────────────────────────────────────────────────────────────────

GstElement* VideoPlayer::BuildAudioSinkBin() {
  // Pick the real sink. Try the env-var override first, then walk the
  // fallback chain. Each candidate is taken to READY before being accepted
  // — autoaudiosink instantiates fine even when no real device is
  // available, but later fails caps negotiation with a confusing
  // GST_FLOW_NOT_LINKED at the source. Testing READY here surfaces those
  // failures up front.
  GstElement* real_sink = nullptr;
  const char* picked = nullptr;
  std::vector<const char*> candidates;
  if (const char* env = std::getenv("VIDEO_PLAYER_AUDIO_SINK")) {
    candidates.push_back(env);
  }
  candidates.insert(candidates.end(),
                    {"pipewiresink", "pulsesink", "alsasink", "autoaudiosink"});
  for (const char* name : candidates) {
    GstElement* candidate = gst_element_factory_make(name, "real-audiosink");
    if (!candidate) {
      SPDLOG_DEBUG("[VideoPlayer] audio sink {} unavailable", name);
      continue;
    }
    // Trust pipewiresink and pulsesink — they're reliable on modern Linux
    // and a state-change probe triggers a noisy "thread-loop recurse"
    // warning from PipeWire. Only the unreliable fallbacks
    // (alsasink, autoaudiosink, env-var override) get the READY probe.
    const bool trusted = std::strcmp(name, "pipewiresink") == 0 ||
                         std::strcmp(name, "pulsesink") == 0;
    if (!trusted) {
      GstStateChangeReturn ret =
          gst_element_set_state(candidate, GST_STATE_READY);
      if (ret == GST_STATE_CHANGE_FAILURE) {
        SPDLOG_DEBUG("[VideoPlayer] audio sink {} failed READY", name);
        gst_element_set_state(candidate, GST_STATE_NULL);
        gst_object_unref(candidate);
        continue;
      }
      gst_element_set_state(candidate, GST_STATE_NULL);
    }
    real_sink = candidate;
    picked = name;
    break;
  }
  if (!real_sink) {
    spdlog::error("[VideoPlayer] No working audio sink found");
    return nullptr;
  }
  spdlog::info("[VideoPlayer] Selected audio sink: {}", picked);
  if (const char* dev = std::getenv("VIDEO_PLAYER_AUDIO_DEVICE")) {
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(real_sink), "device")) {
      g_object_set(real_sink, "device", dev, nullptr);
    }
  }

  audio_convert_ = gst_element_factory_make("audioconvert", "audio-convert");
  audio_resample_ = gst_element_factory_make("audioresample", "audio-resample");
  // scaletempo time-stretches audio when the segment rate isn't 1.0 so the
  // pitch stays correct. Without it, rate-change seeks make the audio drop
  // out / pitch-shift. Ships in gst-plugins-good.
  audio_scaletempo_ =
      gst_element_factory_make("scaletempo", "audio-scaletempo");
  audio_capsfilter_ = gst_element_factory_make("capsfilter", "audio-caps");
  if (!audio_convert_ || !audio_resample_ || !audio_capsfilter_) {
    spdlog::error("[VideoPlayer] Failed to create audio sink bin elements");
    if (audio_convert_)
      gst_object_unref(audio_convert_);
    if (audio_resample_)
      gst_object_unref(audio_resample_);
    if (audio_scaletempo_)
      gst_object_unref(audio_scaletempo_);
    if (audio_capsfilter_)
      gst_object_unref(audio_capsfilter_);
    audio_convert_ = audio_resample_ = audio_capsfilter_ = nullptr;
    audio_scaletempo_ = nullptr;
    gst_object_unref(real_sink);
    return nullptr;
  }
  if (!audio_scaletempo_) {
    spdlog::warn(
        "[VideoPlayer] scaletempo unavailable; playback rate changes will "
        "pitch-shift audio. Install gst-plugins-good.");
  }

  // Output channel count from env var (Phase 1 default 2).
  output_channels_ = 2;
  if (const char* env = std::getenv("VIDEO_PLAYER_AUDIO_CHANNELS")) {
    char* end = nullptr;
    long val = std::strtol(env, &end, 10);
    if (end != env && val >= 1 && val <= 8) {
      output_channels_ = static_cast<int>(val);
    }
  }
  GstCaps* caps = gst_caps_new_simple("audio/x-raw", "channels", G_TYPE_INT,
                                      output_channels_, nullptr);
  g_object_set(audio_capsfilter_, "caps", caps, nullptr);
  gst_caps_unref(caps);

  // Channel reordering defaults — fixes media with broken position metadata.
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(audio_convert_),
                                   "input-channels-reorder-mode")) {
    int reorder_mode = 0;
    int reorder_layout = 1;  // SMPTE
    if (const char* env = std::getenv("VIDEO_PLAYER_AUDIO_REORDER_MODE")) {
      char* end = nullptr;
      const long v = std::strtol(env, &end, 10);
      if (end != env && v >= 0 && v <= 2)
        reorder_mode = static_cast<int>(v);
    }
    if (const char* env = std::getenv("VIDEO_PLAYER_AUDIO_REORDER")) {
      char* end = nullptr;
      const long v = std::strtol(env, &end, 10);
      if (end != env && v >= 0 && v <= 4)
        reorder_layout = static_cast<int>(v);
    }
    g_object_set(audio_convert_, "input-channels-reorder-mode", reorder_mode,
                 "input-channels-reorder", reorder_layout, nullptr);
  }

  GstElement* bin = gst_bin_new("audio-sink-bin");
  gst_bin_add(GST_BIN(bin), audio_convert_);
  gst_bin_add(GST_BIN(bin), audio_resample_);
  if (audio_scaletempo_)
    gst_bin_add(GST_BIN(bin), audio_scaletempo_);
  gst_bin_add(GST_BIN(bin), audio_capsfilter_);
  gst_bin_add(GST_BIN(bin), real_sink);

  // Chain: convert → resample → [scaletempo] → capsfilter → real sink
  bool linked;
  if (audio_scaletempo_) {
    linked = gst_element_link_many(audio_convert_, audio_resample_,
                                   audio_scaletempo_, audio_capsfilter_,
                                   real_sink, nullptr);
  } else {
    linked = gst_element_link_many(audio_convert_, audio_resample_,
                                   audio_capsfilter_, real_sink, nullptr);
  }
  if (!linked) {
    spdlog::error("[VideoPlayer] Failed to link audio sink bin");
    gst_object_unref(bin);
    audio_convert_ = audio_resample_ = audio_capsfilter_ = nullptr;
    audio_scaletempo_ = nullptr;
    return nullptr;
  }

  // Add the ghost sink pad. We deliberately do NOT call
  // gst_pad_set_active() here — pad activation is the responsibility of
  // the parent's state change logic, and forcing it before the bin has a
  // parent has been observed to break uridecodebin auto-plugging on
  // audio-only sources (souphttpsrc returns GST_FLOW_NOT_LINKED).
  GstPad* pad = gst_element_get_static_pad(audio_convert_, "sink");
  GstPad* ghost = gst_ghost_pad_new("sink", pad);
  gst_element_add_pad(bin, ghost);
  gst_object_unref(pad);

  SPDLOG_DEBUG("[VideoPlayer] Built audio sink bin (sink={}, channels={})",
               picked, output_channels_);
  return bin;
}

void VideoPlayer::OnSourceSetup(GstElement* /*playbin*/,
                                GstElement* source,
                                gpointer /*user_data*/) {
  if (!source)
    return;
  GObjectClass* klass = G_OBJECT_GET_CLASS(source);

  // HTTP timeout (souphttpsrc): default 30s prevents indefinite hangs.
  if (g_object_class_find_property(klass, "timeout")) {
    guint timeout = 30;
    if (const char* env = std::getenv("VIDEO_PLAYER_HTTP_TIMEOUT")) {
      char* end = nullptr;
      const long val = std::strtol(env, &end, 10);
      if (end != env && val >= 0 && val < 3600) {
        timeout = static_cast<guint>(val);
      }
    }
    g_object_set(source, "timeout", timeout, nullptr);
    SPDLOG_DEBUG("[VideoPlayer] source timeout={}s", timeout);
  }
  if (g_object_class_find_property(klass, "user-agent")) {
    if (const char* ua = std::getenv("VIDEO_PLAYER_USER_AGENT")) {
      g_object_set(source, "user-agent", ua, nullptr);
    }
  }
  if (g_object_class_find_property(klass, "proxy")) {
    if (const char* proxy = std::getenv("VIDEO_PLAYER_PROXY")) {
      g_object_set(source, "proxy", proxy, nullptr);
    }
  }
  // RTSP latency (rtspsrc) — IVI camera streams typically want low latency.
  if (g_object_class_find_property(klass, "latency")) {
    guint latency = 200;
    if (const char* env = std::getenv("VIDEO_PLAYER_RTSP_LATENCY")) {
      char* end = nullptr;
      const long val = std::strtol(env, &end, 10);
      if (end != env && val >= 0 && val < 60000) {
        latency = static_cast<guint>(val);
      }
    }
    g_object_set(source, "latency", latency, nullptr);
  }
}

void VideoPlayer::HandleAlbumArt(GstSample* sample) {
  if (!sample)
    return;
  GstBuffer* buffer = gst_sample_get_buffer(sample);
  GstCaps* caps = gst_sample_get_caps(sample);
  if (!buffer || !caps)
    return;

  const GstStructure* s = gst_caps_get_structure(caps, 0);
  const gchar* mime = s ? gst_structure_get_name(s) : "image/jpeg";

  // Filter to front cover (or undefined, which is typically the cover).
  const GstStructure* sample_info = gst_sample_get_info(sample);
  GstTagImageType image_type = GST_TAG_IMAGE_TYPE_UNDEFINED;
  if (sample_info) {
    gst_structure_get_enum(sample_info, "image-type", GST_TYPE_TAG_IMAGE_TYPE,
                           reinterpret_cast<gint*>(&image_type));
  }
  if (image_type != GST_TAG_IMAGE_TYPE_FRONT_COVER &&
      image_type != GST_TAG_IMAGE_TYPE_UNDEFINED) {
    return;
  }

  GstMapInfo map;
  if (!gst_buffer_map(buffer, &map, GST_MAP_READ))
    return;
  // 10 MB cap (see discover_media_info for rationale).
  constexpr size_t kMaxAlbumArtBytes = static_cast<size_t>(10) * 1024 * 1024;
  if (map.size == 0 || map.size > kMaxAlbumArtBytes) {
    if (map.size > kMaxAlbumArtBytes) {
      spdlog::warn(
          "[VideoPlayer] Runtime album art is {} bytes (>{} MB cap); ignoring.",
          map.size, kMaxAlbumArtBytes / (static_cast<size_t>(1024) * 1024));
    }
    gst_buffer_unmap(buffer, &map);
    return;
  }
  std::vector<uint8_t> bytes(map.data, map.data + map.size);
  gst_buffer_unmap(buffer, &map);

  SendAlbumArt(bytes, mime ? mime : "image/jpeg");
}

void VideoPlayer::SendAlbumArt(const std::vector<uint8_t>& bytes,
                               const std::string& mime) {
  std::lock_guard event_lock(event_mutex_);
  if (!event_sink_)
    return;
  auto map = flutter::EncodableMap{
      {flutter::EncodableValue("event"), flutter::EncodableValue("albumArt")},
      {flutter::EncodableValue("mimeType"), flutter::EncodableValue(mime)},
      {flutter::EncodableValue("data"), flutter::EncodableValue(bytes)},
  };
  event_sink_->Success(flutter::EncodableValue(
      std::in_place_type<flutter::EncodableMap>, std::move(map)));
}

void VideoPlayer::SendMediaMetadata() {
  std::lock_guard event_lock(event_mutex_);
  if (!event_sink_)
    return;
  auto map = flutter::EncodableMap{
      {flutter::EncodableValue("event"),
       flutter::EncodableValue("mediaMetadata")},
      {flutter::EncodableValue("title"), flutter::EncodableValue(title_)},
      {flutter::EncodableValue("artist"), flutter::EncodableValue(artist_)},
      {flutter::EncodableValue("album"), flutter::EncodableValue(album_)},
      {flutter::EncodableValue("albumArtist"),
       flutter::EncodableValue(album_artist_)},
      {flutter::EncodableValue("genre"), flutter::EncodableValue(genre_)},
      {flutter::EncodableValue("trackNumber"),
       flutter::EncodableValue(track_number_)},
  };
  event_sink_->Success(flutter::EncodableValue(
      std::in_place_type<flutter::EncodableMap>, std::move(map)));
}

void VideoPlayer::SendAudioInfo() {
  std::lock_guard event_lock(event_mutex_);
  if (!event_sink_)
    return;
  auto map = flutter::EncodableMap{
      {flutter::EncodableValue("event"), flutter::EncodableValue("audioInfo")},
      {flutter::EncodableValue("codec"), flutter::EncodableValue(audio_codec_)},
      {flutter::EncodableValue("channels"),
       flutter::EncodableValue(audio_channels_)},
      {flutter::EncodableValue("sampleRate"),
       flutter::EncodableValue(audio_sample_rate_)},
  };
  event_sink_->Success(flutter::EncodableValue(
      std::in_place_type<flutter::EncodableMap>, std::move(map)));
}

int VideoPlayer::GetAudioTrackCount() {
  if (!playbin_)
    return 0;
  gint n = 0;
  g_object_get(playbin_, "n-audio", &n, nullptr);
  return n;
}

void VideoPlayer::SetAudioTrack(int index) {
  if (!playbin_)
    return;
  gint n = 0;
  g_object_get(playbin_, "n-audio", &n, nullptr);
  if (index < 0 || index >= n) {
    spdlog::warn("[VideoPlayer] SetAudioTrack({}) out of range (n={})", index,
                 n);
    return;
  }
  g_object_set(playbin_, "current-audio", index, nullptr);
}

void VideoPlayer::SetOutputChannels(int channels) {
  if (channels < 1 || channels > 8) {
    spdlog::warn("[VideoPlayer] SetOutputChannels({}) out of range", channels);
    return;
  }
  output_channels_ = channels;
  if (!audio_capsfilter_)
    return;
  GstCaps* caps = gst_caps_new_simple("audio/x-raw", "channels", G_TYPE_INT,
                                      channels, nullptr);
  g_object_set(audio_capsfilter_, "caps", caps, nullptr);
  gst_caps_unref(caps);
  // The capsfilter change requires a quick READY round-trip on the audio
  // chain to take effect mid-stream. Acceptable known dropout. The
  // round-trip also resets segment rate, so invalidate the cache.
  if (playbin_) {
    GstState cur = GST_STATE_NULL;
    gst_element_get_state(playbin_, &cur, nullptr, 0);
    rate_.store(-2.0);
    gst_element_set_state(playbin_, GST_STATE_READY);
    gst_element_set_state(playbin_, cur);
  }
}

void VideoPlayer::SetMute(bool mute) {
  if (!playbin_)
    return;
  g_object_set(playbin_, "mute", mute ? TRUE : FALSE, nullptr);
}

// ────────────────────────────────────────────────────────────────────────────
// Phase 2 — Quality & Tuning: scale, A/V offset, subtitles, channel presets
// ────────────────────────────────────────────────────────────────────────────

void VideoPlayer::SetScaleMethod(int method) {
  if (!has_video_ || !video_scale_) {
    return;
  }
  if (method < 0 || method > 9) {
    spdlog::warn("[VideoPlayer] SetScaleMethod({}) out of range", method);
    return;
  }
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(video_scale_),
                                   "method")) {
    g_object_set(video_scale_, "method", method, nullptr);
  }
}

void VideoPlayer::SetAVOffset(int64_t offset_ms) {
  if (!playbin_)
    return;
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(playbin_), "av-offset")) {
    g_object_set(playbin_, "av-offset",
                 static_cast<gint64>(offset_ms) * GST_MSECOND, nullptr);
  }
}

void VideoPlayer::SetSubtitlesEnabled(bool enabled) {
  if (!playbin_)
    return;
  gint flags = 0;
  g_object_get(playbin_, "flags", &flags, nullptr);
  if (enabled) {
    flags |= GST_PLAY_FLAG_TEXT;
  } else {
    flags &= ~GST_PLAY_FLAG_TEXT;
  }
  g_object_set(playbin_, "flags", flags, nullptr);
}

int VideoPlayer::GetSubtitleTrackCount() {
  if (!playbin_)
    return 0;
  gint n = 0;
  g_object_get(playbin_, "n-text", &n, nullptr);
  return n;
}

void VideoPlayer::SetSubtitleTrack(int index) {
  if (!playbin_)
    return;
  gint n = 0;
  g_object_get(playbin_, "n-text", &n, nullptr);
  if (index < 0 || index >= n) {
    spdlog::warn("[VideoPlayer] SetSubtitleTrack({}) out of range (n={})",
                 index, n);
    return;
  }
  g_object_set(playbin_, "current-text", index, nullptr);
}

void VideoPlayer::SetSubtitleUri(const std::string& uri) {
  if (!playbin_)
    return;
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(playbin_), "suburi")) {
    g_object_set(playbin_, "suburi", uri.empty() ? nullptr : uri.c_str(),
                 nullptr);
  }
}

void VideoPlayer::SetSubtitleFont(const std::string& font_desc) {
  if (!playbin_)
    return;
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(playbin_),
                                   "subtitle-font-desc")) {
    g_object_set(playbin_, "subtitle-font-desc", font_desc.c_str(), nullptr);
  }
}

// Query the negotiated input channel count on audio_convert_'s sink pad.
// Returns 0 if the pad has no current caps (pipeline not yet rolling).
static int QueryInputChannels(GstElement* audio_convert) {
  if (!audio_convert)
    return 0;
  GstPad* sink = gst_element_get_static_pad(audio_convert, "sink");
  if (!sink)
    return 0;
  int channels = 0;
  if (GstCaps* caps = gst_pad_get_current_caps(sink)) {
    if (gst_caps_get_size(caps) > 0) {
      const GstStructure* s = gst_caps_get_structure(caps, 0);
      gst_structure_get_int(s, "channels", &channels);
    }
    gst_caps_unref(caps);
  }
  gst_object_unref(sink);
  return channels;
}

void VideoPlayer::SetChannelMixPreset(const std::string& preset) {
  if (!audio_convert_) {
    spdlog::warn("[VideoPlayer] SetChannelMixPreset: audio bin not built");
    return;
  }
  if (!g_object_class_find_property(G_OBJECT_GET_CLASS(audio_convert_),
                                    "mix-matrix")) {
    spdlog::warn(
        "[VideoPlayer] audioconvert mix-matrix unsupported (GStreamer < 1.20)");
    return;
  }

  // The mix-matrix property must match the actual input channel count
  // (cols) × output channel count (rows) of the negotiated caps. If the
  // source isn't 5.1, the surround presets are meaningless — fall back to
  // a plain capsfilter-driven downmix and let audioconvert pick stock
  // coefficients.
  const int in_ch = QueryInputChannels(audio_convert_);
  if (in_ch != 6) {
    spdlog::debug(
        "[VideoPlayer] SetChannelMixPreset('{}'): input has {} channels "
        "(need 6 for the matrix presets); applying capsfilter only.",
        preset, in_ch);
    if (preset == "surround") {
      SetOutputChannels(in_ch > 0 ? in_ch : 6);
    } else {
      SetOutputChannels(2);
    }
    return;
  }

  // Standard 5.1 input order (SMPTE): FL, FR, FC, LFE, RL, RR
  // Each preset defines two output rows (stereo) × six input columns.
  // Coefficients are applied as a flat row-major GValueArray of GValueArrays.
  struct Preset {
    const char* name;
    int out_channels;
    double matrix[2][6];
  };
  static constexpr Preset kPresets[] = {
      // ITU-R BS.775 standard stereo downmix.
      {"stereo",
       2,
       {{1.0, 0.0, 0.707, 0.707, 0.707, 0.0},
        {0.0, 1.0, 0.707, 0.707, 0.0, 0.707}}},
      // Boosted dialog (FC) for the driver, modest LFE bleed.
      {"driver",
       2,
       {{1.0, 0.0, 0.85, 0.5, 0.5, 0.0}, {0.0, 1.0, 0.85, 0.5, 0.0, 0.5}}},
      // Reduced dynamic range for night listening.
      {"night",
       2,
       {{1.0, 0.0, 0.707, 0.1, 0.5, 0.0}, {0.0, 1.0, 0.707, 0.1, 0.0, 0.5}}},
      // Rear-cabin optimised — emphasise rear channels.
      {"rear",
       2,
       {{1.0, 0.0, 0.5, 0.3, 1.0, 0.0}, {0.0, 1.0, 0.5, 0.3, 0.0, 1.0}}},
  };

  if (preset == "surround") {
    // Identity-ish: clear matrix, force 5.1 output.
    GValue empty = G_VALUE_INIT;
    g_value_init(&empty, GST_TYPE_ARRAY);
    g_object_set_property(G_OBJECT(audio_convert_), "mix-matrix", &empty);
    g_value_unset(&empty);
    SetOutputChannels(6);
    return;
  }

  const Preset* match = nullptr;
  for (const auto& p : kPresets) {
    if (preset == p.name) {
      match = &p;
      break;
    }
  }
  if (!match) {
    spdlog::warn("[VideoPlayer] Unknown channel mix preset '{}'", preset);
    return;
  }

  // Build a GST_TYPE_ARRAY of GST_TYPE_ARRAY of doubles.
  GValue matrix = G_VALUE_INIT;
  g_value_init(&matrix, GST_TYPE_ARRAY);
  for (int row = 0; row < match->out_channels; ++row) {
    GValue gst_row = G_VALUE_INIT;
    g_value_init(&gst_row, GST_TYPE_ARRAY);
    for (int col = 0; col < 6; ++col) {
      GValue cell = G_VALUE_INIT;
      g_value_init(&cell, G_TYPE_DOUBLE);
      g_value_set_double(&cell, match->matrix[row][col]);
      gst_value_array_append_value(&gst_row, &cell);
      g_value_unset(&cell);
    }
    gst_value_array_append_value(&matrix, &gst_row);
    g_value_unset(&gst_row);
  }
  g_object_set_property(G_OBJECT(audio_convert_), "mix-matrix", &matrix);
  g_value_unset(&matrix);

  SetOutputChannels(match->out_channels);
  SPDLOG_DEBUG("[VideoPlayer] Applied channel mix preset '{}'", preset);
}

// ────────────────────────────────────────────────────────────────────────────
// Phase 3 — Premium features: equalizer, video balance, passthrough, custom
// downmix matrix
// ────────────────────────────────────────────────────────────────────────────

void VideoPlayer::SetEqualizer(const std::vector<double>& bands) {
  if (bands.size() != 10) {
    spdlog::warn("[VideoPlayer] SetEqualizer: expected 10 bands, got {}",
                 bands.size());
    return;
  }
  // Lazily insert equalizer-10bands into the audio sink bin on first
  // call. The current chain is:
  //   convert → resample → [scaletempo] → capsfilter → real_sink
  // We splice the EQ between scaletempo (or resample, when scaletempo
  // wasn't available) and capsfilter so it operates on the time-stretched
  // PCM that the user actually hears. Inserted in READY with a brief
  // audio dropout.
  if (!equalizer_) {
    if (!audio_bin_ || !audio_capsfilter_) {
      spdlog::warn("[VideoPlayer] SetEqualizer: audio bin not available");
      return;
    }
    GstElement* upstream =
        audio_scaletempo_ ? audio_scaletempo_ : audio_resample_;
    if (!upstream) {
      spdlog::warn("[VideoPlayer] SetEqualizer: upstream element missing");
      return;
    }
    GstElement* eq = gst_element_factory_make("equalizer-10bands", "audio-eq");
    if (!eq) {
      spdlog::warn(
          "[VideoPlayer] equalizer-10bands element unavailable; install "
          "gst-plugins-good");
      return;
    }
    GstState cur = GST_STATE_NULL;
    if (playbin_)
      gst_element_get_state(playbin_, &cur, nullptr, 0);
    // READY round-trip resets segment rate; invalidate so the next
    // PLAYING re-applies pending_rate_.
    rate_.store(-2.0);
    if (playbin_)
      gst_element_set_state(playbin_, GST_STATE_READY);

    gst_element_unlink(upstream, audio_capsfilter_);
    gst_bin_add(GST_BIN(audio_bin_), eq);
    if (!gst_element_link_many(upstream, eq, audio_capsfilter_, nullptr)) {
      spdlog::error("[VideoPlayer] Failed to splice equalizer into audio bin");
      gst_element_unlink(upstream, eq);
      gst_element_unlink(eq, audio_capsfilter_);
      gst_bin_remove(GST_BIN(audio_bin_), eq);
      // Restore the original direct link.
      gst_element_link(upstream, audio_capsfilter_);
      if (playbin_)
        gst_element_set_state(playbin_, cur);
      return;
    }
    gst_element_sync_state_with_parent(eq);
    equalizer_ = eq;
    if (playbin_)
      gst_element_set_state(playbin_, cur);
  }

  for (size_t i = 0; i < bands.size(); ++i) {
    char prop[16];
    // Buffer is 16 bytes, output is "band0".."band9" (max 6 bytes), so
    // truncation is impossible. Cast to void to silence cert-err33-c.
    (void)std::snprintf(prop, sizeof(prop), "band%d", static_cast<int>(i));
    double v = bands[i];
    if (v < -24.0)
      v = -24.0;
    if (v > 12.0)
      v = 12.0;
    g_object_set(equalizer_, prop, v, nullptr);
  }
}

void VideoPlayer::SetVideoBalance(double brightness,
                                  double contrast,
                                  double saturation,
                                  double hue) {
  if (!has_video_) {
    return;
  }
  // Lazily insert videobalance between videoconvert and videoscale on
  // the first non-default call.
  const bool is_default =
      brightness == 0.0 && contrast == 1.0 && saturation == 1.0 && hue == 0.0;
  if (!videobalance_) {
    if (is_default || !pipeline_ || !video_convert_ || !video_scale_) {
      return;
    }
    GstElement* vb = gst_element_factory_make("videobalance", "video-balance");
    if (!vb) {
      spdlog::warn("[VideoPlayer] videobalance element unavailable");
      return;
    }
    GstState cur = GST_STATE_NULL;
    if (playbin_)
      gst_element_get_state(playbin_, &cur, nullptr, 0);
    // READY round-trip resets segment rate; invalidate so the next
    // PLAYING re-applies pending_rate_.
    rate_.store(-2.0);
    if (playbin_)
      gst_element_set_state(playbin_, GST_STATE_READY);

    gst_element_unlink(video_convert_, video_scale_);
    gst_bin_add(GST_BIN(pipeline_), vb);
    // Preserve the NV12 caps the original chain depended on so the
    // downstream fakesink's handoff_handler still receives NV12 frames.
    GstCaps* nv12 = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
                                        "NV12", nullptr);
    const bool ok = gst_element_link(video_convert_, vb) &&
                    gst_element_link_filtered(vb, video_scale_, nv12);
    gst_caps_unref(nv12);
    if (!ok) {
      spdlog::error("[VideoPlayer] Failed to splice videobalance");
      gst_element_unlink(video_convert_, vb);
      gst_element_unlink(vb, video_scale_);
      gst_bin_remove(GST_BIN(pipeline_), vb);
      // Restore the original direct link.
      GstCaps* restore = gst_caps_new_simple("video/x-raw", "format",
                                             G_TYPE_STRING, "NV12", nullptr);
      gst_element_link_filtered(video_convert_, video_scale_, restore);
      gst_caps_unref(restore);
      if (playbin_)
        gst_element_set_state(playbin_, cur);
      return;
    }
    gst_element_sync_state_with_parent(vb);
    videobalance_ = vb;
    if (playbin_)
      gst_element_set_state(playbin_, cur);
  }
  if (videobalance_) {
    g_object_set(videobalance_, "brightness", brightness, "contrast", contrast,
                 "saturation", saturation, "hue", hue, nullptr);
  }
}

void VideoPlayer::SetAudioPassthrough(bool enabled) {
  // Passthrough requires the playbin to forward encoded audio to a sink that
  // accepts encoded caps. autoaudiosink does not. The plugin can't safely
  // hot-swap the sink mid-stream, so this method updates a flag that takes
  // effect on the next media load. Document the env-var fallback as the
  // recommended deployment configuration.
  if (!playbin_)
    return;
  // The most we can do at runtime: tweak the playbin flags to permit
  // native (non-decoded) audio paths via GST_PLAY_FLAG_NATIVE_AUDIO (1<<5).
  constexpr int kNativeAudio = 1 << 5;
  gint flags = 0;
  g_object_get(playbin_, "flags", &flags, nullptr);
  if (enabled) {
    flags |= kNativeAudio;
  } else {
    flags &= ~kNativeAudio;
  }
  g_object_set(playbin_, "flags", flags, nullptr);
}

void VideoPlayer::SetChannelMixMatrix(int in_channels,
                                      int out_channels,
                                      const std::vector<double>& matrix) {
  if (!audio_convert_) {
    spdlog::warn("[VideoPlayer] SetChannelMixMatrix: audio bin not built");
    return;
  }
  if (in_channels <= 0 || out_channels <= 0 || in_channels > 8 ||
      out_channels > 8) {
    spdlog::warn("[VideoPlayer] SetChannelMixMatrix: invalid dimensions {}x{}",
                 in_channels, out_channels);
    return;
  }
  if (static_cast<int>(matrix.size()) != in_channels * out_channels) {
    spdlog::warn(
        "[VideoPlayer] SetChannelMixMatrix: matrix size mismatch (got {}, "
        "expected {})",
        matrix.size(), in_channels * out_channels);
    return;
  }
  if (!g_object_class_find_property(G_OBJECT_GET_CLASS(audio_convert_),
                                    "mix-matrix")) {
    spdlog::warn("[VideoPlayer] audioconvert mix-matrix unsupported");
    return;
  }

  GValue mat = G_VALUE_INIT;
  g_value_init(&mat, GST_TYPE_ARRAY);
  for (int row = 0; row < out_channels; ++row) {
    GValue gst_row = G_VALUE_INIT;
    g_value_init(&gst_row, GST_TYPE_ARRAY);
    for (int col = 0; col < in_channels; ++col) {
      GValue cell = G_VALUE_INIT;
      g_value_init(&cell, G_TYPE_DOUBLE);
      g_value_set_double(
          &cell,
          matrix[static_cast<size_t>(row) * static_cast<size_t>(in_channels) +
                 static_cast<size_t>(col)]);
      gst_value_array_append_value(&gst_row, &cell);
      g_value_unset(&cell);
    }
    gst_value_array_append_value(&mat, &gst_row);
    g_value_unset(&gst_row);
  }
  g_object_set_property(G_OBJECT(audio_convert_), "mix-matrix", &mat);
  g_value_unset(&mat);
  SetOutputChannels(out_channels);
}

}  // namespace video_player_linux
