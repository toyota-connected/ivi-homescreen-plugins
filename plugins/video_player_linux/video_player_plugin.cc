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

#include "video_player_plugin.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include <gst/pbutils/pbutils.h>
#include <gst/tag/tag.h>

#include <plugins/common/common.h>

#include "backend_generic_v4l2.h"
#include "backend_registry.h"
#include "config.h"
#include "messages.g.h"
#include "platform_detection.h"
#include "plugins/common/glib/main_loop.h"
#include "video_player.h"

namespace video_player_linux {

// static
void VideoPlayerPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarDesktop* registrar,
    FlutterDesktopPluginRegistrarRef raw) {
  auto plugin = std::make_unique<VideoPlayerPlugin>(registrar, raw);
  SetUp(registrar->messenger(), plugin.get());
  registrar->AddPlugin(std::move(plugin));
}

VideoPlayerPlugin::~VideoPlayerPlugin() = default;

VideoPlayerPlugin::VideoPlayerPlugin(flutter::PluginRegistrarDesktop* registrar,
                                     FlutterDesktopPluginRegistrarRef raw)
    : registrar_(registrar), raw_registrar_(raw) {
  // GStreamer lib only needs to be initialized once.  Calling it multiple times
  // is fine.
  gst_init(nullptr, nullptr);

  // Phase 2.6 — decoder backend registration. Must precede config load +
  // per-player Select() calls. The function is idempotent, so multiple
  // plugin instances in the same process all hit the same registry
  // entries.
  RegisterGenericV4L2Backend();

  // Detect platform first so Config::Load can materialize the matching
  // [platform.<name>] overlay. The detected profile is used as the
  // default when the TOML config leaves platform_profile as "auto".
  platform_profile_ = DetectPlatform();
  config_ =
      Config::Load([this]() { return PlatformProfileName(platform_profile_); });
  // Explicit override in the config file wins over autodetect.
  if (config_.platform_profile != "auto") {
    platform_profile_ = PlatformProfileFromName(config_.platform_profile);
  }

  spdlog::info("[VideoPlayer] Platform: {} (log_level={})",
               PlatformProfileName(platform_profile_), config_.log_level);
  for (auto* b : BackendRegistry::Instance().All()) {
    spdlog::info(
        "[VideoPlayer] Backend registered: {} (priority={}, available={})",
        b->name(), b->priority(), b->is_available());
  }

  // start the main loop if not already running
  plugin_common_glib::MainLoop::GetInstance();
}

std::optional<FlutterError> VideoPlayerPlugin::Initialize() {
  for (auto& [fst, snd] : videoPlayers) {
    snd->Dispose();
  }
  videoPlayers.clear();
  return std::nullopt;
}

static bool is_allowed_uri_scheme(const std::string& uri) {
  static constexpr std::array<const char*, 4> kAllowedSchemes = {
      "file://", "http://", "https://", "rtsp://"};
  return std::any_of(kAllowedSchemes.begin(), kAllowedSchemes.end(),
                     [&uri](const char* scheme) {
                       return uri.compare(0, strlen(scheme), scheme) == 0;
                     });
}

static bool has_header_injection(const std::string& value) {
  return value.find('\r') != std::string::npos ||
         value.find('\n') != std::string::npos ||
         value.find('\0') != std::string::npos;
}

