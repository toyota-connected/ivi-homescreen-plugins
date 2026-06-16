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

#include <functional>
#include <string>

namespace video_player_linux {

// Typed view of the runtime configuration. Loaded once at plugin
// startup and passed by value to anywhere that needs it (small, all
// trivially copyable except the strings).
//
// Field semantics match plan.md §2.3's TOML schema. Defaults here are
// the baseline baked into the binary; config files override them.
struct Config {
  // Platform [platform]
  // "auto" → detect via devicetree / sysfs (Phase 2.4).
  // Explicit values: "imx8m", "imx8m_plus", "imx8qm", "imx95",
  //   "rockchip_rk3568", "rockchip_rk3588", "rcar_h3", "rcar_v4h",
  //   "qualcomm_sa8155", "qualcomm_sa8295", "mediatek_mt8195",
  //   "mediatek_ct_x1", "generic_v4l2", "software".
  std::string platform_profile = "auto";

  // Decoder [decoder]
  // "auto" means "let BackendRegistry::Select pick by priority".
  // Override with a concrete backend name (matches
  // VideoDecoderBackend::name()) to force a specific decoder.
  std::string decoder_backend = "auto";
  std::string h264_backend = "auto";
  std::string h265_backend = "auto";
  std::string vp9_backend = "auto";
  std::string av1_backend = "auto";
  bool decoder_low_latency = false;
  unsigned decoder_buffer_count = 0;  // 0 = backend default
  bool decoder_require_firmware = false;

  // Pipeline [pipeline]
  bool pipeline_strict_caps = false;
  unsigned pipeline_preroll_frames = 1;
  std::string pipeline_drop_policy = "old";  // "old" or "new"
  bool pipeline_use_mdp_converter = false;

  // Texture [texture]
  bool texture_require_dmabuf = false;
  bool texture_enable_afbc = true;
  bool texture_enable_ubwc = false;
  bool texture_double_buffer = false;
  bool texture_explicit_color_convert = false;
  std::string texture_afbc_modifier;   // e.g. "arm_afbc_16x16_sparse"
  std::string texture_ubwc_modifier;   // e.g. "qcom_compressed"
  std::string texture_max_resolution;  // e.g. "3840x2160@120"

  // HDR [hdr]
  bool hdr_tone_map = true;
  std::string hdr_tone_map_method = "reinhard";  // "reinhard", "hable", "aces"
  double hdr_display_peak_nits = 500.0;

  // Logging [logging]
  std::string log_level = "info";
  unsigned log_stats_interval_sec = 0;  // 0 = disabled
  std::string log_to_file;

  // Merge-order:
  // 1. Built-in defaults (this struct's initializers)
  // 2. /etc/video_player_linux.toml
  // 3. $XDG_CONFIG_HOME/video_player_linux.toml
  //    (or $HOME/.config/video_player_linux.toml when XDG unset)
  // 4. $VIDEO_PLAYER_LINUX_CONFIG (env, single-file override)
  // 5. [platform.<detected>] section of any of the above
  //
  // `detect_platform` is invoked at most once, only if no TOML file
  // set a non-"auto" platform_profile. It should return the profile
  // key matching a [platform.<key>] section ("imx8m_plus",
  // "rockchip_rk3588", etc).
  static Config Load(
      const std::function<std::string()>& detect_platform = nullptr);
};

}  // namespace video_player_linux
