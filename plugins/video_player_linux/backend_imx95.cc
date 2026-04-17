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

#include "backend_imx95.h"

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

// Ordered candidate list per codec. First is the NXP vendor element
// (imxvpudec_<codec>); the rest are mainline v4l2 fallbacks. AV1
// has only the vendor entry — no mainline v4l2 AV1 decoder at the
// time of writing, which is exactly why AV1 support is a reliable
// "this is i.MX 95" signal.
struct FactoryList {
  std::array<const char*, 4> names;  // null-terminated
};

const FactoryList& FactoriesForCodec(const std::string& codec) {
  static constexpr FactoryList h264{
      {"imxvpudec_h264", "v4l2h264dec", "v4l2slh264dec", nullptr}};
  static constexpr FactoryList h265{
      {"imxvpudec_h265", "v4l2h265dec", "v4l2slh265dec", nullptr}};
  static constexpr FactoryList vp9{
      {"imxvpudec_vp9", "v4l2vp9dec", "v4l2slvp9dec", nullptr}};
  static constexpr FactoryList av1{
      {"imxvpudec_av1", nullptr, nullptr, nullptr}};
  static constexpr FactoryList none{{nullptr, nullptr, nullptr, nullptr}};
  if (codec == "h264")
    return h264;
  if (codec == "h265" || codec == "hevc")
    return h265;
  if (codec == "vp9")
    return vp9;
  if (codec == "av1")
    return av1;
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

// Is `factory_name` one this backend is willing to tune? Accepts both
// NXP vendor elements (imxvpudec_*) and mainline v4l2 decoders — on
// i.MX 95 the kernel may expose the VPU through either depending on
// BSP choice.
bool IsSupportedFactory(const char* factory_name) {
  if (!factory_name) {
    return false;
  }
  return std::strncmp(factory_name, "imxvpudec", 9) == 0 ||
         std::strncmp(factory_name, "v4l2", 4) == 0;
}

// Is this a mainline v4l2 decoder? Used to decide whether
// output-io-mode=dmabuf-export is the right knob (v4l2 yes; the NXP
// vendor elements manage dmabuf internally and ignore it).
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

// Low-latency knobs supported across the gstreamer-imx vendor element
// family. Gated on property presence — mainline v4l2 decoders won't
// expose most of these. See plan.md §3.4.
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
  // Mainline v4l2 exposes num-output-buffers; gstreamer-imx exposes
  // num-capture-buffers on recent versions, output-buffers on older.
  if (g_object_class_find_property(klass, "num-output-buffers")) {
    g_object_set(dec, "num-output-buffers", static_cast<int>(count), nullptr);
  } else if (g_object_class_find_property(klass, "num-capture-buffers")) {
    g_object_set(dec, "num-capture-buffers", static_cast<int>(count), nullptr);
  } else if (g_object_class_find_property(klass, "output-buffers")) {
    g_object_set(dec, "output-buffers", static_cast<int>(count), nullptr);
  }
}

// Cache the platform profile — stable for the process lifetime.
PlatformProfile CachedPlatform() {
  static const PlatformProfile p = DetectPlatform();
  return p;
}

constexpr uint64_t kDrmFormatModLinear = 0;

}  // namespace

std::string Imx95Backend::name() const {
  return "imx95_vpu";
}

int Imx95Backend::priority() const {
  return 105;
}

bool Imx95Backend::is_available() const {
  // Two signals — either is enough:
  //  1. DT compatible identified the SoC as i.MX 95.
  //  2. imxvpudec_av1 is registered (no other i.MX variant has AV1,
  //     so presence of this factory is a tight fingerprint).
  if (CachedPlatform() == PlatformProfile::Imx95) {
    return true;
  }
  return FactoryExists("imxvpudec_av1");
}

BackendCapabilities Imx95Backend::query_capabilities() const {
  BackendCapabilities caps;
  caps.supported_modifiers.push_back(kDrmFormatModLinear);

  for (const char* codec : {"h264", "h265", "vp9", "av1"}) {
    if (AnyFactoryExists(FactoriesForCodec(codec))) {
      caps.supported_codecs.emplace_back(codec);
    }
  }

  caps.max_width = 3840;
  caps.max_height = 2160;
  caps.supports_10bit = true;
  // i.MX 95's VPU preserves HDR10 / HLG side data through decode.
  // Actual HDR presentation requires a matching display path; the
  // flag here says "the decoder won't drop the metadata".
  caps.supports_hdr = true;
  caps.supports_afbc = false;
  return caps;
}

GstElement* Imx95Backend::build_decoder_bin(const std::string& codec,
                                            const DecoderConfig& cfg) {
  const auto& list = FactoriesForCodec(codec);
  GstElement* dec = MakeDecoderElement(list);
  if (!dec) {
    SPDLOG_DEBUG("[VideoPlayer] imx95_vpu: no factory for codec '{}'", codec);
    return nullptr;
  }
  GstElementFactory* factory = gst_element_get_factory(dec);
  const gchar* factory_name = factory ? GST_OBJECT_NAME(factory) : nullptr;

  if (IsV4L2Factory(factory_name)) {
    ApplyDmabufExport(dec);
  }
  if (cfg.low_latency) {
    ApplyLowLatency(dec);
  }
  ApplyBufferCount(dec, cfg.buffer_count);

  SPDLOG_DEBUG("[VideoPlayer] imx95_vpu: built {} decoder for '{}'",
               factory_name ? factory_name : "?", codec);
  return dec;
}

GstElement* Imx95Backend::build_converter_bin(uint64_t src_modifier,
                                              uint64_t dst_modifier) {
  // i.MX 95's VPU emits LINEAR directly — no detile needed at this
  // tier. Any modifier-to-modifier request falls through to the
  // caller's software converter fallback.
  (void)src_modifier;
  (void)dst_modifier;
  return nullptr;
}

void Imx95Backend::ConfigureAutoPluggedDecoder(GstElement* dec,
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
  if (!IsSupportedFactory(factory_name)) {
    return;
  }
  if (IsV4L2Factory(factory_name)) {
    ApplyDmabufExport(dec);
  }
  if (cfg.low_latency) {
    ApplyLowLatency(dec);
  }
  ApplyBufferCount(dec, cfg.buffer_count);
  SPDLOG_DEBUG("[VideoPlayer] imx95_vpu: tuned auto-plugged {} for '{}'",
               factory_name, codec);
}

void RegisterImx95Backend() {
  static std::atomic<bool> registered{false};
  if (registered.exchange(true)) {
    return;
  }
  BackendRegistry::Instance().RegisterBackend(std::make_unique<Imx95Backend>());
}

}  // namespace video_player_linux
