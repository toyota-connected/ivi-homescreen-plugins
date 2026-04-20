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

#include "backend_rockchip.h"

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

// Cached platform profile — stable for process lifetime.
PlatformProfile CachedPlatform() {
  static const PlatformProfile p = DetectPlatform();
  return p;
}

bool IsRockchipPlatform() {
  const PlatformProfile p = CachedPlatform();
  return p == PlatformProfile::RockchipRK3568 ||
         p == PlatformProfile::RockchipRK3588;
}

bool IsRK3588() {
  return CachedPlatform() == PlatformProfile::RockchipRK3588;
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

// DRM modifier constants. LINEAR is the everywhere-safe fallback.
// AFBC is what Rockchip MPP actually emits when `arm-afbc=TRUE` —
// specifically the 16x16-block, sparse, YTR-encoded variant matching
// plan.md §4.1. The symbolic form is
// DRM_FORMAT_MOD_ARM_AFBC(BLOCK_SIZE_16x16 | SPARSE | YTR), which
// expands to fourcc_mod_code(ARM=0x08, 0x51) — we write the numeric
// value inline so the backend doesn't require drm_fourcc.h to be
// available at build time (it isn't on every cross-compile target).
//
// AFBC mode bits:
//   BLOCK_SIZE_16x16 = 0x01
//   YTR              = 0x10
//   SPARSE           = 0x40
constexpr uint64_t kDrmFormatModLinear = 0;
constexpr uint64_t kDrmFormatModArmAfbc16x16SparseYtr = 0x0800000000000051ULL;

// ───────────────────────────────────────────────────────────────────
// MPP factory handling
// ───────────────────────────────────────────────────────────────────

// MPP candidates per codec. `mppvideodec` is the newer unified
// element that handles h264/h265/vp9 via caps; on older gst-rockchip
// the per-codec elements are still the only option. Try unified
// first, fall back to per-codec, fall back to `rkximagedec` which
// some RK3588 BSPs ship.
struct FactoryList {
  std::array<const char*, 3> names;
};

const FactoryList& MppFactoriesForCodec(const std::string& codec) {
  static constexpr FactoryList h264{{"mppvideodec", "mpph264dec", nullptr}};
  static constexpr FactoryList h265{{"mppvideodec", "mpph265dec", nullptr}};
  static constexpr FactoryList vp9{{"mppvideodec", "mppvp9dec", nullptr}};
  static constexpr FactoryList av1{{"mppav1dec", nullptr, nullptr}};
  static constexpr FactoryList mpeg2{{"mppmpeg2dec", nullptr, nullptr}};
  static constexpr FactoryList none{{nullptr, nullptr, nullptr}};
  if (codec == "h264")
    return h264;
  if (codec == "h265" || codec == "hevc")
    return h265;
  if (codec == "vp9")
    return vp9;
  if (codec == "av1")
    return av1;
  if (codec == "mpeg2")
    return mpeg2;
  return none;
}

bool AnyMppFactoryExists(const FactoryList& list) {
  return std::any_of(list.names.begin(), list.names.end(),
                     [](const char* n) { return FactoryExists(n); });
}

GstElement* MakeMppDecoderElement(const FactoryList& list) {
  for (const char* name : list.names) {
    if (!name) {
      break;
    }
    if (GstElement* dec = gst_element_factory_make(name, nullptr)) {
      return dec;
    }
  }
  return nullptr;
}

bool IsMppFactory(const char* factory_name) {
  return factory_name && std::strncmp(factory_name, "mpp", 3) == 0;
}

// Apply MPP-specific knobs. All guarded by property-presence because
// gst-rockchip properties vary across fork/version.
void ConfigureMpp(GstElement* dec, const DecoderConfig& cfg) {
  if (!dec) {
    return;
  }
  GObjectClass* klass = G_OBJECT_GET_CLASS(dec);
  if (cfg.enable_afbc && g_object_class_find_property(klass, "arm-afbc")) {
    g_object_set(dec, "arm-afbc", TRUE, nullptr);
  }
  // Fast mode — parallelizes decode across RK3588's dual VPU cores.
  // Not present on RK3568 (single VPU), and actively wrong to set
  // elsewhere, so gate on both platform and property.
  if (IsRK3588() && g_object_class_find_property(klass, "fast-mode")) {
    g_object_set(dec, "fast-mode", TRUE, nullptr);
  }
  if (cfg.low_latency) {
    if (g_object_class_find_property(klass, "ignore-error")) {
      g_object_set(dec, "ignore-error", TRUE, nullptr);
    }
    if (g_object_class_find_property(klass, "output-format")) {
      g_object_set(dec, "output-format", "NV12", nullptr);
    }
  }
  if (cfg.buffer_count > 0) {
    if (g_object_class_find_property(klass, "num-output-buffers")) {
      g_object_set(dec, "num-output-buffers",
                   static_cast<int>(cfg.buffer_count), nullptr);
    } else if (g_object_class_find_property(klass, "output-buffers")) {
      g_object_set(dec, "output-buffers", static_cast<int>(cfg.buffer_count),
                   nullptr);
    }
  }
}

// ───────────────────────────────────────────────────────────────────
// V4L2 factory handling — same factory names as GenericV4L2Backend
// ───────────────────────────────────────────────────────────────────

struct V4L2FactoryChoice {
  const char* primary;
  const char* fallback;
};

const V4L2FactoryChoice& V4L2FactoriesForCodec(const std::string& codec) {
  static constexpr V4L2FactoryChoice h264{"v4l2h264dec", "v4l2slh264dec"};
  static constexpr V4L2FactoryChoice h265{"v4l2h265dec", "v4l2slh265dec"};
  static constexpr V4L2FactoryChoice vp8{"v4l2vp8dec", "v4l2slvp8dec"};
  static constexpr V4L2FactoryChoice vp9{"v4l2vp9dec", "v4l2slvp9dec"};
  static constexpr V4L2FactoryChoice av1{"v4l2av1dec", "v4l2slav1dec"};
  static constexpr V4L2FactoryChoice none{nullptr, nullptr};
  if (codec == "h264")
    return h264;
  if (codec == "h265" || codec == "hevc")
    return h265;
  if (codec == "vp8")
    return vp8;
  if (codec == "vp9")
    return vp9;
  if (codec == "av1")
    return av1;
  return none;
}

GstElement* MakeV4L2DecoderElement(const V4L2FactoryChoice& c) {
  GstElement* dec = nullptr;
  if (c.primary) {
    dec = gst_element_factory_make(c.primary, nullptr);
  }
  if (!dec && c.fallback) {
    dec = gst_element_factory_make(c.fallback, nullptr);
  }
  return dec;
}

bool AnyV4L2FactoryExists() {
  static constexpr std::array<const char*, 10> kProbe{
      "v4l2h264dec", "v4l2slh264dec", "v4l2h265dec", "v4l2slh265dec",
      "v4l2vp8dec",  "v4l2slvp8dec",  "v4l2vp9dec",  "v4l2slvp9dec",
      "v4l2av1dec",  "v4l2slav1dec",
  };
  return std::any_of(kProbe.begin(), kProbe.end(),
                     [](const char* n) { return FactoryExists(n); });
}

bool IsV4L2Factory(const char* factory_name) {
  return factory_name && std::strncmp(factory_name, "v4l2", 4) == 0;
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

void ApplyV4L2Tuning(GstElement* dec, const DecoderConfig& cfg) {
  ApplyDmabufExport(dec);
  if (cfg.buffer_count > 0 &&
      g_object_class_find_property(G_OBJECT_GET_CLASS(dec),
                                   "num-output-buffers")) {
    g_object_set(dec, "num-output-buffers", static_cast<int>(cfg.buffer_count),
                 nullptr);
  }
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════
// RockchipMppBackend
// ═══════════════════════════════════════════════════════════════════

std::string RockchipMppBackend::name() const {
  return "rockchip_mpp";
}

int RockchipMppBackend::priority() const {
  return 100;
}

bool RockchipMppBackend::is_available() const {
  if (!IsRockchipPlatform()) {
    return false;
  }
  // Any MPP factory is enough — mppvideodec covers h264/h265/vp9,
  // mpph264dec etc. handle older BSPs, mppav1dec is RK3588-only.
  static constexpr std::array<const char*, 6> kProbe{
      "mppvideodec", "mpph264dec", "mpph265dec",
      "mppvp9dec",   "mppav1dec",  "mppmpeg2dec",
  };
  return std::any_of(kProbe.begin(), kProbe.end(),
                     [](const char* n) { return FactoryExists(n); });
}

BackendCapabilities RockchipMppBackend::query_capabilities() const {
  BackendCapabilities caps;
  caps.supported_modifiers.push_back(kDrmFormatModLinear);
  caps.supported_modifiers.push_back(kDrmFormatModArmAfbc16x16SparseYtr);

  for (const char* codec : {"h264", "h265", "vp9", "av1", "mpeg2"}) {
    if (AnyMppFactoryExists(MppFactoriesForCodec(codec))) {
      caps.supported_codecs.emplace_back(codec);
    }
  }

  const bool is_3588 = IsRK3588();
  caps.max_width = is_3588 ? 7680 : 3840;  // 8K on RK3588
  caps.max_height = is_3588 ? 4320 : 2160;
  caps.supports_10bit = true;
  caps.supports_hdr = is_3588;  // RK3588 adds HDR10 side-data preservation
  caps.supports_afbc = true;
  return caps;
}

GstElement* RockchipMppBackend::build_decoder_bin(const std::string& codec,
                                                  const DecoderConfig& cfg) {
  const auto& list = MppFactoriesForCodec(codec);
  GstElement* dec = MakeMppDecoderElement(list);
  if (!dec) {
    SPDLOG_DEBUG("[VideoPlayer] rockchip_mpp: no factory for codec '{}'",
                 codec);
    return nullptr;
  }
  ConfigureMpp(dec, cfg);
  SPDLOG_DEBUG("[VideoPlayer] rockchip_mpp: built {} decoder for '{}'",
               GST_OBJECT_NAME(gst_element_get_factory(dec)), codec);
  return dec;
}

GstElement* RockchipMppBackend::build_converter_bin(uint64_t src_modifier,
                                                    uint64_t dst_modifier) {
  // Phase 4.2 will add the RGA detile fallback (AFBC → LINEAR) when
  // the Mali driver can't sample AFBC. For now return nullptr so the
  // caller gets software videoconvert, which is still correct just
  // not zero-copy.
  (void)src_modifier;
  (void)dst_modifier;
  return nullptr;
}

void RockchipMppBackend::ConfigureAutoPluggedDecoder(GstElement* dec,
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
  if (!IsMppFactory(factory_name)) {
    return;
  }
  ConfigureMpp(dec, cfg);
  SPDLOG_DEBUG("[VideoPlayer] rockchip_mpp: tuned auto-plugged {} for '{}'",
               factory_name, codec);
}

// ═══════════════════════════════════════════════════════════════════
// RockchipV4L2Backend
// ═══════════════════════════════════════════════════════════════════

std::string RockchipV4L2Backend::name() const {
  return "rockchip_v4l2";
}

int RockchipV4L2Backend::priority() const {
  // Below MPP (100) but above GenericV4L2 (50). When both MPP and
  // mainline v4l2 are present (unusual but possible on transition
  // BSPs), MPP wins. On mainline-only builds this backend wins over
  // the generic fallback so Rockchip-specific tuning (future:
  // platform-aware modifier preference) has somewhere to live.
  return 90;
}

bool RockchipV4L2Backend::is_available() const {
  return IsRockchipPlatform() && AnyV4L2FactoryExists();
}

BackendCapabilities RockchipV4L2Backend::query_capabilities() const {
  BackendCapabilities caps;
  caps.supported_modifiers.push_back(kDrmFormatModLinear);

  for (const char* codec : {"h264", "h265", "vp8", "vp9", "av1"}) {
    const auto& choice = V4L2FactoriesForCodec(codec);
    if (FactoryExists(choice.primary) || FactoryExists(choice.fallback)) {
      caps.supported_codecs.emplace_back(codec);
    }
  }

  const bool is_3588 = IsRK3588();
  caps.max_width = is_3588 ? 7680 : 3840;
  caps.max_height = is_3588 ? 4320 : 2160;
  caps.supports_10bit = true;
  caps.supports_hdr = is_3588;
  // Mainline v4l2 on Rockchip doesn't negotiate AFBC — MPP backend
  // is the AFBC path. We advertise only LINEAR here.
  caps.supports_afbc = false;
  return caps;
}

GstElement* RockchipV4L2Backend::build_decoder_bin(const std::string& codec,
                                                   const DecoderConfig& cfg) {
  const auto& choice = V4L2FactoriesForCodec(codec);
  GstElement* dec = MakeV4L2DecoderElement(choice);
  if (!dec) {
    SPDLOG_DEBUG("[VideoPlayer] rockchip_v4l2: no factory for codec '{}'",
                 codec);
    return nullptr;
  }
  ApplyV4L2Tuning(dec, cfg);
  (void)cfg.low_latency;  // no generic v4l2 property; Rockchip-
                          // specific low-latency path is MPP-only.
  SPDLOG_DEBUG("[VideoPlayer] rockchip_v4l2: built {} decoder for '{}'",
               GST_OBJECT_NAME(gst_element_get_factory(dec)), codec);
  return dec;
}

GstElement* RockchipV4L2Backend::build_converter_bin(uint64_t src_modifier,
                                                     uint64_t dst_modifier) {
  (void)src_modifier;
  (void)dst_modifier;
  return nullptr;
}

void RockchipV4L2Backend::ConfigureAutoPluggedDecoder(
    GstElement* dec,
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
  ApplyV4L2Tuning(dec, cfg);
  SPDLOG_DEBUG("[VideoPlayer] rockchip_v4l2: tuned auto-plugged {} for '{}'",
               factory_name, codec);
}

void RegisterRockchipBackends() {
  static std::atomic<bool> registered{false};
  if (registered.exchange(true)) {
    return;
  }
  BackendRegistry::Instance().RegisterBackend(
      std::make_unique<RockchipMppBackend>());
  BackendRegistry::Instance().RegisterBackend(
      std::make_unique<RockchipV4L2Backend>());
}

}  // namespace video_player_linux
