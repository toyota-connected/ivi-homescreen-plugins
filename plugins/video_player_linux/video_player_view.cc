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

#include "video_player_view.h"

#include <utility>

#include <flutter/standard_message_codec.h>

#include "plugins/common/common.h"
#include "video_player_plugin.h"

#if BUILD_COMPOSITOR
#include "view/flutter_view.h"
#endif

namespace video_player_linux {

// static
void VideoPlayerView::RegisterWithRegistrar(
    flutter::PluginRegistrar* registrar,
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
    void* platform_views_context) {
  (void)asset_directory;

  CreationParams cp;
  if (!DecodeCreationParams(params, cp)) {
    spdlog::error(
        "[VideoPlayerView] creation params missing existingPlayerId / asset / "
        "uri; refusing to construct platform view id={}",
        id);
    return;
  }

  auto* plugin = VideoPlayerPlugin::Instance();
  if (!plugin) {
    spdlog::error(
        "[VideoPlayerView] VideoPlayerPlugin not registered yet; platform "
        "view id={} constructed before pigeon registrar ran",
        id);
    return;
  }

  VideoPlayer* player = nullptr;

  if (cp.existing_player_id >= 0) {
    // Attach to a player built earlier via Pigeon `create`. The player
    // lives in the plugin's owning map; the view holds a non-owning ref.
    player = plugin->FindPlayer(cp.existing_player_id);
    if (!player) {
      spdlog::error(
          "[VideoPlayerView] existingPlayerId={} not found in plugin map; "
          "platform view id={} will not render",
          cp.existing_player_id, id);
      return;
    }
  } else {
    // Dart supplied asset/uri directly — build the player inline and
    // hand it to the plugin so control APIs (play/pause/seek) can find
    // it under the platform-view id.
    flutter::EncodableMap headers;
    for (auto& [k, v] : cp.http_headers) {
      flutter::EncodableValue ek(std::in_place_type<std::string>, k);
      flutter::EncodableValue ev(std::in_place_type<std::string>, v);
      headers[std::move(ek)] = std::move(ev);
    }

    FlutterError err("", "");
    const std::string* asset_p = cp.asset.empty() ? nullptr : &cp.asset;
    const std::string* uri_p = cp.uri.empty() ? nullptr : &cp.uri;
    auto owned = plugin->BuildPlayer(asset_p, uri_p, headers, &err);
    if (!owned) {
      spdlog::error("[VideoPlayerView] BuildPlayer failed: {}: {}", err.code(),
                    err.message());
      return;
    }
    player = plugin->AdoptPlayer(static_cast<int64_t>(id), std::move(owned));
  }

  auto view = std::make_unique<VideoPlayerView>(
      id, std::move(viewType), direction, top, left, width, height, player,
      engine, add_listener, remove_listener, platform_views_context);
  registrar->AddPlugin(std::move(view));
}

VideoPlayerView::VideoPlayerView(int32_t id,
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
                                 void* platform_views_context)
    : PlatformView(id,
                   std::move(viewType),
                   direction,
                   top,
                   left,
                   width,
                   height),
      id_(id),
      platform_views_context_(platform_views_context),
      remove_listener_(remove_listener),
      player_(player) {
#if BUILD_COMPOSITOR
  if (state && state->view_controller && state->view_controller->view) {
    state->view_controller->view->RegisterCompositorSurface(
        id_, std::shared_ptr<ICompositorSurface>(this, [](ICompositorSurface*) {
          // Aliasing deleter — PluginRegistrar owns the plugin; the
          // compositor-side shared_ptr drops on UnregisterCompositorSurface.
        }));
    SPDLOG_TRACE("[pv-trace] VideoPlayerView registered: id={} size={}x{}", id_,
                 static_cast<int32_t>(width), static_cast<int32_t>(height));
  } else {
    SPDLOG_TRACE(
        "[pv-trace] VideoPlayerView could NOT register (state/view null): "
        "id={}",
        id_);
  }
#else
  (void)state;
  spdlog::warn(
      "[VideoPlayerView] BUILD_COMPOSITOR is off; plugin will not render. "
      "Rebuild with -DBUILD_COMPOSITOR=ON.");
#endif

  add_listener(platform_views_context_, id_, &platform_view_listener_, this);
}

VideoPlayerView::~VideoPlayerView() {
  remove_listener_(platform_views_context_, id_);
  // The player stays alive — it's owned by the plugin and is destroyed on
  // Pigeon `Dispose`.
}

// static
bool VideoPlayerView::DecodeCreationParams(const std::vector<uint8_t>& bytes,
                                           CreationParams& out) {
  auto& codec = flutter::StandardMessageCodec::GetInstance();
  const auto decoded = codec.DecodeMessage(bytes.data(), bytes.size());
  const auto* map = std::get_if<flutter::EncodableMap>(decoded.get());
  if (!map) {
    return false;
  }
  for (const auto& [k, v] : *map) {
    if (!std::holds_alternative<std::string>(k)) {
      continue;
    }
    const auto& key = std::get<std::string>(k);
    if (key == "existingPlayerId") {
      if (std::holds_alternative<int64_t>(v)) {
        out.existing_player_id = std::get<int64_t>(v);
      } else if (std::holds_alternative<int32_t>(v)) {
        out.existing_player_id = std::get<int32_t>(v);
      }
    } else if ((key == "asset" || key == "uri") &&
               std::holds_alternative<std::string>(v)) {
      if (key == "asset") {
        out.asset = std::get<std::string>(v);
      } else {
        out.uri = std::get<std::string>(v);
      }
    } else if (key == "httpHeaders" &&
               std::holds_alternative<flutter::EncodableMap>(v)) {
      for (const auto& [hk, hv] : std::get<flutter::EncodableMap>(v)) {
        if (std::holds_alternative<std::string>(hk) &&
            std::holds_alternative<std::string>(hv)) {
          out.http_headers[std::get<std::string>(hk)] =
              std::get<std::string>(hv);
        }
      }
    }
  }
  return out.existing_player_id >= 0 || !out.asset.empty() || !out.uri.empty();
}

#if BUILD_COMPOSITOR

bool VideoPlayerView::OnPresent(const FlutterLayer* /*layer*/) {
  return player_ != nullptr;
}

void VideoPlayerView::OnResize(int32_t w, int32_t h) {
  width_ = w;
  height_ = h;
}

uint32_t VideoPlayerView::GetGlTextureName() const {
  return player_ ? player_->GetGlTextureName() : 0;
}

int32_t VideoPlayerView::GetGlTextureWidth() const {
  return player_ ? player_->GetGlTextureWidth() : 0;
}

int32_t VideoPlayerView::GetGlTextureHeight() const {
  return player_ ? player_->GetGlTextureHeight() : 0;
}

#endif  // BUILD_COMPOSITOR

// ── platform_view_listener callbacks ────────────────────────────────────

void VideoPlayerView::on_resize(double width, double height, void* data) {
  if (auto* self = static_cast<VideoPlayerView*>(data)) {
    self->width_ = static_cast<int32_t>(width);
    self->height_ = static_cast<int32_t>(height);
  }
}

void VideoPlayerView::on_set_direction(int32_t direction, void* data) {
  if (auto* self = static_cast<VideoPlayerView*>(data)) {
    self->direction_ = direction;
  }
}

void VideoPlayerView::on_set_offset(double left, double top, void* data) {
  if (auto* self = static_cast<VideoPlayerView*>(data)) {
    self->left_ = static_cast<int32_t>(left);
    self->top_ = static_cast<int32_t>(top);
  }
}

void VideoPlayerView::on_touch(int32_t /*action*/,
                               int32_t /*point_count*/,
                               size_t /*point_data_size*/,
                               const double* /*point_data*/,
                               void* /*data*/) {}

void VideoPlayerView::on_dispose(bool /*hybrid*/, void* /*data*/) {}

const platform_view_listener VideoPlayerView::platform_view_listener_ = {
    .resize = on_resize,
    .set_direction = on_set_direction,
    .set_offset = on_set_offset,
    .on_touch = on_touch,
    .dispose = on_dispose,
    .accept_gesture = nullptr,
    .reject_gesture = nullptr,
};

}  // namespace video_player_linux