ErrorOr<int64_t> VideoPlayerPlugin::Create(
    const std::string* asset,
    const std::string* uri,
    const flutter::EncodableMap& http_headers) {
  std::string asset_to_load;
  std::map<std::string, std::string> http_headers_;

  std::unique_ptr<VideoPlayer> player;
  if (asset && !asset->empty()) {
    asset_to_load = "file://";
    std::filesystem::path path;
    if (asset->c_str()[0] == '/') {
      path /= asset->c_str();
    } else {
      path = std::filesystem::absolute(registrar_->flutter_asset_folder());
      SPDLOG_DEBUG("path: [{}]", path.c_str());
      path /= asset->c_str();
    }
    // Ensure the path is absolute so the file:// URI is valid.
    path = std::filesystem::absolute(path);
    if (!exists(path)) {
      spdlog::error("[VideoPlayer] Asset Path does not exist. {}",
                    path.c_str());
      return FlutterError("asset_load_failed", "Asset Path does not exist.");
    }
    asset_to_load += path.c_str();
  } else if (uri && !uri->empty()) {
    if (!is_allowed_uri_scheme(*uri)) {
      spdlog::error("[VideoPlayer] Unsupported URI scheme: {}", *uri);
      return FlutterError("uri_load_failed",
                          "URI scheme not allowed. "
                          "Supported: file, http, https, rtsp");
    }
    asset_to_load = *uri;

    for (const auto& [key, value] : http_headers) {
      if (std::holds_alternative<std::string>(key) &&
          std::holds_alternative<std::string>(value)) {
        const auto& k = std::get<std::string>(key);
        const auto& v = std::get<std::string>(value);
        if (has_header_injection(k) || has_header_injection(v)) {
          spdlog::error(
              "[VideoPlayer] Rejected HTTP header with control characters");
          return FlutterError("invalid_headers",
                              "HTTP header contains invalid characters");
        }
        http_headers_[k] = v;
      }
    }
  } else {
    return FlutterError("not_implemented", "Set either an asset or a uri");
  }

  SPDLOG_DEBUG("[VideoPlayer] asset: {}", asset_to_load);

  try {
    MediaInfo info;
    if (!discover_media_info(asset_to_load.c_str(), info)) {
      return FlutterError("media_info_failed", "No playable streams found");
    }
    if (info.has_video && (info.width <= 0 || info.height <= 0 ||
                           info.width > 16384 || info.height > 16384)) {
      return FlutterError("video_info_failed", "Invalid video dimensions");
    }

    player = std::make_unique<VideoPlayer>(
        registrar_, raw_registrar_, asset_to_load, std::move(http_headers_),
        info, config_, platform_profile_);

  } catch (std::exception& e) {
    return FlutterError("uri_load_failed", e.what());
  }

  player->Init(registrar_->messenger());

  auto texture_id = player->GetTextureId();

  videoPlayers.insert(std::make_pair(texture_id, std::move(player)));

  return texture_id;
}

std::optional<FlutterError> VideoPlayerPlugin::Dispose(
    const int64_t texture_id) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  if (searchPlayer == videoPlayers.end()) {
    return FlutterError("player_not_found", "This player ID was not found");
  }
  if (searchPlayer->second->IsValid()) {
    searchPlayer->second->Dispose();
    videoPlayers.erase(texture_id);
  }

  return {};
}

std::optional<FlutterError> VideoPlayerPlugin::SetLooping(
    const int64_t texture_id,
    const bool is_looping) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  if (searchPlayer == videoPlayers.end()) {
    return FlutterError("player_not_found", "This player ID was not found");
  }
  if (searchPlayer->second->IsValid()) {
    searchPlayer->second->SetLooping(is_looping);
  }

  return {};
}

std::optional<FlutterError> VideoPlayerPlugin::SetVolume(
    const int64_t texture_id,
    const double volume) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  if (searchPlayer == videoPlayers.end()) {
    return FlutterError("player_not_found", "This player ID was not found");
  }
  if (searchPlayer->second->IsValid()) {
    searchPlayer->second->SetVolume(volume);
  }

  return {};
}

std::optional<FlutterError> VideoPlayerPlugin::SetPlaybackSpeed(
    const int64_t texture_id,
    const double speed) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  if (searchPlayer == videoPlayers.end()) {
    return FlutterError("player_not_found", "This player ID was not found");
  }
  if (searchPlayer->second->IsValid()) {
    searchPlayer->second->SetPlaybackSpeed(speed);
  }

  return {};
}

std::optional<FlutterError> VideoPlayerPlugin::Play(const int64_t texture_id) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  if (searchPlayer == videoPlayers.end()) {
    return FlutterError("player_not_found", "This player ID was not found");
  }
  if (searchPlayer->second->IsValid()) {
    searchPlayer->second->Play();
  }

  return {};
}

ErrorOr<int64_t> VideoPlayerPlugin::GetPosition(const int64_t texture_id) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  int64_t position = 0;
  if (searchPlayer != videoPlayers.end()) {
    if (const std::unique_ptr<VideoPlayer>& player = searchPlayer->second;
        player->IsValid()) {
      position = player->GetPosition();
      //      player->SendBufferingUpdate();
    }
  }
  return position;
}

std::optional<FlutterError> VideoPlayerPlugin::SeekTo(const int64_t texture_id,
                                                      const int64_t position) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  if (searchPlayer == videoPlayers.end()) {
    return FlutterError("player_not_found", "This player ID was not found");
  }
  if (searchPlayer->second->IsValid()) {
    searchPlayer->second->SeekTo(position);
  }

  return std::nullopt;
}

std::optional<FlutterError> VideoPlayerPlugin::Pause(const int64_t texture_id) {
  const auto searchPlayer = videoPlayers.find(texture_id);
  if (searchPlayer == videoPlayers.end()) {
    return FlutterError("player_not_found", "This player ID was not found");
  }
  if (searchPlayer->second->IsValid()) {
    searchPlayer->second->Pause();
  }

  return std::nullopt;
}

