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

#pragma once

#include <array>
#include <cstdint>

extern "C" {
#include <glib.h>
}

namespace video_player_linux {

// Generic zero-copy video frame descriptor built from a GstSample whose
// buffer is backed by dmabuf-memory. Phase 1.3 consumes this to build
// an EGLImage via eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, ...). The
// struct is POD-ish on purpose — it's copied cheaply between the
// GStreamer streaming thread and the GL upload path.
//
// NV12 has two planes (Y, UV). A single GstBuffer may hold both planes
// in one GstMemory (single-FD, two offsets — typical for VA-API /
// driver-side allocations) or split across two GstMemory chunks
// (two-FD, one offset each — typical for v4l2 / stateful decoders).
// Both layouts land in this struct with the correct fd/offset/stride
// per plane; the consumer doesn't need to branch on provenance.
//
// Plane FDs are borrowed from gst_dmabuf_memory_get_fd(); the owning
// GstSample must stay alive until EGLImage creation dup()s them. Do
// NOT close() these FDs — they remain owned by the GstMemory.
struct DmabufPlane {
  int fd{-1};
  guint32 offset{0};  // byte offset into fd where this plane starts
  guint32 stride{0};  // row stride in bytes
};

struct DmabufFrame {
  int width{0};
  int height{0};
  std::array<DmabufPlane, 2> planes{};
  unsigned n_planes{0};
  // DRM FourCC of the frame's pixel layout (e.g. NV12 = 'N','V','1','2').
  // Keeps the import attr list clean regardless of which gst format we
  // started from.
  guint32 drm_fourcc{0};
  // DRM modifier. 0 (== DRM_FORMAT_MOD_LINEAR) for ordinary linear
  // buffers; non-linear (tiled, compressed) formats put the vendor
  // modifier here and EGLImage import gets EGL_DMA_BUF_PLANEn_MODIFIER
  // attrs — see Phase 3 for NXP Amphion, Rockchip AFBC, Qualcomm UBWC.
  // 0x00ffffffffffffff is DRM_FORMAT_MOD_INVALID and means "unknown /
  // don't ask" — import without modifier attrs.
  guint64 drm_modifier{0};
};

// Helpers — kept header-inline because they're two lines each and
// avoid pulling drm_fourcc.h into the translation unit.
constexpr guint32 Fourcc(char a, char b, char c, char d) {
  return static_cast<guint32>(a) | (static_cast<guint32>(b) << 8) |
         (static_cast<guint32>(c) << 16) | (static_cast<guint32>(d) << 24);
}
constexpr guint64 kDrmFormatModLinear = 0ULL;
constexpr guint64 kDrmFormatModInvalid = 0x00FFFFFFFFFFFFFFULL;

}  // namespace video_player_linux
