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

#include "backend_imx8qm.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>

#include <plugins/common/common.h>

#include "backend_registry.h"
#include "platform_detection.h"

namespace video_player_linux {

namespace {

// Amphion on 8QM/8QXP is exposed as the standard V4L2 M2M decoders.
// Per-codec primary/fallback pairing matches GenericV4L2Backend
// because it IS the same factory set — this backend just tunes the
// output for the Amphion quirks and wins on priority.
struct FactoryChoice {
  const char* primary;
  const char* fallback;
};

const FactoryChoice& FactoriesForCodec(const std::string& codec) {
  static constexpr FactoryChoice h264{"v4l2h264dec", "v4l2slh264dec"};
  static constexpr FactoryChoice h265{"v4l2h265dec", "v4l2slh265dec"};
  static constexpr FactoryChoice vp8{"v4l2vp8dec", "v4l2slvp8dec"};
  static constexpr FactoryChoice vp9{"v4l2vp9dec", "v4l2slvp9dec"};
  static constexpr FactoryChoice none{nullptr, nullptr};
  if (codec == "h264")
    return h264;
  if (codec == "h265" || codec == "hevc")
    return h265;
  if (codec == "vp8")
    return vp8;
  if (codec == "vp9")
    return vp9;
  return none;
}

bool FactoryExists(const char* name) {
  if (!name) {
    return false;
  }
  GstElementFactory* f = gst_element_factory_find(name);
  if (!f) {
    return false;
  }
  gst_object_unref(f);
  return true;
}

GstElement* MakeDecoderElement(const FactoryChoice& c) {
  GstElement* dec = nullptr;
  if (c.primary) {
    dec = gst_element_factory_make(c.primary, nullptr);
  }
  if (!dec && c.fallback) {
    dec = gst_element_factory_make(c.fallback, nullptr);
  }
  return dec;
}

bool AnyV4L2DecoderFactoryExists() {
  static constexpr std::array<const char*, 8> kProbe{
      "v4l2h264dec", "v4l2slh264dec", "v4l2h265dec", "v4l2slh265dec",
      "v4l2vp8dec",  "v4l2slvp8dec",  "v4l2vp9dec",  "v4l2slvp9dec",
  };
  return std::any_of(kProbe.begin(), kProbe.end(),
                     [](const char* n) { return FactoryExists(n); });
}

void ApplyDmabufExport(GstElement* dec) {
  if (!dec) {
    return;
  }
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(dec), "output-io-mode")) {
    constexpr int kGstV4L2IOModeDmabufExport = 5;
    g_object_set(dec, "output-io-mode", kGstV4L2IOModeDmabufExport, nullptr);
  }
}

void ApplyDecoderConfig(GstElement* dec, const DecoderConfig& cfg) {
  if (!dec) {
    return;
  }
  GObjectClass* klass = G_OBJECT_GET_CLASS(dec);
  if (cfg.buffer_count > 0 &&
      g_object_class_find_property(klass, "num-output-buffers")) {
    g_object_set(dec, "num-output-buffers", static_cast<int>(cfg.buffer_count),
                 nullptr);
  }
  // v4l2 decoders expose a `capture-io-mode` alongside output-io-mode;
  // leaving it default on the input side keeps kernel driver choice
  // alone. Amphion-specific low-latency is handled in the kernel,
  // nothing to toggle here beyond what GenericV4L2Backend does.
  (void)cfg.low_latency;
}

// Match any v4l2 decoder factory name. Same prefix rule as the
// generic backend — we consult it when the platform check has
// already narrowed us to 8QM/8QXP, so any `v4l2*dec` we see is by
// construction the Amphion decoder.
bool IsV4L2Factory(const char* factory_name) {
  return factory_name && std::strncmp(factory_name, "v4l2", 4) == 0;
}

// Cache the platform profile — stable for the process lifetime.
PlatformProfile CachedPlatform() {
  static const PlatformProfile p = DetectPlatform();
  return p;
}

bool IsAmphionPlatform() {
  const PlatformProfile p = CachedPlatform();
  return p == PlatformProfile::Imx8QM || p == PlatformProfile::Imx8QXP;
}

