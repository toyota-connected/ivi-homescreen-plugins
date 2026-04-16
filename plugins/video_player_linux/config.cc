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

#include "config.h"

#include <cstdlib>
#include <filesystem>
#include <optional>

#include <toml++/toml.hpp>

#include <plugins/common/common.h>

namespace video_player_linux {

namespace {

// Resolve each path in the merge order into an optional filesystem
// path. Missing files are silently OK — they just contribute nothing
// to the merge. An invalid / unreadable file currently also falls
// through silently (toml++ throws and we catch); we log it so users
// don't wonder why a typo in their config is ignored.
std::vector<std::filesystem::path> ConfigPaths() {
  std::vector<std::filesystem::path> out;

  // 1. System-wide.
  out.emplace_back("/etc/video_player_linux.toml");

  // 2. User override via XDG, with the standard fallback when the
  //    env var isn't set.
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
    out.emplace_back(std::filesystem::path(xdg) / "video_player_linux.toml");
  } else if (const char* home = std::getenv("HOME"); home && *home) {
    out.emplace_back(std::filesystem::path(home) / ".config" /
                     "video_player_linux.toml");
  }

  // 3. Single-file override.
  if (const char* over = std::getenv("VIDEO_PLAYER_LINUX_CONFIG");
      over && *over) {
    out.emplace_back(over);
  }

  return out;
}

// Apply one TOML document's non-[platform.*] top-level tables to the
// Config. Later calls win (merge order is: earlier → later).
// Platform-specific sections ([platform.<name>]) are applied by
// ApplyPlatformOverlay() after profile detection.
void ApplyDocument(const toml::table& doc, Config* cfg) {
  if (auto* platform = doc["platform"].as_table()) {
    if (auto v = (*platform)["profile"].value<std::string>(); v) {
      cfg->platform_profile = *v;
    }
  }
  if (auto* decoder = doc["decoder"].as_table()) {
    if (auto v = (*decoder)["backend"].value<std::string>(); v) {
      cfg->decoder_backend = *v;
    }
    if (auto v = (*decoder)["h264_backend"].value<std::string>(); v) {
      cfg->h264_backend = *v;
    }
    if (auto v = (*decoder)["h265_backend"].value<std::string>(); v) {
      cfg->h265_backend = *v;
    }
    if (auto v = (*decoder)["vp9_backend"].value<std::string>(); v) {
      cfg->vp9_backend = *v;
    }
    if (auto v = (*decoder)["av1_backend"].value<std::string>(); v) {
      cfg->av1_backend = *v;
    }
    if (auto v = (*decoder)["low_latency"].value<bool>(); v) {
      cfg->decoder_low_latency = *v;
    }
    if (auto v = (*decoder)["buffer_count"].value<int64_t>(); v) {
      cfg->decoder_buffer_count = static_cast<unsigned>(*v);
    }
    if (auto v = (*decoder)["require_firmware"].value<bool>(); v) {
      cfg->decoder_require_firmware = *v;
    }
  }
  if (auto* pipeline = doc["pipeline"].as_table()) {
    if (auto v = (*pipeline)["strict_caps"].value<bool>(); v) {
      cfg->pipeline_strict_caps = *v;
    }
    if (auto v = (*pipeline)["preroll_frames"].value<int64_t>(); v) {
      cfg->pipeline_preroll_frames = static_cast<unsigned>(*v);
    }
    if (auto v = (*pipeline)["drop_policy"].value<std::string>(); v) {
      cfg->pipeline_drop_policy = *v;
    }
    if (auto v = (*pipeline)["use_mdp_converter"].value<bool>(); v) {
      cfg->pipeline_use_mdp_converter = *v;
    }
  }
  if (auto* texture = doc["texture"].as_table()) {
    if (auto v = (*texture)["require_dmabuf"].value<bool>(); v) {
      cfg->texture_require_dmabuf = *v;
    }
    if (auto v = (*texture)["enable_afbc"].value<bool>(); v) {
      cfg->texture_enable_afbc = *v;
    }
    if (auto v = (*texture)["enable_ubwc"].value<bool>(); v) {
      cfg->texture_enable_ubwc = *v;
    }
    if (auto v = (*texture)["double_buffer"].value<bool>(); v) {
      cfg->texture_double_buffer = *v;
    }
    if (auto v = (*texture)["explicit_color_convert"].value<bool>(); v) {
      cfg->texture_explicit_color_convert = *v;
    }
    if (auto v = (*texture)["afbc_modifier"].value<std::string>(); v) {
      cfg->texture_afbc_modifier = *v;
    }
    if (auto v = (*texture)["ubwc_modifier"].value<std::string>(); v) {
      cfg->texture_ubwc_modifier = *v;
    }
    if (auto v = (*texture)["max_resolution"].value<std::string>(); v) {
      cfg->texture_max_resolution = *v;
    }
  }
  if (auto* hdr = doc["hdr"].as_table()) {
    if (auto v = (*hdr)["tone_map_hdr"].value<bool>(); v) {
      cfg->hdr_tone_map = *v;
    }
    if (auto v = (*hdr)["tone_map_method"].value<std::string>(); v) {
      cfg->hdr_tone_map_method = *v;
    }
    if (auto v = (*hdr)["display_peak_nits"].value<double>(); v) {
      cfg->hdr_display_peak_nits = *v;
    }
  }
  if (auto* logging = doc["logging"].as_table()) {
    if (auto v = (*logging)["level"].value<std::string>(); v) {
      cfg->log_level = *v;
    }
    if (auto v = (*logging)["log_stats_interval_sec"].value<int64_t>(); v) {
      cfg->log_stats_interval_sec = static_cast<unsigned>(*v);
    }
    if (auto v = (*logging)["log_to_file"].value<std::string>(); v) {
      cfg->log_to_file = *v;
    }
  }
}

// Apply the [platform.<profile>] section from every loaded document,
// in the same merge order as the base sections. Most platforms only
// override decoder and texture fields — the helper delegates the
// field-by-field copy to ApplyDocument() by wrapping the platform
// subtree in a synthetic root table.
void ApplyPlatformOverlay(const std::vector<toml::table>& docs,
                          const std::string& profile,
                          Config* cfg) {
  if (profile.empty() || profile == "auto") {
    return;
  }
  for (const auto& doc : docs) {
    const auto* platforms = doc["platform"].as_table();
    if (!platforms) {
      continue;
    }
    const auto* overlay = (*platforms)[profile].as_table();
    if (!overlay) {
      continue;
    }
    // ApplyDocument() expects the top-level sections at the root;
    // the TOML platform overlay uses bare section names like
    // [platform.imx8m_plus] + `decoder.h264_backend = "..."`,
    // which toml++ represents as a nested subtable. Build a flat
    // root-style table with those subtables at the top level so the
    // same field parser works.
    toml::table synthetic;
    for (const auto& [k, v] : *overlay) {
      if (v.is_table()) {
        synthetic.insert_or_assign(k, *v.as_table());
      }
    }
    ApplyDocument(synthetic, cfg);
  }
}

}  // namespace

