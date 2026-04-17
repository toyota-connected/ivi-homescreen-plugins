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

#include "backend_generic_v4l2.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <string>

#include <plugins/common/common.h>

#include "backend_registry.h"

namespace video_player_linux {

namespace {

// v4l2h264dec is the stateful decoder; v4l2slh264dec is the stateless
// counterpart (newer kernels with media request API). Stateful is
// faster on existing vendor BSPs and is tried first. The fallback
// order per codec is hardcoded here rather than enum'd so new
// codecs can be added by extending these tables.
struct FactoryChoice {
  const char* primary;   // preferred stateful factory
  const char* fallback;  // stateless fallback
};

const FactoryChoice& FactoriesForCodec(const std::string& codec) {
  static const FactoryChoice h264{"v4l2h264dec", "v4l2slh264dec"};
  static const FactoryChoice h265{"v4l2h265dec", "v4l2slh265dec"};
  static const FactoryChoice vp8{"v4l2vp8dec", "v4l2slvp8dec"};
  static const FactoryChoice vp9{"v4l2vp9dec", "v4l2slvp9dec"};
  static const FactoryChoice av1{"v4l2av1dec", "v4l2slav1dec"};
  static const FactoryChoice none{nullptr, nullptr};

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

bool AnyFactoryExists(const FactoryChoice& c) {
  return FactoryExists(c.primary) || FactoryExists(c.fallback);
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

// GStreamer v4l2 decoders accept `output-io-mode = dmabuf-export (5)`
// so the decoded NV12 / NV12_4L4 frames land as dmabuf FDs ready for
// Phase 1.3's EGLImage import. This is a no-op on factories that
// don't expose the property, which keeps us compatible with the
// stateless decoders (which use a different mechanism).
void ApplyDmabufExport(GstElement* decoder) {
  if (!decoder) {
    return;
  }
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(decoder),
                                   "output-io-mode")) {
    constexpr int kGstV4L2IOModeDmabufExport = 5;
    g_object_set(decoder, "output-io-mode", kGstV4L2IOModeDmabufExport,
                 nullptr);
  }
}

}  // namespace

std::string GenericV4L2Backend::name() const {
  return "v4l2_stateless";
}

int GenericV4L2Backend::priority() const {
  return 50;
}

bool GenericV4L2Backend::is_available() const {
  // At least one v4l2 codec factory must exist. GStreamer's v4l2
  // plugin only registers factories when it finds a matching
  // kernel device at init time, so factory presence is a reasonable
  // proxy for "has V4L2 codec hardware".
  static constexpr std::array<const char*, 8> kProbe{
      "v4l2h264dec", "v4l2slh264dec", "v4l2h265dec", "v4l2slh265dec",
      "v4l2vp9dec",  "v4l2slvp9dec",  "v4l2av1dec",  "v4l2slav1dec",
  };
  return std::any_of(kProbe.begin(), kProbe.end(),
                     [](const char* n) { return FactoryExists(n); });
}

BackendCapabilities GenericV4L2Backend::query_capabilities() const {
  BackendCapabilities caps;
  caps.supported_modifiers.push_back(0);  // DRM_FORMAT_MOD_LINEAR
  caps.supports_10bit = false;
  caps.supports_hdr = false;
  caps.supports_afbc = false;

  const std::pair<const char*, const char*> codecs[] = {
      {"h264", "H264"}, {"h265", "H265/HEVC"}, {"vp8", "VP8"},
      {"vp9", "VP9"},   {"av1", "AV1"},
  };
  for (const auto& [short_name, _display] : codecs) {
    if (AnyFactoryExists(FactoriesForCodec(short_name))) {
      caps.supported_codecs.emplace_back(short_name);
    }
  }
  return caps;
}

GstElement* GenericV4L2Backend::build_decoder_bin(const std::string& codec,
                                                  const DecoderConfig& cfg) {
  const auto& choice = FactoriesForCodec(codec);
  GstElement* dec = MakeDecoderElement(choice);
  if (!dec) {
    SPDLOG_DEBUG("[VideoPlayer] v4l2_stateless: no factory for codec '{}'",
                 codec);
    return nullptr;
  }
  ApplyDmabufExport(dec);

  // `num-buffers` style tuning — some v4l2 decoders expose a
  // "min-capture-buffers" / "num-output-buffers" property. Apply
  // buffer_count when the property is present and the caller asked
  // for a non-default value.
  if (cfg.buffer_count > 0) {
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(dec),
                                     "num-output-buffers")) {
      g_object_set(dec, "num-output-buffers",
                   static_cast<int>(cfg.buffer_count), nullptr);
    }
  }
  (void)cfg.low_latency;  // no generic v4l2 property for this; honored
                          // per-platform in later backends.

  SPDLOG_DEBUG("[VideoPlayer] v4l2_stateless: built {} decoder for '{}'",
               GST_OBJECT_NAME(gst_element_get_factory(dec)), codec);
  return dec;
}

GstElement* GenericV4L2Backend::build_converter_bin(uint64_t src_modifier,
                                                    uint64_t dst_modifier) {
  // Generic backend doesn't ship a hardware converter. When a format
  // conversion is needed the caller can fall back to GStreamer's
  // software videoconvert element; platform-specific backends
  // (Rockchip RGA, MediaTek MDP, Renesas FCP) override this to
  // return a hardware converter.
  (void)src_modifier;
  (void)dst_modifier;
  return nullptr;
}

void RegisterGenericV4L2Backend() {
  static std::atomic<bool> registered{false};
  if (registered.exchange(true)) {
    return;
  }
  BackendRegistry::Instance().RegisterBackend(
      std::make_unique<GenericV4L2Backend>());
}

}  // namespace video_player_linux