bool VideoPlayerPlugin::discover_media_info(const char* url, MediaInfo& info) {
  GError* err = nullptr;
  GstDiscoverer* discoverer = gst_discoverer_new(10 * GST_SECOND, &err);
  if (!discoverer) {
    spdlog::error("[VideoPlayer] Failed to create discoverer: {}",
                  err ? err->message : "unknown");
    g_clear_error(&err);
    return false;
  }

  GstDiscovererInfo* disc_info =
      gst_discoverer_discover_uri(discoverer, url, &err);
  if (!disc_info ||
      gst_discoverer_info_get_result(disc_info) != GST_DISCOVERER_OK) {
    spdlog::error("[VideoPlayer] Discovery failed for {}: {}", url,
                  err ? err->message : "unknown");
    g_clear_error(&err);
    if (disc_info)
      gst_discoverer_info_unref(disc_info);
    g_object_unref(discoverer);
    return false;
  }

  info.duration =
      static_cast<gint64>(gst_discoverer_info_get_duration(disc_info));

  if (GList* video_streams = gst_discoverer_info_get_video_streams(disc_info)) {
    auto* stream_info =
        static_cast<GstDiscovererStreamInfo*>(video_streams->data);
    if (GST_IS_DISCOVERER_VIDEO_INFO(stream_info)) {
      auto* vinfo = GST_DISCOVERER_VIDEO_INFO(stream_info);
      info.width = static_cast<int>(gst_discoverer_video_info_get_width(vinfo));
      info.height =
          static_cast<int>(gst_discoverer_video_info_get_height(vinfo));
      info.has_video = true;
      // Derive the short codec key from the stream caps. GStreamer
      // spells these "video/x-h264", "video/x-h265", "video/x-vp9",
      // "video/x-vp8", "video/x-av1", and "image/jpeg" for MJPEG.
      // Anything we don't recognize leaves info.video_codec empty, which
      // makes BackendRegistry::Select() return nullptr and lets playbin
      // auto-plug fall through unchanged.
      if (GstCaps* caps = gst_discoverer_stream_info_get_caps(stream_info)) {
        if (gst_caps_get_size(caps) > 0) {
          const GstStructure* s = gst_caps_get_structure(caps, 0);
          if (const gchar* cname = gst_structure_get_name(s)) {
            const std::string n = cname;
            if (n.rfind("video/x-", 0) == 0) {
              info.video_codec = n.substr(8);
            } else if (n == "image/jpeg") {
              info.video_codec = "mjpeg";
            }
          }
        }
        gst_caps_unref(caps);
      }
    }
    gst_discoverer_stream_info_list_free(video_streams);
  }

  if (GList* audio_streams = gst_discoverer_info_get_audio_streams(disc_info)) {
    info.has_audio = true;
    info.n_audio_streams = static_cast<gint>(g_list_length(audio_streams));
    auto* stream_info =
        static_cast<GstDiscovererStreamInfo*>(audio_streams->data);
    if (GST_IS_DISCOVERER_AUDIO_INFO(stream_info)) {
      auto* ainfo = GST_DISCOVERER_AUDIO_INFO(stream_info);
      info.audio_channels =
          static_cast<int>(gst_discoverer_audio_info_get_channels(ainfo));
      info.audio_sample_rate =
          static_cast<int>(gst_discoverer_audio_info_get_sample_rate(ainfo));
    }
    gst_discoverer_stream_info_list_free(audio_streams);
  }

  if (!info.has_video && !info.has_audio) {
    spdlog::error("[VideoPlayer] No playable streams in {}", url);
    gst_discoverer_info_unref(disc_info);
    g_object_unref(discoverer);
    return false;
  }

  // Embedded album art + text metadata.
  if (const GstTagList* tags = gst_discoverer_info_get_tags(disc_info)) {
    GstSample* image_sample = nullptr;
    if (gst_tag_list_get_sample(tags, GST_TAG_IMAGE, &image_sample) ||
        gst_tag_list_get_sample(tags, GST_TAG_PREVIEW_IMAGE, &image_sample)) {
      GstBuffer* buf = gst_sample_get_buffer(image_sample);
      GstCaps* caps = gst_sample_get_caps(image_sample);
      GstMapInfo map;
      if (buf && gst_buffer_map(buf, &map, GST_MAP_READ)) {
        // Cap embedded album art at 10 MB to defend against malformed
        // / malicious media files with absurdly large embedded images.
        // Real-world cover art is typically 200-500 KB; anything beyond
        // a few MB is almost certainly garbage we don't want to ferry
        // through the platform channel.
        constexpr size_t kMaxAlbumArtBytes =
            static_cast<size_t>(10) * 1024 * 1024;
        if (map.size > 0 && map.size <= kMaxAlbumArtBytes) {
          info.album_art.assign(map.data, map.data + map.size);
          if (caps) {
            if (const gchar* name =
                    gst_structure_get_name(gst_caps_get_structure(caps, 0))) {
              info.album_art_mime = name;
            }
          }
        } else if (map.size > kMaxAlbumArtBytes) {
          spdlog::warn(
              "[VideoPlayer] Embedded album art is {} bytes (>{} MB cap); "
              "ignoring.",
              map.size, kMaxAlbumArtBytes / (static_cast<size_t>(1024) * 1024));
        }
        gst_buffer_unmap(buf, &map);
      }
      gst_sample_unref(image_sample);
    }

    auto take_string = [&](const char* tag, std::string& dst) {
      gchar* val = nullptr;
      if (gst_tag_list_get_string(tags, tag, &val) && val) {
        dst = val;
        g_free(val);
      }
    };
    take_string(GST_TAG_TITLE, info.title);
    take_string(GST_TAG_ARTIST, info.artist);
    take_string(GST_TAG_ALBUM, info.album);
    take_string(GST_TAG_ALBUM_ARTIST, info.album_artist);
    take_string(GST_TAG_GENRE, info.genre);
    take_string(GST_TAG_AUDIO_CODEC, info.audio_codec);
    guint track_num = 0;
    if (gst_tag_list_get_uint(tags, GST_TAG_TRACK_NUMBER, &track_num)) {
      info.track_number = static_cast<int>(track_num);
    }
    // tags owned by disc_info — do not unref
  }

  SPDLOG_DEBUG(
      "[VideoPlayer] Discovered: video={} ({}x{}, codec='{}'), audio={} "
      "({}ch/{}Hz), duration={}ns, art={}B",
      info.has_video, info.width, info.height, info.video_codec, info.has_audio,
      info.audio_channels, info.audio_sample_rate, info.duration,
      info.album_art.size());

  gst_discoverer_info_unref(disc_info);
  g_object_unref(discoverer);
  return true;
}