Config Config::Load(const std::function<std::string()>& detect_platform) {
  Config cfg;
  std::vector<toml::table> docs;

  for (const auto& path : ConfigPaths()) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
      continue;
    }
    try {
      toml::table doc = toml::parse_file(path.string());
      SPDLOG_DEBUG("[VideoPlayer] Loaded config: {}", path.string());
      ApplyDocument(doc, &cfg);
      docs.push_back(std::move(doc));
    } catch (const toml::parse_error& e) {
      spdlog::warn("[VideoPlayer] Failed to parse {}: {}", path.string(),
                   e.what());
    }
  }

  // Profile resolution: if configs pinned one explicitly, honor it;
  // otherwise ask the detector (wired up in Phase 2.4). An explicit
  // "auto" falls through to detection too.
  if (cfg.platform_profile == "auto" && detect_platform) {
    cfg.platform_profile = detect_platform();
  }

  // Apply per-platform overlay from every loaded document, in
  // merge order (same as the base sections).
  ApplyPlatformOverlay(docs, cfg.platform_profile, &cfg);

  SPDLOG_DEBUG(
      "[VideoPlayer] Config: platform={} backend={} low_latency={} "
      "dmabuf_required={} afbc={} ubwc={} log_level={}",
      cfg.platform_profile, cfg.decoder_backend, cfg.decoder_low_latency,
      cfg.texture_require_dmabuf, cfg.texture_enable_afbc,
      cfg.texture_enable_ubwc, cfg.log_level);

  return cfg;
}

}  // namespace video_player_linux