// DRM modifier constants. AMPHION_TILED is the NXP-specific format
// that Malone emits; the symbolic name isn't in upstream
// drm_fourcc.h, so it's declared locally. The encoding is
// fourcc_mod_code(NXP, 1) — DRM_FORMAT_MOD_VENDOR_NXP = 0x0c.
constexpr uint64_t kDrmFormatModLinear = 0;
constexpr uint64_t kDrmFormatModAmphionTiled = 0x0c00000000000001ULL;

// Build a detile bin: sink pad carries NV12:AMPHION_TILED dmabuf,
// src pad emits NV12:LINEAR dmabuf via imxvideoconvert_g2d. Returned
// as a floating-ref GstElement the caller adds to its parent bin.
// Returns nullptr if imxvideoconvert_g2d isn't registered (caller
// falls back to generic videoconvert, which then drops zero-copy).
GstElement* BuildAmphionDetileBin() {
  GstElement* g2d = gst_element_factory_make("imxvideoconvert_g2d", nullptr);
  if (!g2d) {
    SPDLOG_DEBUG(
        "[VideoPlayer] imx_amphion: imxvideoconvert_g2d missing; no "
        "hardware detile available. Install gstreamer-imx or fall back "
        "to software videoconvert.");
    return nullptr;
  }

  GstElement* bin = gst_bin_new("amphion_detile");
  if (!bin) {
    gst_object_unref(g2d);
    return nullptr;
  }
  gst_bin_add(GST_BIN(bin), g2d);  // bin takes floating ref

  // Force LINEAR output via a capsfilter downstream of the G2D.
  // Constrain to NV12 linear dmabuf (drm-modifier=0 is linear).
  GstElement* caps = gst_element_factory_make("capsfilter", nullptr);
  if (!caps) {
    gst_object_unref(bin);
    return nullptr;
  }
  gst_bin_add(GST_BIN(bin), caps);

  GstCaps* linear_caps = gst_caps_from_string(
      "video/x-raw(memory:DMABuf), format=(string)NV12, "
      "drm-modifier=(uint64)0");
  g_object_set(caps, "caps", linear_caps, nullptr);
  gst_caps_unref(linear_caps);

  if (!gst_element_link(g2d, caps)) {
    SPDLOG_ERROR("[VideoPlayer] imx_amphion: failed to link G2D to capsfilter");
    gst_object_unref(bin);
    return nullptr;
  }

  // Ghost the G2D sink and the capsfilter src so the bin looks like a
  // single transform to the outer pipeline.
  GstPad* g2d_sink = gst_element_get_static_pad(g2d, "sink");
  GstPad* caps_src = gst_element_get_static_pad(caps, "src");
  gst_element_add_pad(bin, gst_ghost_pad_new("sink", g2d_sink));
  gst_element_add_pad(bin, gst_ghost_pad_new("src", caps_src));
  gst_object_unref(g2d_sink);
  gst_object_unref(caps_src);
  return bin;
}

}  // namespace

std::string Imx8QmBackend::name() const {
  return "imx_amphion";
}

int Imx8QmBackend::priority() const {
  return 100;
}

bool Imx8QmBackend::is_available() const {
  // Platform gate first — Amphion tuning is actively wrong on any
  // other SoC (different decoder behaviour, no G2D, no Amphion
  // modifier). Without the gate this backend would also win on
  // generic i.MX 8M boards, where GenericV4L2Backend must drive.
  if (!IsAmphionPlatform()) {
    return false;
  }
  return AnyV4L2DecoderFactoryExists();
}

