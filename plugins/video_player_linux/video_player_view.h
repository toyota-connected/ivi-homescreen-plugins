/*
 * Copyright 2020-2026 Toyota Connected North America
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

#ifndef PLUGINS_VIDEO_PLAYER_LINUX_VIEW_H_
#define PLUGINS_VIDEO_PLAYER_LINUX_VIEW_H_

#include <memory>
#include <string>
#include <vector>

#include <flutter/plugin_registrar.h>

#include "config/common.h"
#include "flutter_desktop_engine_state.h"
#include "flutter_homescreen.h"
#include "platform_views/platform_view.h"

#if BUILD_COMPOSITOR
#include "view/compositor_surface_interface.h"
#endif

#include "config.h"
#include "video_player.h"

namespace video_player_linux {

// Bridges a single `VideoPlayer` instance to the embedder's platform-view
// + compositor-surface machinery. Instantiated once per Dart-side
// `PlatformViewLink(viewType: '@views/video-player', ...)` invocation.
//
// The player itself is owned by the plugin's `videoPlayers` map and was
// constructed earlier via the Pigeon `create()` flow; the view attaches
// to it by id through creation-params. Control APIs (play/pause/seek)
// continue to route through the Pigeon map unchanged — this class adds
// no additional ownership.
class VideoPlayerView final : public flutter::Plugin,
                              public PlatformView
#if BUILD_COMPOSITOR
    ,
                              public ICompositorSurface
#endif
{
 public:
  // Entry point invoked from the plugin's C-API dispatch. Decodes the
  // creation params and attaches to the player identified by
  // `existingPlayerId` (constructed earlier via Pigeon `create()`).
  // Alternatively, when `asset` or `uri` is supplied directly and no
  // existing player matches, builds a new one and registers it with the
  // plugin under the platform-view id.
  static void RegisterWithRegistrar(flutter::PluginRegistrar* registrar,
                                    int32_t id,
                                    std::string viewType,
                                    int32_t direction,
                                    double top,
                                    double left,
                                    double width,
                                    double height,
                                    const std::vector<uint8_t>& params,
                                    const std::string& asset_directory,
                                    FlutterDesktopEngineRef engine,
                                    PlatformViewAddListener add_listener,
                                    PlatformViewRemoveListener remove_listener,
                                    void* platform_views_context);

  VideoPlayerView(int32_t id,
                  std::string viewType,
                  int32_t direction,
                  double top,
                  double left,
                  double width,
                  double height,
                  VideoPlayer* player,
                  FlutterDesktopEngineState* state,
                  PlatformViewAddListener add_listener,
                  PlatformViewRemoveListener remove_listener,
                  void* platform_views_context);

  ~VideoPlayerView() override;

  VideoPlayerView(const VideoPlayerView&) = delete;
  VideoPlayerView& operator=(const VideoPlayerView&) = delete;

#if BUILD_COMPOSITOR
  // ICompositorSurface. The engine owns backing stores; this plugin
  // publishes a single GL texture each frame, which the compositor
  // composites into the scene at the layer's offset/size.
  bool OnCreateBackingStore(const FlutterBackingStoreConfig*,
                            FlutterBackingStore*) override {
    return false;
  }
  bool OnCollectBackingStore(const FlutterBackingStore*) override {
    return true;
  }
  bool OnPresent(const FlutterLayer* layer) override;
  [[nodiscard]] FlutterPlatformViewIdentifier GetIdentifier() const override {
    return id_;
  }
  void OnResize(int32_t w, int32_t h) override;

  [[nodiscard]] uint32_t GetGlTextureName() const override;
  [[nodiscard]] int32_t GetGlTextureWidth() const override;
  [[nodiscard]] int32_t GetGlTextureHeight() const override;
  [[nodiscard]] bool TextureIsTopFirst() const override { return true; }
#endif

 private:
  int32_t id_;
  void* platform_views_context_;
  PlatformViewRemoveListener remove_listener_;

  // Non-owning. The player lives in VideoPlayerPlugin::videoPlayers and
  // is destroyed on Pigeon `Dispose()`.
  VideoPlayer* player_;

  // Decode the creation-params blob (standard-codec EncodableMap).
  // Recognizes `existingPlayerId` (int) for attaching to a Pigeon-created
  // player, plus `asset` / `uri` / `httpHeaders` for the alternative
  // build-new-player flow. Returns false when no valid combination is
  // present.
  struct CreationParams {
    int64_t existing_player_id{-1};
    std::string asset;
    std::string uri;
    std::map<std::string, std::string> http_headers;
  };
  static bool DecodeCreationParams(const std::vector<uint8_t>& bytes,
                                   CreationParams& out);

  static void on_resize(double width, double height, void* data);
  static void on_set_direction(int32_t direction, void* data);
  static void on_set_offset(double left, double top, void* data);
  static void on_touch(int32_t action,
                       int32_t point_count,
                       size_t point_data_size,
                       const double* point_data,
                       void* data);
  static void on_dispose(bool hybrid, void* data);
  static const struct platform_view_listener platform_view_listener_;
};

}  // namespace video_player_linux

#endif  // PLUGINS_VIDEO_PLAYER_LINUX_VIEW_H_