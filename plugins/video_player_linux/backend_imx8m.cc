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

#include "backend_imx8m.h"

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

// Candidate factory names per codec. Tried in order; first one that
// gst_element_factory_make() succeeds on wins. `imxvpudec_<codec>` is
// the newer vendor element; `vpudec` is the catch-all found on older
// gstreamer-imx builds and some community ports. The v4l2codecs names
// intentionally are NOT listed here — when mainline kernel exposes the
// VPU via v4l2codecs, GenericV4L2Backend handles it at priority 50 and
// this backend's is_available() returns false.
struct FactoryList {
  std::array<const char*, 3> names;  // null-terminated
};

const FactoryList& FactoriesForCodec(const std::string& codec) {
  static constexpr FactoryList h264{{"imxvpudec_h264", "vpudec", nullptr}};
  static constexpr FactoryList h265{{"imxvpudec_h265", "vpudec", nullptr}};
  static constexpr FactoryList vp8{{"imxvpudec_vp8", "vpudec", nullptr}};
  static constexpr FactoryList vp9{{"imxvpudec_vp9", "vpudec", nullptr}};
  static constexpr FactoryList none{{nullptr, nullptr, nullptr}};
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

bool AnyFactoryExists(const FactoryList& list) {
  return std::any_of(list.names.begin(), list.names.end(),
                     [](const char* n) { return FactoryExists(n); });
}

GstElement* MakeDecoderElement(const FactoryList& list) {
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

// Does `factory_name` look like an i.MX VPU decoder? Used so
// ConfigureAutoPluggedDecoder only tunes elements it understands and
// leaves unrelated factories (avdec_*, vaapi*, v4l2*dec) alone.
bool IsVpuFactory(const char* factory_name) {
  if (!factory_name) {
    return false;
  }
  return std::strncmp(factory_name, "imxvpudec", 9) == 0 ||
         std::strcmp(factory_name, "vpudec") == 0;
}

// Low-latency knobs documented by NXP for vpudec / imxvpudec_*. Not
// every property exists on every element version — gate each with
// g_object_class_find_property so this remains a best-effort tuning
// pass rather than a hard fail. See plan.md §3.4.
void ApplyLowLatency(GstElement* dec) {
  if (!dec) {
    return;
  }
  GObjectClass* klass = G_OBJECT_GET_CLASS(dec);
  if (g_object_class_find_property(klass, "low-latency")) {
    g_object_set(dec, "low-latency", TRUE, nullptr);
  }
  if (g_object_class_find_property(klass, "frame-plus")) {
    g_object_set(dec, "frame-plus", 0, nullptr);
  }
  if (g_object_class_find_property(klass, "frame-drop")) {
    g_object_set(dec, "frame-drop", FALSE, nullptr);
  }
}

void ApplyBufferCount(GstElement* dec, unsigned count) {
  if (!dec || count == 0) {
    return;
  }
  GObjectClass* klass = G_OBJECT_GET_CLASS(dec);
  // gstreamer-imx elements expose "num-capture-buffers" on some
  // versions and "output-buffers" on others; try both.
  if (g_object_class_find_property(klass, "num-capture-buffers")) {
    g_object_set(dec, "num-capture-buffers", static_cast<int>(count), nullptr);
  } else if (g_object_class_find_property(klass, "output-buffers")) {
    g_object_set(dec, "output-buffers", static_cast<int>(count), nullptr);
  }
}

// DRM_FORMAT_MOD_ARM_NV12_4L4 — the 4x4-tiled NV12 modifier the Hantro
// G2 on 8M Plus emits for H.265. Declared locally as a numeric constant
// because the symbolic name isn't in mainline drm_fourcc.h; the encoding
// follows fourcc_mod_code(ARM, ARM_TYPE_TILED_NV12_4L4). Consumers that
// can't sample this modifier will see it in query_capabilities() and
// can ask for a converter bin instead.
constexpr uint64_t kDrmFormatModLinear = 0;
constexpr uint64_t kDrmFormatModArmNv12_4L4 = 0x0800000000000008ULL;

// Cache the platform profile so query_capabilities() doesn't hit the
// filesystem on every call. PlatformProfile is stable for the process
// lifetime, so a function-local static is safe.
PlatformProfile CachedPlatform() {
  static const PlatformProfile p = DetectPlatform();
  return p;
}

}  // namespace

std::string Imx8mVpuBackend::name() const {
  return "imx_vpu";
}

int Imx8mVpuBackend::priority() const {
  return 100;
}

bool Imx8mVpuBackend::is_available() const {
  // At least one vendor factory must be registered. gstreamer-imx
  // only registers when the VPU device node exists, so factory
  // presence is a reasonable proxy for "is this i.MX BSP".
  static constexpr std::array<const char*, 5> kProbe{
      "imxvpudec_h264", "imxvpudec_h265", "imxvpudec_vp8",
      "imxvpudec_vp9",  "vpudec",
  };
  return std::any_of(kProbe.begin(), kProbe.end(),
                     [](const char* n) { return FactoryExists(n); });
}

BackendCapabilities Imx8mVpuBackend::query_capabilities() const {
  BackendCapabilities caps;
  caps.supported_modifiers.push_back(kDrmFormatModLinear);

  const PlatformProfile profile = CachedPlatform();
  const bool is_plus = profile == PlatformProfile::Imx8MPlus;

  // 8M base has Hantro G1 only: H.264, VP8 decode up to 1080p.
  // 8M Plus adds Hantro G2: H.265, VP9 up to 4K and 10-bit.
  if (AnyFactoryExists(FactoriesForCodec("h264"))) {
    caps.supported_codecs.emplace_back("h264");
  }
  if (is_plus && AnyFactoryExists(FactoriesForCodec("h265"))) {
    caps.supported_codecs.emplace_back("h265");
    // Hantro G2 output for H.265. We list it but do NOT set it as the
    // default — downstream paths that can sample 4L4 tiles will grab it
    // through caps negotiation; the rest fall back to LINEAR via
    // build_converter_bin or the driver's implicit detile.
    caps.supported_modifiers.push_back(kDrmFormatModArmNv12_4L4);
  }
  if (AnyFactoryExists(FactoriesForCodec("vp8"))) {
    caps.supported_codecs.emplace_back("vp8");
  }
  if (is_plus && AnyFactoryExists(FactoriesForCodec("vp9"))) {
    caps.supported_codecs.emplace_back("vp9");
  }

  caps.max_width = is_plus ? 3840 : 1920;
  caps.max_height = is_plus ? 2160 : 1080;
  caps.supports_10bit = is_plus;  // Hantro G2 adds 10-bit
  caps.supports_hdr = false;
  caps.supports_afbc = false;
  return caps;
}

GstElement* Imx8mVpuBackend::build_decoder_bin(const std::string& codec,
                                               const DecoderConfig& cfg) {
  const auto& list = FactoriesForCodec(codec);
  GstElement* dec = MakeDecoderElement(list);
  if (!dec) {
    SPDLOG_DEBUG("[VideoPlayer] imx_vpu: no factory for codec '{}'", codec);
    return nullptr;
  }
  if (cfg.low_latency) {
    ApplyLowLatency(dec);
  }
  ApplyBufferCount(dec, cfg.buffer_count);

  SPDLOG_DEBUG("[VideoPlayer] imx_vpu: built {} decoder for '{}'",
               GST_OBJECT_NAME(gst_element_get_factory(dec)), codec);
  return dec;
}

GstElement* Imx8mVpuBackend::build_converter_bin(uint64_t src_modifier,
                                                 uint64_t dst_modifier) {
  // Phase 3.1 scope — no hardware detile yet. 8M Plus tiled-to-linear
  // detiling via imxvideoconvert_g2d lands in Phase 3.2 alongside the
  // same mechanism for Amphion on 8QM. Returning nullptr here makes
  // the caller fall back to GStreamer's software videoconvert, which
  // still works correctly — just not zero-copy.
  (void)src_modifier;
  (void)dst_modifier;
  return nullptr;
}

void Imx8mVpuBackend::ConfigureAutoPluggedDecoder(GstElement* dec,
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
  if (!IsVpuFactory(factory_name)) {
    return;
  }
  if (cfg.low_latency) {
    ApplyLowLatency(dec);
  }
  ApplyBufferCount(dec, cfg.buffer_count);
  SPDLOG_DEBUG("[VideoPlayer] imx_vpu: tuned auto-plugged {} for '{}'",
               factory_name, codec);
}

void RegisterImx8mVpuBackend() {
  static std::atomic<bool> registered{false};
  if (registered.exchange(true)) {
    return;
  }
  BackendRegistry::Instance().RegisterBackend(
      std::make_unique<Imx8mVpuBackend>());
}

}  // namespace video_player_linux