BackendCapabilities Imx8QmBackend::query_capabilities() const {
  BackendCapabilities caps;
  // LINEAR is always acceptable (after G2D detile). AMPHION_TILED is
  // the decoder's native output — we advertise it so callers that
  // know how to request a converter (via build_converter_bin) can
  // keep the zero-copy path; callers that can't will ignore it and
  // accept the LINEAR entry.
  caps.supported_modifiers.push_back(kDrmFormatModLinear);
  caps.supported_modifiers.push_back(kDrmFormatModAmphionTiled);

  const std::pair<const char*, const char*> codecs[] = {
      {"h264", "H264"},
      {"h265", "H265/HEVC"},
      {"vp8", "VP8"},
      {"vp9", "VP9"},
  };
  for (const auto& [short_name, _display] : codecs) {
    const auto& choice = FactoriesForCodec(short_name);
    if (FactoryExists(choice.primary) || FactoryExists(choice.fallback)) {
      caps.supported_codecs.emplace_back(short_name);
    }
  }

  caps.max_width = 3840;       // 4K on 8QM; 8QXP is 1920 but Amphion
  caps.max_height = 2160;      // caps don't advertise per-tier limits
  caps.supports_10bit = true;  // Amphion V4 supports 10-bit HEVC
  caps.supports_hdr = false;
  caps.supports_afbc = false;
  return caps;
}

GstElement* Imx8QmBackend::build_decoder_bin(const std::string& codec,
                                             const DecoderConfig& cfg) {
  const auto& choice = FactoriesForCodec(codec);
  GstElement* dec = MakeDecoderElement(choice);
  if (!dec) {
    SPDLOG_DEBUG("[VideoPlayer] imx_amphion: no factory for codec '{}'", codec);
    return nullptr;
  }
  ApplyDmabufExport(dec);
  ApplyDecoderConfig(dec, cfg);
  SPDLOG_DEBUG("[VideoPlayer] imx_amphion: built {} decoder for '{}'",
               GST_OBJECT_NAME(gst_element_get_factory(dec)), codec);
  return dec;
}

GstElement* Imx8QmBackend::build_converter_bin(uint64_t src_modifier,
                                               uint64_t dst_modifier) {
  // No conversion needed when the source is already LINEAR (or when
  // the caller happens to want AMPHION_TILED and can consume it).
  if (src_modifier == dst_modifier || src_modifier == kDrmFormatModLinear) {
    return nullptr;
  }
  // Only the AMPHION_TILED → LINEAR transition is handled at this
  // tier. Any other pair hands back nullptr and the caller falls
  // through to software videoconvert, which can still do the
  // conversion (losing zero-copy).
  if (src_modifier != kDrmFormatModAmphionTiled ||
      dst_modifier != kDrmFormatModLinear) {
    return nullptr;
  }
  return BuildAmphionDetileBin();
}

void Imx8QmBackend::ConfigureAutoPluggedDecoder(GstElement* dec,
                                                const std::string& codec,
                                                const DecoderConfig& cfg) {
  if (!dec) {
    return;
  }
  GstElementFactory* factory = gst_element_get_factory(dec);
  if (!factory) {
    return;
  }
  const gchar* factory_name = GST_OBJECT_NAME(factory);
  if (!IsV4L2Factory(factory_name)) {
    return;
  }
  ApplyDmabufExport(dec);
  ApplyDecoderConfig(dec, cfg);
  // AMPHION_TILED detile is NOT applied here — playbin owns the
  // pipeline topology and won't insert imxvideoconvert_g2d on its
  // own. On 8QM that means the downstream NV12 shader path will
  // receive tiled buffers and render garbage unless VideoPlayer
  // builds the explicit pipeline. Until that wiring lands (Phase
  // 3.x follow-up), flag it so users hit the warning at first play
  // instead of silently seeing corruption.
  spdlog::warn(
      "[VideoPlayer] imx_amphion: auto-plugged {} for '{}'. Amphion output "
      "uses DRM_FORMAT_MOD_AMPHION_TILED which current GL drivers cannot "
      "sample; insert imxvideoconvert_g2d upstream of the sink or force "
      "the backend's explicit build path to avoid corrupt video.",
      factory_name, codec);
}

void RegisterImx8QmBackend() {
  static std::atomic<bool> registered{false};
  if (registered.exchange(true)) {
    return;
  }
  BackendRegistry::Instance().RegisterBackend(
      std::make_unique<Imx8QmBackend>());
}

}  // namespace video_player_linux