// ────────────────────────────────────────────────────────────────────────────
// Phase 1 dispatch — audio control surface
// ────────────────────────────────────────────────────────────────────────────

ErrorOr<int64_t> VideoPlayerPlugin::GetAudioTrackCount(
    const int64_t texture_id) {
  const auto it = videoPlayers.find(texture_id);
  if (it == videoPlayers.end())
    return FlutterError("player_not_found", "This player ID was not found");
  return static_cast<int64_t>(it->second->GetAudioTrackCount());
}

std::optional<FlutterError> VideoPlayerPlugin::SetAudioTrack(
    const int64_t texture_id,
    const int64_t track_index) {
  const auto it = videoPlayers.find(texture_id);
  if (it == videoPlayers.end())
    return FlutterError("player_not_found", "This player ID was not found");
  it->second->SetAudioTrack(static_cast<int>(track_index));
  return std::nullopt;
}

std::optional<FlutterError> VideoPlayerPlugin::SetOutputChannels(
    const int64_t texture_id,
    const int64_t channels) {
  const auto it = videoPlayers.find(texture_id);
  if (it == videoPlayers.end())
    return FlutterError("player_not_found", "This player ID was not found");
  it->second->SetOutputChannels(static_cast<int>(channels));
  return std::nullopt;
}

std::optional<FlutterError> VideoPlayerPlugin::SetMute(const int64_t texture_id,
                                                       const bool mute) {
  const auto it = videoPlayers.find(texture_id);
  if (it == videoPlayers.end())
    return FlutterError("player_not_found", "This player ID was not found");
  it->second->SetMute(mute);
  return std::nullopt;
}

ErrorOr<bool> VideoPlayerPlugin::IsAudioOnly(const int64_t texture_id) {
  const auto it = videoPlayers.find(texture_id);
  if (it == videoPlayers.end())
    return FlutterError("player_not_found", "This player ID was not found");
  return it->second->IsAudioOnly();
}

// ────────────────────────────────────────────────────────────────────────────
// Phase 2 dispatch — quality & tuning
// ────────────────────────────────────────────────────────────────────────────

#define VPL_LOOKUP(id)                   \
  const auto it = videoPlayers.find(id); \
  if (it == videoPlayers.end())          \
  return FlutterError("player_not_found", "This player ID was not found")

std::optional<FlutterError> VideoPlayerPlugin::SetScaleMethod(
    const int64_t texture_id,
    const int64_t method) {
  VPL_LOOKUP(texture_id);
  it->second->SetScaleMethod(static_cast<int>(method));
  return std::nullopt;
}

