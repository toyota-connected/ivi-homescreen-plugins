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

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include <gst/gst.h>
#include <gst/video/video.h>
}

namespace video_player_linux {

// Platform profile the plugin detected / was asked to behave as. Used
// as a selection hint by the registry and by backend implementations
// that want to branch on SoC family (tiled format choices, VPU bring-up
// quirks, etc.). Kept out of backend_interface's implementation surface,
// so backends don't need to #include the detection header.
enum class PlatformProfile {
  Auto,
  // NXP
  Imx8M,      // i.MX 8M (Quad/Mini/Nano) — Hantro G1
  Imx8MPlus,  // i.MX 8M Plus — Hantro G1+G2, NPU
  Imx8QM,     // i.MX 8 QuadMax — Amphion Malone (tiled output)
  Imx8QXP,    // i.MX 8 QuadXPlus
  Imx95,      // i.MX 95 — newer VPU, AV1
  // Rockchip
  RockchipRK3568,
  RockchipRK3588,  // RK3588 / RK3588s — 8K decode, dual VPU
  // Renesas
  RcarH3,   // Gen3 — VCP4, Mali-G71
  RcarM3,   // Gen3 mid-tier
  RcarV3H,  // Gen3 ADAS
  RcarV4H,  // Gen4 ADAS, newer VCP
  RcarH4,   // Gen4 cockpit
  // Qualcomm Snapdragon Auto
  QualcommSA8155,  // Gen 3 — Venus v4
  QualcommSA8295,  // Gen 4 — Venus v6, AV1
  // MediaTek
  MediatekMT8195,  // Genio 1200 — 4K60 HDR, Mali-G57
  MediatekCTX1,    // Dimensity Auto Cockpit CT-X1
  // Fallbacks
  GenericV4L2,  // any V4L2 M2M codec device
  Software,     // avdec_* / openh264dec / dav1d
};

// Per-decoder knobs the backend may want to honor. Extended over time
// as config.toml grows new settings. All fields have conservative
// defaults, so a backend that ignores the struct behaves sanely.
struct DecoderConfig {
  // Disable frame reordering and emit in decode order. Good for
  // latency-sensitive ADAS/camera use cases, bad for B-frame heavy
  // content (reverses display order).
  bool low_latency{false};

  // Requested size of the decoder's output buffer pool. 0 means
  // "use the backend's default". Tuning this up trades memory for
  // smoother playback under load; tuning down is useful on
  // memory-constrained targets.
  unsigned buffer_count{0};

  // Prefer AFBC (ARM Frame Buffer Compression) output when the
  // decoder can emit it. Only Rockchip MPP and some Mali-paired
  // platforms honor this today; other backends gate on property
  // presence and silently ignore it. Driven from Config's
  // texture_enable_afbc — the compressed buffer is only useful when
  // the display path can sample it, so the gate lives in the
  // texture section of the TOML rather than in [decoder].
  bool enable_afbc{false};
};

// What a backend can do. Filled in by query_capabilities() so the
// registry / selector doesn't have to instantiate pipelines just to
// learn format support. Backends may return a conservative under-
// approximation (e.g. declare only 8-bit support even if the HW can
// do 10-bit) — the registry treats it as ground truth.
struct BackendCapabilities {
  // Short codec names: "h264", "h265", "vp9", "av1", "mjpeg"
  std::vector<std::string> supported_codecs;

  // DRM format modifiers the backend can emit on its src pad.
  // DRM_FORMAT_MOD_LINEAR must be in every list (sink pipeline can
  // always consume linear); tiled / compressed modifiers are
  // platform-specific (AFBC, UBWC, Amphion 4x4 tiled, …).
  std::vector<uint64_t> supported_modifiers;

  // Pixel formats the backend can produce. Empty means "any gst
  // raw format the decoder natively emits"; a non-empty list
  // constrains negotiation.
  std::vector<GstVideoFormat> supported_formats;

  unsigned max_width{0};  // 0 = unbounded / unknown
  unsigned max_height{0};
  bool supports_10bit{false};
  bool supports_hdr{false};   // HDR10/HLG colorimetry + static metadata
  bool supports_afbc{false};  // shorthand for "has an AFBC modifier"
};

// Pluggable video decoder backend. One instance per registered
// backend; the BackendRegistry owns them. Backends are consulted at
// stream creation time (VideoPlayer ctor) to build the decoder
// fragment of the pipeline.
//
// The GstElement pointers returned from build_*_bin() are floating-
// ref'd (gst_element_factory_make semantics). Caller attaches them
// into the parent bin — after gst_bin_add the parent owns the ref.
class VideoDecoderBackend {
 public:
  virtual ~VideoDecoderBackend() = default;

  // Stable identifier for logging + config. Should match the string
  // a user would write in config.toml's `*.backend = "..."` field.
  virtual std::string name() const = 0;

  // Selection order when multiple backends are available. Higher
  // wins. Conventions: software ~0, generic v4l2 ~50, platform-
  // specific backends 80..100.
  virtual int priority() const = 0;

  // Cheap runtime check. is_available() must not build pipelines
  // or hold resources — it's called during registry construction
  // and needs to be fast + side-effect-free. Typical probes: look
  // for a factory via gst_element_factory_find(), stat a /dev/*
  // node, read /sys/class/* metadata.
  virtual bool is_available() const = 0;

  // Full capability enumeration. Called lazily the first time the
  // registry is asked to select a backend for a codec; cached by
  // the registry. May be expensive (opens /dev/videoN to query
  // formats); is_available() is the fast path.
  virtual BackendCapabilities query_capabilities() const = 0;

  // Build a pipeline fragment that accepts encoded video of
  // `codec` on its sink pad and produces raw video (ideally
  // dmabuf-backed NV12 or a modifier-tagged equivalent) on its
  // src pad. Returns nullptr if codec isn't supported. Caller
  // links it into the pipeline and is responsible for releasing
  // the floating ref (gst_bin_add, etc).
  virtual GstElement* build_decoder_bin(const std::string& codec,
                                        const DecoderConfig& cfg) = 0;

  // Optional format-conversion fragment between the decoder output
  // and our appsink. Returns nullptr when no conversion is
  // required (src_modifier == dst_modifier, or both linear NV12).
  // Platforms that expose hardware converters (Rockchip RGA,
  // MediaTek MDP, Renesas FCP) plug them in here instead of
  // falling back to GStreamer's generic videoconvert.
  virtual GstElement* build_converter_bin(uint64_t src_modifier,
                                          uint64_t dst_modifier) = 0;

  // Apply backend-specific tuning to a decoder element that was
  // auto-plugged by playbin / uridecodebin rather than built via
  // build_decoder_bin(). Lets the backend share its property setup
  // (dmabuf-export toggles, output pool sizes, low-latency flags) with
  // the playbin path so Phase 2.6's observe-and-tune model matches the
  // fully constructed path. Called from deep-element-added after the
  // element has been classified as a video decoder; the element is
  // still in GST_STATE_NULL. Default no-op — backends opt in by
  // overriding when they have knobs worth applying.
  virtual void ConfigureAutoPluggedDecoder(GstElement* /*dec*/,
                                           const std::string& /*codec*/,
                                           const DecoderConfig& /*cfg*/) {}
};

}  // namespace video_player_linux
