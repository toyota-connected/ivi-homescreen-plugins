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

#ifndef PLUGINS_VIDEO_PLAYER_LINUX_PLUGIN_H_
#define PLUGINS_VIDEO_PLAYER_LINUX_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_homescreen.h>

#include "backend_interface.h"
#include "config.h"
#include "flutter_desktop_plugin_registrar.h"
#include "messages.g.h"
#include "video_player.h"

namespace video_player_linux {

#define GSTREAMER_DEBUG 1

class VideoPlayerPlugin final : public flutter::Plugin,
                                public LinuxVideoPlayerApi {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarDesktop* registrar,
                                    FlutterDesktopPluginRegistrarRef raw);

  VideoPlayerPlugin(flutter::PluginRegistrarDesktop* registrar,
                    FlutterDesktopPluginRegistrarRef raw);

  ~VideoPlayerPlugin() override;

  // Disallow copy and assign.
  VideoPlayerPlugin(const VideoPlayerPlugin&) = delete;
  VideoPlayerPlugin& operator=(const VideoPlayerPlugin&) = delete;

  // VideoPlayerApi methods.
  std::optional<FlutterError> Initialize() override;
  ErrorOr<int64_t> Create(const std::string* asset,
                          const std::string* uri,
                          const flutter::EncodableMap& http_headers) override;
  std::optional<FlutterError> Dispose(int64_t texture_id) override;
  std::optional<FlutterError> SetLooping(int64_t texture_id,
                                         bool is_looping) override;
  std::optional<FlutterError> SetVolume(int64_t texture_id,
                                        double volume) override;
  std::optional<FlutterError> SetPlaybackSpeed(int64_t texture_id,
                                               double speed) override;
  std::optional<FlutterError> Play(int64_t texture_id) override;
  ErrorOr<int64_t> GetPosition(int64_t texture_id) override;
  std::optional<FlutterError> SeekTo(int64_t texture_id,
                                     int64_t position) override;
  std::optional<FlutterError> Pause(int64_t texture_id) override;

  // Phase 1 — audio control surface.
  ErrorOr<int64_t> GetAudioTrackCount(int64_t texture_id) override;
  std::optional<FlutterError> SetAudioTrack(int64_t texture_id,
                                            int64_t track_index) override;
  std::optional<FlutterError> SetOutputChannels(int64_t texture_id,
                                                int64_t channels) override;
  std::optional<FlutterError> SetMute(int64_t texture_id, bool mute) override;
  ErrorOr<bool> IsAudioOnly(int64_t texture_id) override;

  // Phase 2 — quality & tuning.
  std::optional<FlutterError> SetScaleMethod(int64_t texture_id,
                                             int64_t method) override;
  std::optional<FlutterError> SetAVOffset(int64_t texture_id,
                                          int64_t offset_ms) override;
  std::optional<FlutterError> SetSubtitlesEnabled(int64_t texture_id,
                                                  bool enabled) override;
  ErrorOr<int64_t> GetSubtitleTrackCount(int64_t texture_id) override;
  std::optional<FlutterError> SetSubtitleTrack(int64_t texture_id,
                                               int64_t track_index) override;
  std::optional<FlutterError> SetSubtitleUri(int64_t texture_id,
                                             const std::string& uri) override;
  std::optional<FlutterError> SetSubtitleFont(
      int64_t texture_id,
      const std::string& font_desc) override;
  std::optional<FlutterError> SetChannelMixPreset(
      int64_t texture_id,
      const std::string& preset) override;

  // Phase 3 — premium features.
  std::optional<FlutterError> SetEqualizer(
      int64_t texture_id,
      const flutter::EncodableList& bands) override;
  std::optional<FlutterError> SetVideoBalance(int64_t texture_id,
                                              double brightness,
                                              double contrast,
                                              double saturation,
                                              double hue) override;
  std::optional<FlutterError> SetAudioPassthrough(int64_t texture_id,
                                                  bool enabled) override;
  std::optional<FlutterError> SetChannelMixMatrix(
      int64_t texture_id,
      int64_t in_channels,
      int64_t out_channels,
      const flutter::EncodableList& matrix) override;

  // Process-wide accessor used by the platform-view entry point
  // (VideoPlayerView::RegisterWithRegistrar). Returns the most recently
  // constructed VideoPlayerPlugin — typical embedders host a single
  // engine and a single plugin instance, so the "most recent" and "the
  // only one" coincide. Null until the Pigeon `registerWith` fires.
  static VideoPlayerPlugin* Instance();

  // Shared builder used by both Pigeon `Create` and the platform-view
  // path to turn a Dart-supplied asset / uri / http_headers triple into
  // a ready-to-`Init` VideoPlayer. Performs URI-scheme whitelisting,
  // header-injection rejection, media discovery, and player
  // construction. Returns nullptr on any failure; error details are
  // surfaced via spdlog and, for the Pigeon path, `error_out`.
  std::unique_ptr<VideoPlayer> BuildPlayer(
      const std::string* asset,
      const std::string* uri,
      const flutter::EncodableMap& http_headers,
      FlutterError* error_out);

  [[nodiscard]] flutter::PluginRegistrarDesktop* registrar() const {
    return registrar_;
  }
  [[nodiscard]] FlutterDesktopPluginRegistrarRef raw_registrar() const {
    return raw_registrar_;
  }

  // Look up a previously-Pigeon-created player by its texture/player id.
  // Returns a non-owning pointer that's valid until the matching
  // `Dispose(id)` is received. Null when the id isn't known.
  [[nodiscard]] VideoPlayer* FindPlayer(int64_t player_id) const;

  // Adopt an externally-built (e.g. by VideoPlayerView via creation
  // params) player into the owning map under `player_id`, returning a
  // non-owning pointer for the caller to keep using. If an entry
  // already exists under that id it is replaced and the old one is
  // disposed.
  VideoPlayer* AdoptPlayer(int64_t player_id,
                           std::unique_ptr<VideoPlayer> player);

 private:
  // A list of all the video players instantiated by this plugin.
  std::map<int64_t, std::unique_ptr<VideoPlayer>> videoPlayers;

  flutter::PluginRegistrarDesktop* registrar_{};
  FlutterDesktopPluginRegistrarRef raw_registrar_{};

  // Phase 2.6 — process-wide config + detected platform. Loaded once
  // in the plugin ctor and handed by const-ref to every VideoPlayer.
  // VideoPlayer copies what it needs; the plugin-owned values remain
  // authoritative for subsequent Create() calls. Platform detection
  // is cheap (reads /sys/firmware/devicetree/base/compatible) and the
  // result is stable for the process lifetime.
  Config config_{};
  PlatformProfile platform_profile_{PlatformProfile::Auto};

  // Probes the media at [url] for video, audio, embedded album art and
  // text metadata. Returns false only when neither audio nor video streams
  // can be found (i.e. nothing playable).
  static bool discover_media_info(const char* url, MediaInfo& info);
};

}  // namespace video_player_linux

#endif  // PLUGINS_VIDEO_PLAYER_LINUX_PLUGIN_H_