std::optional<FlutterError> VideoPlayerPlugin::SetAVOffset(
    const int64_t texture_id,
    const int64_t offset_ms) {
  VPL_LOOKUP(texture_id);
  it->second->SetAVOffset(offset_ms);
  return std::nullopt;
}

std::optional<FlutterError> VideoPlayerPlugin::SetSubtitlesEnabled(
    const int64_t texture_id,
    const bool enabled) {
  VPL_LOOKUP(texture_id);
  it->second->SetSubtitlesEnabled(enabled);
  return std::nullopt;
}

ErrorOr<int64_t> VideoPlayerPlugin::GetSubtitleTrackCount(
    const int64_t texture_id) {
  VPL_LOOKUP(texture_id);
  return static_cast<int64_t>(it->second->GetSubtitleTrackCount());
}

std::optional<FlutterError> VideoPlayerPlugin::SetSubtitleTrack(
    const int64_t texture_id,
    const int64_t track_index) {
  VPL_LOOKUP(texture_id);
  it->second->SetSubtitleTrack(static_cast<int>(track_index));
  return std::nullopt;
}

std::optional<FlutterError> VideoPlayerPlugin::SetSubtitleUri(
    const int64_t texture_id,
    const std::string& uri) {
  VPL_LOOKUP(texture_id);
  // Apply the same scheme allowlist used for the main media URI so a
  // hostile caller can't smuggle a subtitle load through gio:// or
  // similar back-channels.
  if (!uri.empty() && !is_allowed_uri_scheme(uri)) {
    return FlutterError("invalid_subtitle_uri",
                        "Subtitle URI scheme not allowed. "
                        "Supported: file, http, https, rtsp");
  }
  it->second->SetSubtitleUri(uri);
  return std::nullopt;
}

std::optional<FlutterError> VideoPlayerPlugin::SetSubtitleFont(
    const int64_t texture_id,
    const std::string& font_desc) {
  VPL_LOOKUP(texture_id);
  it->second->SetSubtitleFont(font_desc);
  return std::nullopt;
}

std::optional<FlutterError> VideoPlayerPlugin::SetChannelMixPreset(
    const int64_t texture_id,
    const std::string& preset) {
  VPL_LOOKUP(texture_id);
  it->second->SetChannelMixPreset(preset);
  return std::nullopt;
}

// ────────────────────────────────────────────────────────────────────────────
// Phase 3 dispatch — premium features
// ────────────────────────────────────────────────────────────────────────────

static std::vector<double> EncodableListToDoubles(
    const flutter::EncodableList& list) {
  std::vector<double> out;
  out.reserve(list.size());
  for (const auto& v : list) {
    if (std::holds_alternative<double>(v)) {
      out.push_back(std::get<double>(v));
    } else if (std::holds_alternative<int32_t>(v)) {
      out.push_back(static_cast<double>(std::get<int32_t>(v)));
    } else if (std::holds_alternative<int64_t>(v)) {
      out.push_back(static_cast<double>(std::get<int64_t>(v)));
    } else {
      out.push_back(0.0);
    }
  }
  return out;
}

std::optional<FlutterError> VideoPlayerPlugin::SetEqualizer(
    const int64_t texture_id,
    const flutter::EncodableList& bands) {
  VPL_LOOKUP(texture_id);
  it->second->SetEqualizer(EncodableListToDoubles(bands));
  return std::nullopt;
}

std::optional<FlutterError> VideoPlayerPlugin::SetVideoBalance(
    const int64_t texture_id,
    const double brightness,
    const double contrast,
    const double saturation,
    const double hue) {
  VPL_LOOKUP(texture_id);
  it->second->SetVideoBalance(brightness, contrast, saturation, hue);
  return std::nullopt;
}

std::optional<FlutterError> VideoPlayerPlugin::SetAudioPassthrough(
    const int64_t texture_id,
    const bool enabled) {
  VPL_LOOKUP(texture_id);
  it->second->SetAudioPassthrough(enabled);
  return std::nullopt;
}

std::optional<FlutterError> VideoPlayerPlugin::SetChannelMixMatrix(
    const int64_t texture_id,
    const int64_t in_channels,
    const int64_t out_channels,
    const flutter::EncodableList& matrix) {
  VPL_LOOKUP(texture_id);
  it->second->SetChannelMixMatrix(static_cast<int>(in_channels),
                                  static_cast<int>(out_channels),
                                  EncodableListToDoubles(matrix));
  return std::nullopt;
}

#undef VPL_LOOKUP

}  // namespace video_player_linux