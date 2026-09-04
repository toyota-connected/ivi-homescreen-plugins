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

#include "video_player.h"

#include <flutter/event_channel.h>
#include <flutter/event_stream_handler.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/plugin_registrar_homescreen.h>
#include <flutter/standard_method_codec.h>
#include <memory>

#include <atomic>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <gst/audio/audio.h>
#include <gst/tag/tag.h>

#include <backend/backend.h>
#include <plugins/common/common.h>
#include <utility>

#define GSTREAMER_DEBUG 0

namespace video_player_linux {

// Serialize access to the shared EGL texture context across all players.
// EGL contexts can only be current on one thread at a time.
static std::mutex g_texture_context_mutex;

typedef enum {
  GST_PLAY_FLAG_AUDIO = 1 << 0,
  GST_PLAY_FLAG_VIDEO = 1 << 1,
  GST_PLAY_FLAG_TEXT = 1 << 2
} GstPlayFlags;

// Audio-only synthetic player IDs start at 0x7F000000 to avoid colliding
// with GL texture IDs (which are small positive integers).
static std::atomic<int64_t> g_audio_player_id_counter{0x7F000000};

VideoPlayer::VideoPlayer(flutter::PluginRegistrarDesktop* registrar,
                         std::string uri,
                         std::map<std::string, std::string> http_headers,
                         const MediaInfo& info)
    : m_registrar(registrar),
      uri_(std::move(uri)),
      http_headers_(std::move(http_headers)),
      width_(info.width),
      height_(info.height),
      duration_(info.duration),
      has_video_(info.has_video),
      initial_album_art_(info.album_art),
      initial_album_art_mime_(info.album_art_mime),
      title_(info.title),
      artist_(info.artist),
      album_(info.album),
      album_artist_(info.album_artist),
      genre_(info.genre),
      track_number_(info.track_number),
      audio_codec_(info.audio_codec),
      audio_channels_(info.audio_channels),
      audio_sample_rate_(info.audio_sample_rate) {
  IHS_DEBUG(
      "[VideoPlayer] uri: {}, http_headers: {}, size: {} x {}, duration: {}, "
      "has_video: {}",
      uri_.c_str(), http_headers_.size(), width_, height_, duration_,
      has_video_);

  gst_video_info_init(&info_);

  // Validate dimensions only when video is expected.
  if (has_video_ && (width_ <= 0 || height_ <= 0)) {
    ihs::log::error("[VideoPlayer] Invalid dimensions: {}x{}", width_, height_);
    m_valid = false;
    return;
  }

  std::lock_guard buffer_lock(buffer_mutex_);

  if (has_video_) {
    /// Setup OpenGL
    {
      std::lock_guard ctx_lock(g_texture_context_mutex);
      m_registrar->texture_registrar()->TextureMakeCurrent();
      // Double-buffer can be enabled via env var for flicker-sensitive cases.
      const bool double_buf =
          std::getenv("VIDEO_PLAYER_DOUBLE_BUFFER") != nullptr;
      shader_ = std::make_unique<nv12::Shader>(width_, height_, double_buf);
      m_texture_id = shader_->textureId;
      m_registrar->texture_registrar()->TextureClearCurrent();
    }

    /// Setup GL Texture 2D
    m_descriptor.struct_size = sizeof(FlutterDesktopGpuSurfaceDescriptor);
    m_descriptor.handle = &m_texture_id;
    m_descriptor.width = static_cast<size_t>(width_);
    m_descriptor.height = static_cast<size_t>(height_);
    m_descriptor.visible_width = static_cast<size_t>(width_);
    m_descriptor.visible_height = static_cast<size_t>(height_);
    m_descriptor.format = kFlutterDesktopPixelFormatRGBA8888;
    m_descriptor.release_callback = [](void* /* release_context */) {};
    m_descriptor.release_context = this;

    gpu_surface_texture_ = std::make_unique<flutter::GpuSurfaceTexture>(
        kFlutterDesktopGpuSurfaceTypeGlTexture2D,
        [&](size_t /* width */,
            size_t /* height */) -> const FlutterDesktopGpuSurfaceDescriptor* {
          return &m_descriptor;
        });

    flutter::TextureVariant texture = *gpu_surface_texture_;
    m_registrar->texture_registrar()->RegisterTexture(&texture);
    IHS_DEBUG("[VideoPlayer] Registered texture_id={}", m_texture_id);
  } else {
    // Audio-only: synthesise a player ID outside the GL texture ID range.
    m_texture_id = g_audio_player_id_counter.fetch_add(1);
    IHS_DEBUG("[VideoPlayer] Audio-only player_id={}", m_texture_id);
  }

  /// Setup GST Pipeline

  context_ = g_main_context_get_thread_default();

  playbin_ = gst_element_factory_make("playbin", nullptr);
  if (!playbin_) {
    ihs::log::error("[VideoPlayer] Failed to create playbin element");
    m_valid = false;
    return;
  }
  g_object_set(playbin_, "uri", uri_.c_str(), nullptr);

  if (!http_headers_.empty()) {
    GstStructure* extraHeaders = gst_structure_new_empty("extra-headers");
    for (const auto& [key, value] : http_headers_) {
      gst_structure_set(extraHeaders, key.c_str(), G_TYPE_STRING, value.c_str(),
                        nullptr);
      IHS_DEBUG("extra-header: {}:{}", key, value);
    }
    g_object_set(playbin_, "extra-headers", extraHeaders, nullptr);
    gst_structure_free(extraHeaders);
  }

  // Connect source-setup so we can configure souphttpsrc / rtspsrc as soon
  // as playbin instantiates the source element. The handler is safe across
  // source types because it probes for properties before setting them.
  source_setup_id_ =
      g_signal_connect(playbin_, "source-setup",
                       reinterpret_cast<GCallback>(OnSourceSetup), this);

  // Custom audio sink bin: audioconvert → audioresample → capsfilter → sink.
  // Even without app-level controls this improves codec/sample-rate
  // compatibility and is a prerequisite for channel/EQ work.
  if (GstElement* audio_bin = BuildAudioSinkBin()) {
    audio_bin_ = audio_bin;
    g_object_set(playbin_, "audio-sink", audio_bin_, nullptr);
    audio_upgraded_ = true;
  } else {
    ihs::log::warn("[VideoPlayer] Audio sink bin build failed; using fakesink");
    GstElement* fake = gst_element_factory_make("fakesink", "fake-audio");
    if (fake) {
      g_object_set(fake, "sync", TRUE, nullptr);
      g_object_set(playbin_, "audio-sink", fake, nullptr);
    }
  }

  gint flags = 0;
  g_object_get(playbin_, "flags", &flags, nullptr);
  // Always enable both AUDIO and VIDEO flags. For audio-only sources the
  // VIDEO flag is harmless (uridecodebin won't find a video pad), but
  // clearing it has been observed to leave playsink in a state where it
  // also refuses the audio pad ("1 pending" forever, then GST_FLOW_NOT_LINKED
  // on the source). Provide a fake video-sink below so playbin never tries
  // to instantiate autovideosink either.
  flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO;
  flags &= ~GST_PLAY_FLAG_TEXT;
  g_object_set(playbin_, "flags", flags, nullptr);

  // Audio-only: install a bare fakesink as video-sink so playbin never
  // instantiates its autovideosink default. Without this, playbin's
  // playsink will refuse to link the audio pad on headless / embedded
  // contexts where autovideosink can't grab a real display surface.
  if (!has_video_) {
    GstElement* fake_video = gst_element_factory_make("fakesink", "fake-video");
    if (fake_video) {
      g_object_set(fake_video, "sync", TRUE, "async", FALSE, nullptr);
      g_object_set(playbin_, "video-sink", fake_video, nullptr);
    }
  }

  int connection_speed = 10000;
  if (const char* env = std::getenv("VIDEO_PLAYER_CONNECTION_SPEED")) {
    char* end = nullptr;
    const long val = std::strtol(env, &end, 10);
    if (end != env && val > 0 && val <= INT_MAX) {
      connection_speed = static_cast<int>(val);
    }
  }
  g_object_set(playbin_, "connection-speed", connection_speed, nullptr);
  g_object_set(playbin_, "volume", volume_, nullptr);

  // Buffer tuning (Phase 2). Defaults are GStreamer's; only override on
  // explicit env vars to keep behaviour stable for unconstrained targets.
  if (const char* env = std::getenv("VIDEO_PLAYER_BUFFER_SIZE")) {
    char* end = nullptr;
    const long val = std::strtol(env, &end, 10);
    if (end != env && val > 0) {
      g_object_set(playbin_, "buffer-size", static_cast<int>(val), nullptr);
    }
  }
  if (const char* env = std::getenv("VIDEO_PLAYER_BUFFER_DURATION")) {
    char* end = nullptr;
    const long long val = std::strtoll(env, &end, 10);
    if (end != env && val > 0) {
      g_object_set(playbin_, "buffer-duration",
                   static_cast<gint64>(val) * GST_SECOND, nullptr);
    }
  }

  if (has_video_) {
    sink_ = gst_element_factory_make("fakesink", nullptr);
    if (!sink_) {
      ihs::log::error("[VideoPlayer] Failed to create fakesink element");
      m_valid = false;
      return;
    }
    g_object_set(sink_, "sync", TRUE, nullptr);
    g_object_set(sink_, "signal-handoffs", TRUE, nullptr);
    g_object_set(sink_, "can-activate-pull", TRUE, nullptr);
    handoff_handler_id_ = g_signal_connect(
        sink_, "handoff", reinterpret_cast<GCallback>(handoff_handler), this);

    video_convert_ = gst_element_factory_make("videoconvert", nullptr);
    if (!video_convert_) {
      ihs::log::error("[VideoPlayer] Failed to create videoconvert element");
      m_valid = false;
      return;
    }

    GstCaps* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
                                        "NV12", nullptr);

    video_scale_ = gst_element_factory_make("videoscale", nullptr);
    if (!video_scale_) {
      ihs::log::error("[VideoPlayer] Failed to create videoscale element");
      m_valid = false;
      return;
    }
    // Phase 2: scaling algorithm via env var. 1=bilinear (default).
    int scale_method = 1;
    if (const char* env = std::getenv("VIDEO_PLAYER_SCALE_METHOD")) {
      char* end = nullptr;
      const long v = std::strtol(env, &end, 10);
      if (end != env && v >= 0 && v <= 9)
        scale_method = static_cast<int>(v);
    }
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(video_scale_),
                                     "method")) {
      g_object_set(video_scale_, "method", scale_method, nullptr);
    }

    GstCaps* scale =
        gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, width_,
                            "height", G_TYPE_INT, height_, nullptr);

    pipeline_ = gst_bin_new(nullptr);

    gst_bin_add_many(GST_BIN(pipeline_), video_convert_, video_scale_, sink_,
                     nullptr);

    if (!gst_element_link_filtered(video_convert_, video_scale_, caps)) {
      ihs::log::error(
          "[VideoPlayer] Failed to link videoconvert with videoscale");
    }
    gst_caps_unref(caps);

    if (!gst_element_link_filtered(video_scale_, sink_, scale)) {
      ihs::log::error("[VideoPlayer] Failed to link videoscale with fakesink");
    }
    gst_caps_unref(scale);

    // Ghost pad so playbin can link its decoded output into this bin
    GstPad* pad = gst_element_get_static_pad(video_convert_, "sink");
    GstPad* ghost_pad = gst_ghost_pad_new("sink", pad);
    gst_pad_set_active(ghost_pad, TRUE);
    gst_element_add_pad(pipeline_, ghost_pad);
    gst_object_unref(pad);

    g_object_set(playbin_, "video-sink", pipeline_, nullptr);
  }

  bus_ = gst_element_get_bus(playbin_);
  GSource* bus_source = gst_bus_create_watch(bus_);
  // GstBus source dispatch calls the callback with (GstBus*, GstMessage*,
  // gpointer) arguments matching gst_bus_async_signal_func's signature.
  // Cast through void* to avoid -Wcast-function-type-mismatch.
  g_source_set_callback(bus_source,
                        reinterpret_cast<GSourceFunc>(
                            reinterpret_cast<void*>(gst_bus_async_signal_func)),
                        nullptr, nullptr);
  g_source_attach(bus_source, context_);
  g_source_unref(bus_source);
  on_bus_msg_id_ = g_signal_connect(
      bus_, "message", reinterpret_cast<GCallback>(OnBusMessage), this);
}

VideoPlayer::~VideoPlayer() {
  if (m_valid) {
    Dispose();
  }
}

void VideoPlayer::Dispose() {
  IHS_DEBUG("[VideoPlayer] Dispose");
  m_valid = false;
  is_initialized_ = false;
  StopAudioMonitor();

  std::lock_guard buffer_lock(buffer_mutex_);

  if (bus_ && on_bus_msg_id_) {
    g_signal_handler_disconnect(G_OBJECT(bus_), on_bus_msg_id_);
  }
  if (sink_ && handoff_handler_id_) {
    g_signal_handler_disconnect(G_OBJECT(sink_), handoff_handler_id_);
  }
  if (playbin_ && source_setup_id_) {
    g_signal_handler_disconnect(G_OBJECT(playbin_), source_setup_id_);
    source_setup_id_ = 0;
  }

  if (playbin_) {
    gst_element_set_state(playbin_, GST_STATE_NULL);
  }

  if (has_video_) {
    // Ensure no in-flight handoff callback is using the shader
    std::lock_guard gst_lock(gst_mutex_);
    if (shader_) {
      std::lock_guard ctx_lock(g_texture_context_mutex);
      m_registrar->texture_registrar()->TextureMakeCurrent();
      shader_.reset();
      m_registrar->texture_registrar()->TextureClearCurrent();
    }
  }

  if (has_video_ && m_texture_id != 0) {
    IHS_DEBUG("[VideoPlayer] Unregistering texture_id={}", m_texture_id);
    m_registrar->texture_registrar()->UnregisterTexture(m_texture_id);
    m_texture_id = 0;
  }

  if (bus_) {
    gst_object_unref(bus_);
    bus_ = nullptr;
  }

  {
    std::lock_guard event_lock(event_mutex_);
    event_sink_ = nullptr;
  }
  event_channel_ = nullptr;
}

void VideoPlayer::SetLooping(const bool isLooping) {
  IHS_DEBUG("[VideoPlayer] SetLooping: {}", is_looping_.load());
  is_looping_ = isLooping;
}

void VideoPlayer::SetVolume(const double volume) {
  IHS_DEBUG("[VideoPlayer] SetVolume: {}", volume);
  volume_ = volume;
  g_object_set(playbin_, "volume", volume, nullptr);
}

void VideoPlayer::SetPlaybackSpeed(const double playbackSpeed) {
  // Store the desired rate so it can be applied when the pipeline is ready
  pending_rate_.store(playbackSpeed);

  // Seek events require the pipeline to be in PAUSED or PLAYING state
  GstState state;
  gst_element_get_state(playbin_, &state, nullptr, 0);
  if (state < GST_STATE_PAUSED) {
    IHS_DEBUG(
        "[VideoPlayer] Deferring playback speed {} until pipeline is ready",
        playbackSpeed);
    return;
  }

  ApplyPlaybackSpeed();
}

void VideoPlayer::Play() {
  target_state_ = GST_STATE_PLAYING;
  const GstStateChangeReturn ret =
      gst_element_set_state(playbin_, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    ihs::log::error("[VideoPlayer] Failed to set state GST_STATE_PLAYING.");
  } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
    is_live_ = TRUE;
    IHS_DEBUG("[VideoPlayer] Pipeline is live");
  }
}

void VideoPlayer::Pause() {
  GstState state;
  gst_element_get_state(playbin_, &state, nullptr, GST_SECOND);
  if (state != GST_STATE_NULL) {
    target_state_ = GST_STATE_PAUSED;
    const GstStateChangeReturn ret =
        gst_element_set_state(playbin_, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
      std::lock_guard event_lock(event_mutex_);
      if (event_sink_) {
        event_sink_->Error("[VideoPlayer] Unable to Pause Transport.");
      }
      return;
    }
    IHS_DEBUG("[VideoPlayer] Transport Paused.");
  }
}

int64_t VideoPlayer::GetPosition() {
  gint64 pos = 0;
  if (gst_element_query_position(playbin_, GST_FORMAT_TIME, &pos)) {
    position_ = pos;
    IHS_TRACE("[VideoPlayer] Position: {}", pos);
  }
  const gint64 current = position_.load();
  return current >= 0 ? current / GST_MSECOND : 0;
}

void VideoPlayer::SendBufferingUpdate() {
  std::lock_guard event_lock(event_mutex_);
  if (!event_sink_) {
    return;
  }
  auto values = flutter::EncodableList();

  GstQuery* query = gst_query_new_buffering(GST_FORMAT_TIME);
  if (gst_element_query(playbin_, query)) {
    const guint n_ranges = gst_query_get_n_buffering_ranges(query);
    for (guint i = 0; i < n_ranges; i++) {
      gint64 start = 0, stop = 0;
      if (gst_query_parse_nth_buffering_range(query, i, &start, &stop)) {
        values.emplace_back(
            std::in_place_type<flutter::EncodableList>,
            flutter::EncodableList{
                flutter::EncodableValue(std::in_place_type<int64_t>,
                                        start / GST_MSECOND),
                flutter::EncodableValue(std::in_place_type<int64_t>,
                                        stop / GST_MSECOND)});
      }
    }
  }
  gst_query_unref(query);

  auto res = flutter::EncodableMap(
      {{flutter::EncodableValue("event"),
        flutter::EncodableValue("bufferingUpdate")},
       {flutter::EncodableValue("values"),
        flutter::EncodableValue(std::in_place_type<flutter::EncodableList>,
                                std::move(values))}});
  event_sink_->Success(flutter::EncodableValue(
      std::in_place_type<flutter::EncodableMap>, std::move(res)));
}

void VideoPlayer::SeekTo(const int64_t seek) {
  const gint64 position = seek * GST_MSECOND;
  IHS_DEBUG("[VideoPlayer] SeekTo: {} -> {}", seek, position);

  if (!gst_element_seek_simple(
          playbin_, GST_FORMAT_TIME,
          static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH |
                                    GST_SEEK_FLAG_KEY_UNIT),
          position)) {
    ihs::log::error("[VideoPlayer] Seek Failed");
  }
  gint64 pos;
  gst_element_query_position(playbin_, GST_FORMAT_TIME, &pos);
  position_ = pos;
  IHS_DEBUG("[VideoPlayer] SeekTo: {} -> {}", seek, pos);

  // gst_element_seek_simple resets the segment rate to 1.0. If the user
  // had set a non-default rate, re-apply it now so dragging the scrubber
  // doesn't silently drop the rate back to 1×.
  if (pending_rate_.load() != 1.0) {
    rate_.store(
        -2.0);  // sentinel; forces ApplyPlaybackSpeed to send a real seek
    ApplyPlaybackSpeed();
  }
}

bool VideoPlayer::IsValid() {
  return m_valid;
}

void VideoPlayer::Init(flutter::BinaryMessenger* messenger) {
  if (is_initialized_) {
    return;
  }

  IHS_DEBUG("[VideoPlayer] Init");
  event_channel_ = std::make_unique<flutter::EventChannel<>>(
      messenger,
      std::string("flutter.io/videoPlayer/videoEvents") +
          std::to_string(m_texture_id),
      &flutter::StandardMethodCodec::GetInstance());

  event_channel_->SetStreamHandler(
      std::make_unique<flutter::StreamHandlerFunctions<>>(
          [this](const flutter::EncodableValue* /* arguments */,
                 std::unique_ptr<flutter::EventSink<>>&& events)
              -> std::unique_ptr<flutter::StreamHandlerError<>> {
            {
              std::lock_guard event_lock(event_mutex_);
              event_sink_ = std::move(events);
            }
            // The Dart-side `play()` and the EventChannel `listen` arrive
            // on different platform-channel transports and may be
            // processed in either order. If `play()` ran first the
            // pipeline already fired `SendInitialized` while
            // `event_sink_` was null and the event was dropped. Replay
            // the current state to the freshly-attached sink so the
            // Dart MiniController's `initialize()` future doesn't hang
            // forever.
            if (is_initialized_) {
              SendInitialized();
              SendMediaMetadata();
              if (!initial_album_art_.empty()) {
                SendAlbumArt(initial_album_art_, initial_album_art_mime_);
              }
              SendAudioInfo();
            }
            return nullptr;
          },
          [this](const flutter::EncodableValue* /* arguments */)
              -> std::unique_ptr<flutter::StreamHandlerError<>> {
            std::lock_guard event_lock(event_mutex_);
            event_sink_ = nullptr;
            return nullptr;
          }));
}

void VideoPlayer::SetBuffering(const bool buffering) {
  std::lock_guard event_lock(event_mutex_);
  if (event_sink_) {
    IHS_DEBUG("[VideoPlayer] SetBuffering: {}", buffering);
    auto res = flutter::EncodableMap(
        {{flutter::EncodableValue("event"),
          flutter::EncodableValue(buffering ? "bufferingStart"
                                            : "bufferingEnd")}});
    event_sink_->Success(flutter::EncodableValue(
        std::in_place_type<flutter::EncodableMap>, std::move(res)));
  }
}

void VideoPlayer::ApplyPlaybackSpeed() {
  const double pending = pending_rate_.load();
  if (pending == rate_.load()) {
    return;
  }

  const auto playbackSpeed = pending;
  gint64 pos = 0;
  if (!gst_element_query_position(playbin_, GST_FORMAT_TIME, &pos)) {
    pos = position_.load();
  }
  // Canonical rate-only seek: SET start at the current position, NONE for
  // the stop. Earlier we passed END/0 for the stop which on some sinks
  // collapses the segment to zero length and silently squashes the rate
  // change.
  GstEvent* seek_event = nullptr;
  if (playbackSpeed > 0) {
    seek_event = gst_event_new_seek(
        playbackSpeed, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        GST_SEEK_TYPE_SET, pos, GST_SEEK_TYPE_NONE,
        static_cast<gint64>(GST_CLOCK_TIME_NONE));
  } else {
    // Reverse playback: walk from start to the current position.
    seek_event = gst_event_new_seek(
        playbackSpeed, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, pos);
  }

  if (!gst_element_send_event(playbin_, seek_event)) {
    ihs::log::error("[VideoPlayer] Failed to set playback speed: {}",
                    playbackSpeed);
    return;
  }
  rate_.store(playbackSpeed);
  IHS_DEBUG("[VideoPlayer] Playback speed -> {}", playbackSpeed);
}

gboolean VideoPlayer::OnAudioRecovery(gpointer user_data) {
  auto* self = static_cast<VideoPlayer*>(user_data);
  IHS_DEBUG("[VideoPlayer] Audio recovery: restarting without audio");

  gint flags = 0;
  g_object_get(self->playbin_, "flags", &flags, nullptr);
  flags &= ~GST_PLAY_FLAG_AUDIO;
  g_object_set(self->playbin_, "flags", flags, nullptr);

  self->is_buffering_ = false;
  self->is_initialized_ = false;
  self->sent_initialized_ = false;

  // Pipeline restart resets segment rate; force ApplyPlaybackSpeed to
  // re-seek when we reach PLAYING again.
  self->rate_.store(-2.0);
  gst_element_set_state(self->playbin_, GST_STATE_NULL);
  gst_element_set_state(self->playbin_,
                        static_cast<GstState>(self->target_state_.load()));
  return G_SOURCE_REMOVE;
}

void VideoPlayer::StartAudioMonitor() {
  if (udev_mon_)
    return;  // already running

  udev_ = udev_new();
  if (!udev_)
    return;

  udev_mon_ = udev_monitor_new_from_netlink(udev_, "udev");
  if (!udev_mon_) {
    udev_unref(udev_);
    udev_ = nullptr;
    return;
  }

  udev_monitor_filter_add_match_subsystem_devtype(udev_mon_, "sound", nullptr);
  udev_monitor_enable_receiving(udev_mon_);

  int fd = udev_monitor_get_fd(udev_mon_);
  udev_channel_ = g_io_channel_unix_new(fd);
  udev_watch_id_ = g_io_add_watch(udev_channel_, G_IO_IN, OnUdevEvent, this);

  IHS_DEBUG("[VideoPlayer] Audio hotplug monitor started");

  // Check if a PCM device already exists (device may have appeared
  // before the monitor was set up).
  struct udev_enumerate* enumerate = udev_enumerate_new(udev_);
  udev_enumerate_add_match_subsystem(enumerate, "sound");
  udev_enumerate_scan_devices(enumerate);
  struct udev_list_entry* entry;
  udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate)) {
    const char* path = udev_list_entry_get_name(entry);
    struct udev_device* dev = udev_device_new_from_syspath(udev_, path);
    if (dev) {
      const char* sysname = udev_device_get_sysname(dev);
      if (sysname && strncmp(sysname, "pcmC", 4) == 0) {
        IHS_DEBUG("[VideoPlayer] Audio device already present: {}", sysname);
        udev_device_unref(dev);
        // Schedule upgrade immediately. Track the GSource id so we can
        // cancel it in StopAudioMonitor if Dispose runs first.
        if (!audio_upgrade_idle_id_) {
          GSource* idle = g_idle_source_new();
          g_source_set_callback(idle, OnAudioUpgrade, this, nullptr);
          audio_upgrade_idle_id_ = g_source_attach(idle, context_);
          g_source_unref(idle);
        }
        break;
      }
      udev_device_unref(dev);
    }
  }
  udev_enumerate_unref(enumerate);
}

void VideoPlayer::StopAudioMonitor() {
  // Cancel any pending OnAudioUpgrade idle source first so it can't fire
  // on a half-torn-down player.
  if (audio_upgrade_idle_id_) {
    g_source_remove(audio_upgrade_idle_id_);
    audio_upgrade_idle_id_ = 0;
  }
  if (udev_watch_id_) {
    g_source_remove(udev_watch_id_);
    udev_watch_id_ = 0;
  }
  if (udev_channel_) {
    g_io_channel_unref(udev_channel_);
    udev_channel_ = nullptr;
  }
  if (udev_mon_) {
    udev_monitor_unref(udev_mon_);
    udev_mon_ = nullptr;
  }
  if (udev_) {
    udev_unref(udev_);
    udev_ = nullptr;
  }
}

gboolean VideoPlayer::OnUdevEvent(GIOChannel* /*channel*/,
                                  GIOCondition /*cond*/,
                                  gpointer user_data) {
  auto* self = static_cast<VideoPlayer*>(user_data);
  struct udev_device* dev = udev_monitor_receive_device(self->udev_mon_);
  if (!dev)
    return G_SOURCE_CONTINUE;

  const char* action = udev_device_get_action(dev);
  const char* sysname = udev_device_get_sysname(dev);

  if (action && sysname && strcmp(action, "add") == 0 &&
      strncmp(sysname, "pcmC", 4) == 0 && !self->audio_upgraded_ &&
      !self->audio_upgrade_idle_id_) {
    IHS_DEBUG("[VideoPlayer] Audio device hotplugged: {}", sysname);
    // Schedule the upgrade on an idle callback to avoid reentrancy.
    // Track the source id so it can be cancelled if Dispose runs first.
    GSource* idle = g_idle_source_new();
    g_source_set_callback(idle, OnAudioUpgrade, self, nullptr);
    self->audio_upgrade_idle_id_ = g_source_attach(idle, self->context_);
    g_source_unref(idle);
  }

  udev_device_unref(dev);
  return G_SOURCE_CONTINUE;
}

gboolean VideoPlayer::OnAudioUpgrade(gpointer user_data) {
  auto* self = static_cast<VideoPlayer*>(user_data);
  // Clear the tracked source id first — once we return REMOVE, the
  // GSource is gone, and StopAudioMonitor must not call g_source_remove
  // on a stale id.
  self->audio_upgrade_idle_id_ = 0;
  if (self->audio_upgraded_ || !self->m_valid) {
    return G_SOURCE_REMOVE;
  }

  // Try to create a real audio sink.  If it can reach READY, swap it in.
  GstElement* real_sink = gst_element_factory_make("autoaudiosink", nullptr);
  if (!real_sink) {
    ihs::log::warn("[VideoPlayer] Audio upgrade: autoaudiosink unavailable");
    return G_SOURCE_REMOVE;
  }

  if (gst_element_set_state(real_sink, GST_STATE_READY) ==
      GST_STATE_CHANGE_FAILURE) {
    gst_element_set_state(real_sink, GST_STATE_NULL);
    gst_object_unref(real_sink);
    ihs::log::warn("[VideoPlayer] Audio upgrade: sink not ready yet");
    return G_SOURCE_REMOVE;  // udev will trigger us again on next hotplug
  }
  gst_element_set_state(real_sink, GST_STATE_NULL);

  // Hot-swap: pause playbin, swap audio-sink, resume
  IHS_DEBUG("[VideoPlayer] Audio upgrade: swapping to real audio sink");
  GstState cur_state = GST_STATE_NULL;
  gst_element_get_state(self->playbin_, &cur_state, nullptr, 0);

  // READY round-trip resets segment rate; invalidate cache so the next
  // PLAYING transition re-applies pending_rate_.
  self->rate_.store(-2.0);
  gst_element_set_state(self->playbin_, GST_STATE_READY);
  gst_element_get_state(self->playbin_, nullptr, nullptr, 2 * GST_SECOND);

  g_object_set(self->playbin_, "audio-sink", real_sink, nullptr);

  gst_element_set_state(self->playbin_, cur_state);
  self->audio_upgraded_ = true;
  self->StopAudioMonitor();
  IHS_DEBUG("[VideoPlayer] Audio upgrade: done");
  return G_SOURCE_REMOVE;
}

void VideoPlayer::OnPlaybackEnded() {
  if (is_looping_) {
    IHS_DEBUG("[VideoPlayer] Looping: restarting pipeline");
    is_buffering_ = false;
    // The READY round-trip resets the pipeline's segment rate to 1.0.
    // Invalidate our cached rate so the next ApplyPlaybackSpeed call
    // (fired from OnMediaStateChange on PLAYING) re-sends the user's
    // chosen pending_rate_.
    rate_.store(-2.0);
    gst_element_set_state(playbin_, GST_STATE_READY);
    gst_element_set_state(playbin_, GST_STATE_PLAYING);
    return;
  }
  std::lock_guard event_lock(event_mutex_);
  if (event_sink_) {
    IHS_DEBUG("[VideoPlayer] OnPlaybackEnded");
    auto res = flutter::EncodableMap({{flutter::EncodableValue("event"),
                                       flutter::EncodableValue("completed")}});
    event_sink_->Success(flutter::EncodableValue(
        std::in_place_type<flutter::EncodableMap>, std::move(res)));
  }
}

void VideoPlayer::OnMediaInitialized() {}

void VideoPlayer::OnMediaStateChange(const GstState state) {
  if (state == GST_STATE_NULL) {
    SetBuffering(true);
    SendBufferingUpdate();
  } else {
    SetBuffering(false);

    if (state == GST_STATE_PLAYING) {
      IHS_DEBUG("[VideoPlayer] message state changed, start playing {}",
                m_texture_id);
      audio_recovery_ = false;
      if (has_video_) {
        prepare(this);
      }
      is_initialized_ = true;
      ApplyPlaybackSpeed();

      // For audio-only there are no video frames, so the handoff path will
      // never fire SendInitialized — emit it directly on first PLAYING.
      if (!has_video_ && !sent_initialized_) {
        sent_initialized_ = true;
        SendInitialized();
      }
      // Forward seeded discoverer metadata + album art on each PLAYING
      // transition. Idempotent on the Dart side.
      SendMediaMetadata();
      if (!initial_album_art_.empty()) {
        SendAlbumArt(initial_album_art_, initial_album_art_mime_);
      }
      SendAudioInfo();

      // If audio was initially a fakesink, monitor for real device hotplug.
      if (!audio_upgraded_) {
        StartAudioMonitor();
      }
    } else if (state == GST_STATE_READY) {
      IHS_DEBUG("[VideoPlayer] message state changed, ready {}", m_texture_id);
    }
  }
}

void VideoPlayer::OnMediaError(GstMessage* msg) {
  GError* err;
  gchar* debug_info;
  gst_message_parse_error(msg, &err, &debug_info);
  const std::string error_msg = err->message ? err->message : "Unknown error";
  ihs::log::error("[VideoPlayer] Error: {}:{}", GST_OBJECT_NAME(msg->src),
                  error_msg);
  if (debug_info) {
    ihs::log::error("[VideoPlayer] {}", debug_info);
    g_free(debug_info);
  }
  g_clear_error(&err);

  std::lock_guard event_lock(event_mutex_);
  if (event_sink_) {
    event_sink_->Error("VideoPlayerError", error_msg);
  }
}

void VideoPlayer::OnMediaDurationChange() {
  GstQuery* query = gst_query_new_duration(GST_FORMAT_TIME);
  if (gst_element_query(playbin_, query)) {
    gst_query_parse_duration(query, nullptr, &duration_);
  }
  gst_query_unref(query);
}

void VideoPlayer::SendInitialized() {
  std::lock_guard event_lock(event_mutex_);
  if (!event_sink_) {
    return;
  }
  auto event = flutter::EncodableMap(
      {{flutter::EncodableValue("event"),
        flutter::EncodableValue("initialized")},
       {flutter::EncodableValue("duration"),
        flutter::EncodableValue(
            std::in_place_type<int64_t>,
            static_cast<int64_t>(duration_ / GST_MSECOND))}});

  event.insert({flutter::EncodableValue("width"),
                flutter::EncodableValue(std::in_place_type<int32_t>,
                                        static_cast<int32_t>(width_))});
  event.insert({flutter::EncodableValue("height"),
                flutter::EncodableValue(std::in_place_type<int32_t>,
                                        static_cast<int32_t>(height_))});
  event.insert({flutter::EncodableValue("isAudioOnly"),
                flutter::EncodableValue(!has_video_)});

  event_sink_->Success(flutter::EncodableValue(
      std::in_place_type<flutter::EncodableMap>, std::move(event)));
}

void VideoPlayer::OnTag(const GstTagList* list,
                        const gchar* tag,
                        gpointer /* user_data */) {
  const std::string tag_str = tag;
  if (const auto type = gst_tag_get_type(tag);
      tag_str == "audio-codec" && type == 64) {
    gchar* value = nullptr;
    if (gst_tag_list_get_string(list, tag, &value) && value) {
      ihs::log::debug("[VideoPlayer] audio-codec: {}", value);
      g_free(value);
    }
  } else if (tag_str == "video-codec" && type == 64) {
    gchar* value = nullptr;
    if (gst_tag_list_get_string(list, tag, &value) && value) {
      ihs::log::debug("[VideoPlayer] video-codec: {}", value);
      g_free(value);
    }
  } else if (tag_str == "maximum-bitrate" && type == 28) {
    guint value = 0;
    if (gst_tag_list_get_uint(list, tag, &value)) {
      ihs::log::debug("[VideoPlayer] maximum-bitrate: {}", value);
    }
  } else if (tag_str == "minimum-bitrate" && type == 28) {
    guint value = 0;
    if (gst_tag_list_get_uint(list, tag, &value)) {
      ihs::log::debug("[VideoPlayer] minimum-bitrate: {}", value);
    }
  } else if (tag_str == "bitrate" && type == 28) {
    guint value = 0;
    if (gst_tag_list_get_uint(list, tag, &value)) {
      ihs::log::debug("[VideoPlayer] bitrate: {}", value);
    }
  }
}

void VideoPlayer::handoff_handler(GstElement* /* fakesink */,
                                  GstBuffer* buffer,
                                  GstPad* /* pad */,
                                  void* user_data) {
  const auto obj = static_cast<VideoPlayer*>(user_data);
  if (!obj->m_valid || !obj->is_initialized_ || !obj->has_video_) {
    return;
  }

  std::lock_guard lock(obj->gst_mutex_);
  if (obj->info_.finfo == nullptr || !obj->shader_) {
    return;
  }
  GstVideoFrame frame;
  if (gst_video_frame_map(&frame, &obj->info_, buffer, GST_MAP_READ)) {
    {
      std::lock_guard ctx_lock(g_texture_context_mutex);
      obj->m_registrar->texture_registrar()->TextureMakeCurrent();
      glBindVertexArray(obj->shader_->vertex_arr_id_);

      if (const guint n_planes = GST_VIDEO_INFO_N_PLANES(&obj->info_);
          n_planes == 2) {
        // Assume NV12
        obj->shader_->load_pixels(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0),
                                  GST_VIDEO_FRAME_PLANE_DATA(&frame, 1),
                                  GST_VIDEO_FRAME_COMP_PSTRIDE(&frame, 0),
                                  GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0),
                                  GST_VIDEO_FRAME_COMP_PSTRIDE(&frame, 1),
                                  GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 1));
      } else {
        // Assume RGB
        obj->shader_->load_rgb_pixels(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
      }
      gst_video_frame_unmap(&frame);

      // Render NV12→RGBA into the render target
      glBindFramebuffer(GL_FRAMEBUFFER, obj->shader_->render_target());
      obj->shader_->draw_core();

      // When double-buffered, blit back→front to avoid tearing
      if (obj->shader_->double_buffer) {
        obj->shader_->blit_to_front();
      }

      obj->m_registrar->texture_registrar()->TextureClearCurrent();
    }

    // Send initialized event after first frame so the Dart Texture widget
    // doesn't display stale GL content before real video arrives.
    if (!obj->sent_initialized_) {
      obj->sent_initialized_ = true;
      obj->SendInitialized();
    }

    obj->m_registrar->texture_registrar()->MarkTextureFrameAvailable(
        obj->m_texture_id);
    IHS_TRACE("[VideoPlayer] frame");
  } else {
    ihs::log::error("[VideoPlayer] Cannot read video frame out from buffer");
  }
}

gboolean VideoPlayer::OnBusMessage(GstBus* /* bus */,
                                   GstMessage* msg,
                                   void* user_data) {
  auto obj = static_cast<VideoPlayer*>(user_data);
  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR:
      obj->OnMediaError(msg);
      return FALSE;
    case GST_MESSAGE_EOS:
    case GST_MESSAGE_SEGMENT_DONE: {
      IHS_DEBUG(
          "[VideoPlayer] {}: texture_id: {}",
          (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS ? "EOS" : "Segment done"),
          obj->m_texture_id);
      obj->OnPlaybackEnded();
      break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state, pending_state;
      gst_message_parse_state_changed(msg, &old_state, &new_state,
                                      &pending_state);
      if (GST_MESSAGE_SRC(msg) == GST_OBJECT(obj->playbin_)) {
        obj->OnMediaStateChange(new_state);
      }
      break;
    }
    case GST_MESSAGE_DURATION_CHANGED: {
      obj->OnMediaDurationChange();
      break;
    }
#if 0
    case GST_MESSAGE_LATENCY: {
      auto src = GST_MESSAGE_SRC(msg);
      IHS_DEBUG("[VideoPlayer] Latency: {}", src->name);
      GstQuery* query;
      gboolean res;
      query = gst_query_new_latency();
      res = gst_element_query(obj->playbin_, query);
      if (res) {
        GstClockTime min_latency;
        GstClockTime max_latency;
        gst_query_parse_latency(query, &obj->is_live_, &min_latency,
                                &max_latency);
      }
      gst_query_unref(query);
      break;
    }
#endif
    case GST_MESSAGE_WARNING: {
      GError* warn_err = nullptr;
      gchar* warn_debug = nullptr;
      gst_message_parse_warning(msg, &warn_err, &warn_debug);
      ihs::log::warn(
          "[VideoPlayer] Warning: {}:{} debug={}", GST_OBJECT_NAME(msg->src),
          warn_err ? warn_err->message : "", warn_debug ? warn_debug : "");
      g_clear_error(&warn_err);
      g_free(warn_debug);
      break;
    }
    case GST_MESSAGE_ASYNC_DONE: {
      IHS_DEBUG("[VideoPlayer] Async Done");
      // bufferingEnd
      break;
    }
    case GST_MESSAGE_NEW_CLOCK: {
      GstClock* clock;
      gst_message_parse_new_clock(msg, &clock);
      const GstClockTime time = gst_clock_get_time(clock);
      (void)time;
      IHS_DEBUG("[VideoPlayer] New Clock: {}", time);
      break;
    }
    case GST_MESSAGE_BUFFERING: {
      // no state management needed for live pipelines
      if (obj->is_live_)
        break;

      gint percent;
      gst_message_parse_buffering(msg, &percent);
      IHS_DEBUG("[VideoPlayer] Buffering: {}% texture_id: {}", percent,
                obj->m_texture_id);

      obj->SendBufferingUpdate();

      if (percent == 100) {
        // a 100% message means buffering is done
        if (obj->is_buffering_) {
          obj->is_buffering_ = false;
          obj->SetBuffering(false);
        }
        // if the desired state is playing, resume
        if (obj->target_state_ == GST_STATE_PLAYING) {
          gst_element_set_state(obj->playbin_, GST_STATE_PLAYING);
        }
      } else {
        // buffering busy
        if (!obj->is_buffering_ && obj->target_state_ == GST_STATE_PLAYING) {
          // pause the pipeline while buffering
          gst_element_set_state(obj->playbin_, GST_STATE_PAUSED);
        }
        if (!obj->is_buffering_) {
          obj->is_buffering_ = true;
          obj->SetBuffering(true);
        }
      }
      break;
    }
#if GSTREAMER_DEBUG
    case GST_MESSAGE_STREAM_STATUS: {
      GstStreamStatusType type;
      GstElement* owner;
      const GValue* val;
      gchar* path;
      GstTask* task = nullptr;

      IHS_DEBUG("STREAM_STATUS:");
      gst_message_parse_stream_status(msg, &type, &owner);

      val = gst_message_get_stream_status_object(msg);

      IHS_DEBUG("\ttype:   {}", static_cast<guint>(type));
      path = gst_object_get_path_string(GST_MESSAGE_SRC(msg));
      IHS_DEBUG("\tsource: {}", path);
      g_free(path);
      path = gst_object_get_path_string(GST_OBJECT(owner));
      IHS_DEBUG("\towner:  {}", path);
      g_free(path);
      IHS_DEBUG("\tobject: type {}, value {}", G_VALUE_TYPE_NAME(val),
                g_value_get_object(val));

      /* see if we know how to deal with this object */
      if (G_VALUE_TYPE(val) == GST_TYPE_TASK) {
        task = GST_TASK(g_value_get_object(val));
      }

      switch (type) {
        case GST_STREAM_STATUS_TYPE_CREATE:
          IHS_DEBUG("Created task: {}", fmt::ptr(task));
          break;
        case GST_STREAM_STATUS_TYPE_ENTER:
          IHS_DEBUG("raising task priority");
          // setpriority (PRIO_PROCESS, 0, -10);
          break;
        case GST_STREAM_STATUS_TYPE_LEAVE:
        default:
          break;
      }
      break;
    }
    case GST_MESSAGE_RESET_TIME: {
      GstClockTime running_time;
      gst_message_parse_reset_time(msg, &running_time);
      if (running_time > 0) {
        g_print("reset-time: %" GST_TIME_FORMAT, GST_TIME_ARGS(running_time));
      }
      break;
    }
    // element specific message
    case GST_MESSAGE_ELEMENT: {
      IHS_DEBUG("message-element: {}",
                gst_structure_get_name(gst_message_get_structure(msg)));
      break;
    }
#endif
    case GST_MESSAGE_TAG: {
      GstTagList* tags = nullptr;
      gst_message_parse_tag(msg, &tags);
      if (tags) {
        // Embedded album art (front cover preferred, then preview).
        GstSample* image_sample = nullptr;
        if (gst_tag_list_get_sample(tags, GST_TAG_IMAGE, &image_sample) ||
            gst_tag_list_get_sample(tags, GST_TAG_PREVIEW_IMAGE,
                                    &image_sample)) {
          obj->HandleAlbumArt(image_sample);
          gst_sample_unref(image_sample);
        }

        // Update text metadata if present and re-emit.
        bool changed = false;
        gchar* val = nullptr;
        if (gst_tag_list_get_string(tags, GST_TAG_TITLE, &val) && val) {
          obj->title_ = val;
          g_free(val);
          val = nullptr;
          changed = true;
        }
        if (gst_tag_list_get_string(tags, GST_TAG_ARTIST, &val) && val) {
          obj->artist_ = val;
          g_free(val);
          val = nullptr;
          changed = true;
        }
        if (gst_tag_list_get_string(tags, GST_TAG_ALBUM, &val) && val) {
          obj->album_ = val;
          g_free(val);
          val = nullptr;
          changed = true;
        }
        if (gst_tag_list_get_string(tags, GST_TAG_ALBUM_ARTIST, &val) && val) {
          obj->album_artist_ = val;
          g_free(val);
          val = nullptr;
          changed = true;
        }
        if (gst_tag_list_get_string(tags, GST_TAG_GENRE, &val) && val) {
          obj->genre_ = val;
          g_free(val);
          val = nullptr;
          changed = true;
        }
        if (gst_tag_list_get_string(tags, GST_TAG_AUDIO_CODEC, &val) && val) {
          obj->audio_codec_ = val;
          g_free(val);
          val = nullptr;
          changed = true;
        }
        guint track_num = 0;
        if (gst_tag_list_get_uint(tags, GST_TAG_TRACK_NUMBER, &track_num)) {
          obj->track_number_ = static_cast<int>(track_num);
          changed = true;
        }
        if (changed) {
          obj->SendMediaMetadata();
        }
        gst_tag_list_unref(tags);
      }
      break;
    }
    default:
#if GSTREAMER_DEBUG
    {
      IHS_DEBUG("GST Message Type: {}",
                gst_message_type_get_name(GST_MESSAGE_TYPE(msg)));
      break;
    }
#else
        ;
#endif
  }
  return TRUE;
}

void VideoPlayer::prepare(VideoPlayer* user_data) {
  IHS_DEBUG("[VideoPlayer] prepare");
  g_object_get(user_data->playbin_, "n-video", &user_data->n_video_, nullptr);
  IHS_DEBUG("[VideoPlayer] {} video streams", user_data->n_video_);
  g_object_get(user_data->playbin_, "current-video", &user_data->current_video_,
               nullptr);
  GstPad* pad = nullptr;
  g_signal_emit_by_name(user_data->playbin_, "get-video-pad",
                        user_data->current_video_, &pad);
  if (!pad) {
    ihs::log::error(
        "[VideoPlayer] Failed to get video pad, stream number might not exist");
    // TODO g_main_loop_quit(obj->main_loop_);
    return;
  }
  GstCaps* caps = gst_pad_get_current_caps(pad);
  if (!caps) {
    ihs::log::error("[VideoPlayer] Failed to get caps from video pad");
    gst_object_unref(pad);
    return;
  }
  std::lock_guard lock(user_data->gst_mutex_);
  if (!gst_video_info_from_caps(&user_data->info_, caps)) {
    ihs::log::error("[VideoPlayer] Fail to get video info from the cap");
  }
  gst_caps_unref(caps);
  IHS_DEBUG("[VideoPlayer] original video width: {}, height: {}",
            user_data->info_.width, user_data->info_.height);
  // set to the target
  if (!gst_video_info_set_format(&user_data->info_, GST_VIDEO_FORMAT_NV12,
                                 static_cast<guint>(user_data->width_),
                                 static_cast<guint>(user_data->height_))) {
    ihs::log::error(
        "[VideoPlayer] Failed to set the video info to target NV12");
  }
}

// ────────────────────────────────────────────────────────────────────────────
// Phase 1 — Foundation: audio sink bin, audio control, source-setup, album art
// ────────────────────────────────────────────────────────────────────────────

GstElement* VideoPlayer::BuildAudioSinkBin() {
  // Pick the real sink. Try the env-var override first, then walk the
  // fallback chain. Each candidate is taken to READY before being accepted
  // — autoaudiosink instantiates fine even when no real device is
  // available, but later fails caps negotiation with a confusing
  // GST_FLOW_NOT_LINKED at the source. Testing READY here surfaces those
  // failures up front.
  GstElement* real_sink = nullptr;
  const char* picked = nullptr;
  std::vector<const char*> candidates;
  if (const char* env = std::getenv("VIDEO_PLAYER_AUDIO_SINK")) {
    candidates.push_back(env);
  }
  candidates.insert(candidates.end(),
                    {"pipewiresink", "pulsesink", "alsasink", "autoaudiosink"});
  for (const char* name : candidates) {
    GstElement* candidate = gst_element_factory_make(name, "real-audiosink");
    if (!candidate) {
      IHS_DEBUG("[VideoPlayer] audio sink {} unavailable", name);
      continue;
    }
    // Trust pipewiresink and pulsesink — they're reliable on modern Linux
    // and a state-change probe triggers a noisy "thread-loop recurse"
    // warning from PipeWire. Only the unreliable fallbacks
    // (alsasink, autoaudiosink, env-var override) get the READY probe.
    const bool trusted = std::strcmp(name, "pipewiresink") == 0 ||
                         std::strcmp(name, "pulsesink") == 0;
    if (!trusted) {
      GstStateChangeReturn ret =
          gst_element_set_state(candidate, GST_STATE_READY);
      if (ret == GST_STATE_CHANGE_FAILURE) {
        IHS_DEBUG("[VideoPlayer] audio sink {} failed READY", name);
        gst_element_set_state(candidate, GST_STATE_NULL);
        gst_object_unref(candidate);
        continue;
      }
      gst_element_set_state(candidate, GST_STATE_NULL);
    }
    real_sink = candidate;
    picked = name;
    break;
  }
  if (!real_sink) {
    ihs::log::error("[VideoPlayer] No working audio sink found");
    return nullptr;
  }
  ihs::log::info("[VideoPlayer] Selected audio sink: {}", picked);
  if (const char* dev = std::getenv("VIDEO_PLAYER_AUDIO_DEVICE")) {
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(real_sink), "device")) {
      g_object_set(real_sink, "device", dev, nullptr);
    }
  }

  audio_convert_ = gst_element_factory_make("audioconvert", "audio-convert");
  audio_resample_ = gst_element_factory_make("audioresample", "audio-resample");
  // scaletempo time-stretches audio when the segment rate isn't 1.0 so the
  // pitch stays correct. Without it, rate-change seeks make the audio drop
  // out / pitch-shift. Ships in gst-plugins-good.
  audio_scaletempo_ =
      gst_element_factory_make("scaletempo", "audio-scaletempo");
  audio_capsfilter_ = gst_element_factory_make("capsfilter", "audio-caps");
  if (!audio_convert_ || !audio_resample_ || !audio_capsfilter_) {
    ihs::log::error("[VideoPlayer] Failed to create audio sink bin elements");
    if (audio_convert_)
      gst_object_unref(audio_convert_);
    if (audio_resample_)
      gst_object_unref(audio_resample_);
    if (audio_scaletempo_)
      gst_object_unref(audio_scaletempo_);
    if (audio_capsfilter_)
      gst_object_unref(audio_capsfilter_);
    audio_convert_ = audio_resample_ = audio_capsfilter_ = nullptr;
    audio_scaletempo_ = nullptr;
    gst_object_unref(real_sink);
    return nullptr;
  }
  if (!audio_scaletempo_) {
    ihs::log::warn(
        "[VideoPlayer] scaletempo unavailable; playback rate changes will "
        "pitch-shift audio. Install gst-plugins-good.");
  }

  // Output channel count from env var (Phase 1 default 2).
  output_channels_ = 2;
  if (const char* env = std::getenv("VIDEO_PLAYER_AUDIO_CHANNELS")) {
    char* end = nullptr;
    long val = std::strtol(env, &end, 10);
    if (end != env && val >= 1 && val <= 8) {
      output_channels_ = static_cast<int>(val);
    }
  }
  GstCaps* caps = gst_caps_new_simple("audio/x-raw", "channels", G_TYPE_INT,
                                      output_channels_, nullptr);
  g_object_set(audio_capsfilter_, "caps", caps, nullptr);
  gst_caps_unref(caps);

  // Channel reordering defaults — fixes media with broken position metadata.
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(audio_convert_),
                                   "input-channels-reorder-mode")) {
    int reorder_mode = 0;
    int reorder_layout = 1;  // SMPTE
    if (const char* env = std::getenv("VIDEO_PLAYER_AUDIO_REORDER_MODE")) {
      char* end = nullptr;
      const long v = std::strtol(env, &end, 10);
      if (end != env && v >= 0 && v <= 2)
        reorder_mode = static_cast<int>(v);
    }
    if (const char* env = std::getenv("VIDEO_PLAYER_AUDIO_REORDER")) {
      char* end = nullptr;
      const long v = std::strtol(env, &end, 10);
      if (end != env && v >= 0 && v <= 4)
        reorder_layout = static_cast<int>(v);
    }
    g_object_set(audio_convert_, "input-channels-reorder-mode", reorder_mode,
                 "input-channels-reorder", reorder_layout, nullptr);
  }

  GstElement* bin = gst_bin_new("audio-sink-bin");
  gst_bin_add(GST_BIN(bin), audio_convert_);
  gst_bin_add(GST_BIN(bin), audio_resample_);
  if (audio_scaletempo_)
    gst_bin_add(GST_BIN(bin), audio_scaletempo_);
  gst_bin_add(GST_BIN(bin), audio_capsfilter_);
  gst_bin_add(GST_BIN(bin), real_sink);

  // Chain: convert → resample → [scaletempo] → capsfilter → real sink
  bool linked;
  if (audio_scaletempo_) {
    linked = gst_element_link_many(audio_convert_, audio_resample_,
                                   audio_scaletempo_, audio_capsfilter_,
                                   real_sink, nullptr);
  } else {
    linked = gst_element_link_many(audio_convert_, audio_resample_,
                                   audio_capsfilter_, real_sink, nullptr);
  }
  if (!linked) {
    ihs::log::error("[VideoPlayer] Failed to link audio sink bin");
    gst_object_unref(bin);
    audio_convert_ = audio_resample_ = audio_capsfilter_ = nullptr;
    audio_scaletempo_ = nullptr;
    return nullptr;
  }

  // Add the ghost sink pad. We deliberately do NOT call
  // gst_pad_set_active() here — pad activation is the responsibility of
  // the parent's state change logic, and forcing it before the bin has a
  // parent has been observed to break uridecodebin auto-plugging on
  // audio-only sources (souphttpsrc returns GST_FLOW_NOT_LINKED).
  GstPad* pad = gst_element_get_static_pad(audio_convert_, "sink");
  GstPad* ghost = gst_ghost_pad_new("sink", pad);
  gst_element_add_pad(bin, ghost);
  gst_object_unref(pad);

  IHS_DEBUG("[VideoPlayer] Built audio sink bin (sink={}, channels={})", picked,
            output_channels_);
  return bin;
}

void VideoPlayer::OnSourceSetup(GstElement* /*playbin*/,
                                GstElement* source,
                                gpointer /*user_data*/) {
  if (!source)
    return;
  GObjectClass* klass = G_OBJECT_GET_CLASS(source);

  // HTTP timeout (souphttpsrc): default 30s prevents indefinite hangs.
  if (g_object_class_find_property(klass, "timeout")) {
    guint timeout = 30;
    if (const char* env = std::getenv("VIDEO_PLAYER_HTTP_TIMEOUT")) {
      char* end = nullptr;
      const long val = std::strtol(env, &end, 10);
      if (end != env && val >= 0 && val < 3600) {
        timeout = static_cast<guint>(val);
      }
    }
    g_object_set(source, "timeout", timeout, nullptr);
    IHS_DEBUG("[VideoPlayer] source timeout={}s", timeout);
  }
  if (g_object_class_find_property(klass, "user-agent")) {
    if (const char* ua = std::getenv("VIDEO_PLAYER_USER_AGENT")) {
      g_object_set(source, "user-agent", ua, nullptr);
    }
  }
  if (g_object_class_find_property(klass, "proxy")) {
    if (const char* proxy = std::getenv("VIDEO_PLAYER_PROXY")) {
      g_object_set(source, "proxy", proxy, nullptr);
    }
  }
  // RTSP latency (rtspsrc) — IVI camera streams typically want low latency.
  if (g_object_class_find_property(klass, "latency")) {
    guint latency = 200;
    if (const char* env = std::getenv("VIDEO_PLAYER_RTSP_LATENCY")) {
      char* end = nullptr;
      const long val = std::strtol(env, &end, 10);
      if (end != env && val >= 0 && val < 60000) {
        latency = static_cast<guint>(val);
      }
    }
    g_object_set(source, "latency", latency, nullptr);
  }
}

void VideoPlayer::HandleAlbumArt(GstSample* sample) {
  if (!sample)
    return;
  GstBuffer* buffer = gst_sample_get_buffer(sample);
  GstCaps* caps = gst_sample_get_caps(sample);
  if (!buffer || !caps)
    return;

  const GstStructure* s = gst_caps_get_structure(caps, 0);
  const gchar* mime = s ? gst_structure_get_name(s) : "image/jpeg";

  // Filter to front cover (or undefined, which is typically the cover).
  const GstStructure* sample_info = gst_sample_get_info(sample);
  GstTagImageType image_type = GST_TAG_IMAGE_TYPE_UNDEFINED;
  if (sample_info) {
    gst_structure_get_enum(sample_info, "image-type", GST_TYPE_TAG_IMAGE_TYPE,
                           reinterpret_cast<gint*>(&image_type));
  }
  if (image_type != GST_TAG_IMAGE_TYPE_FRONT_COVER &&
      image_type != GST_TAG_IMAGE_TYPE_UNDEFINED) {
    return;
  }

  GstMapInfo map;
  if (!gst_buffer_map(buffer, &map, GST_MAP_READ))
    return;
  // 10 MB cap (see discover_media_info for rationale).
  constexpr size_t kMaxAlbumArtBytes = static_cast<size_t>(10) * 1024 * 1024;
  if (map.size == 0 || map.size > kMaxAlbumArtBytes) {
    if (map.size > kMaxAlbumArtBytes) {
      ihs::log::warn(
          "[VideoPlayer] Runtime album art is {} bytes (>{} MB cap); ignoring.",
          map.size, kMaxAlbumArtBytes / (static_cast<size_t>(1024) * 1024));
    }
    gst_buffer_unmap(buffer, &map);
    return;
  }
  std::vector<uint8_t> bytes(map.data, map.data + map.size);
  gst_buffer_unmap(buffer, &map);

  SendAlbumArt(bytes, mime ? mime : "image/jpeg");
}

void VideoPlayer::SendAlbumArt(const std::vector<uint8_t>& bytes,
                               const std::string& mime) {
  std::lock_guard event_lock(event_mutex_);
  if (!event_sink_)
    return;
  auto map = flutter::EncodableMap{
      {flutter::EncodableValue("event"), flutter::EncodableValue("albumArt")},
      {flutter::EncodableValue("mimeType"), flutter::EncodableValue(mime)},
      {flutter::EncodableValue("data"), flutter::EncodableValue(bytes)},
  };
  event_sink_->Success(flutter::EncodableValue(
      std::in_place_type<flutter::EncodableMap>, std::move(map)));
}

void VideoPlayer::SendMediaMetadata() {
  std::lock_guard event_lock(event_mutex_);
  if (!event_sink_)
    return;
  auto map = flutter::EncodableMap{
      {flutter::EncodableValue("event"),
       flutter::EncodableValue("mediaMetadata")},
      {flutter::EncodableValue("title"), flutter::EncodableValue(title_)},
      {flutter::EncodableValue("artist"), flutter::EncodableValue(artist_)},
      {flutter::EncodableValue("album"), flutter::EncodableValue(album_)},
      {flutter::EncodableValue("albumArtist"),
       flutter::EncodableValue(album_artist_)},
      {flutter::EncodableValue("genre"), flutter::EncodableValue(genre_)},
      {flutter::EncodableValue("trackNumber"),
       flutter::EncodableValue(track_number_)},
  };
  event_sink_->Success(flutter::EncodableValue(
      std::in_place_type<flutter::EncodableMap>, std::move(map)));
}

void VideoPlayer::SendAudioInfo() {
  std::lock_guard event_lock(event_mutex_);
  if (!event_sink_)
    return;
  auto map = flutter::EncodableMap{
      {flutter::EncodableValue("event"), flutter::EncodableValue("audioInfo")},
      {flutter::EncodableValue("codec"), flutter::EncodableValue(audio_codec_)},
      {flutter::EncodableValue("channels"),
       flutter::EncodableValue(audio_channels_)},
      {flutter::EncodableValue("sampleRate"),
       flutter::EncodableValue(audio_sample_rate_)},
  };
  event_sink_->Success(flutter::EncodableValue(
      std::in_place_type<flutter::EncodableMap>, std::move(map)));
}

int VideoPlayer::GetAudioTrackCount() {
  if (!playbin_)
    return 0;
  gint n = 0;
  g_object_get(playbin_, "n-audio", &n, nullptr);
  return n;
}

void VideoPlayer::SetAudioTrack(int index) {
  if (!playbin_)
    return;
  gint n = 0;
  g_object_get(playbin_, "n-audio", &n, nullptr);
  if (index < 0 || index >= n) {
    ihs::log::warn("[VideoPlayer] SetAudioTrack({}) out of range (n={})", index,
                   n);
    return;
  }
  g_object_set(playbin_, "current-audio", index, nullptr);
}

void VideoPlayer::SetOutputChannels(int channels) {
  if (channels < 1 || channels > 8) {
    ihs::log::warn("[VideoPlayer] SetOutputChannels({}) out of range",
                   channels);
    return;
  }
  output_channels_ = channels;
  if (!audio_capsfilter_)
    return;
  GstCaps* caps = gst_caps_new_simple("audio/x-raw", "channels", G_TYPE_INT,
                                      channels, nullptr);
  g_object_set(audio_capsfilter_, "caps", caps, nullptr);
  gst_caps_unref(caps);
  // The capsfilter change requires a quick READY round-trip on the audio
  // chain to take effect mid-stream. Acceptable known dropout. The
  // round-trip also resets segment rate, so invalidate the cache.
  if (playbin_) {
    GstState cur = GST_STATE_NULL;
    gst_element_get_state(playbin_, &cur, nullptr, 0);
    rate_.store(-2.0);
    gst_element_set_state(playbin_, GST_STATE_READY);
    gst_element_set_state(playbin_, cur);
  }
}

void VideoPlayer::SetMute(bool mute) {
  if (!playbin_)
    return;
  g_object_set(playbin_, "mute", mute ? TRUE : FALSE, nullptr);
}

// ────────────────────────────────────────────────────────────────────────────
// Phase 2 — Quality & Tuning: scale, A/V offset, subtitles, channel presets
// ────────────────────────────────────────────────────────────────────────────

void VideoPlayer::SetScaleMethod(int method) {
  if (!has_video_ || !video_scale_) {
    return;
  }
  if (method < 0 || method > 9) {
    ihs::log::warn("[VideoPlayer] SetScaleMethod({}) out of range", method);
    return;
  }
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(video_scale_),
                                   "method")) {
    g_object_set(video_scale_, "method", method, nullptr);
  }
}

void VideoPlayer::SetAVOffset(int64_t offset_ms) {
  if (!playbin_)
    return;
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(playbin_), "av-offset")) {
    g_object_set(playbin_, "av-offset",
                 static_cast<gint64>(offset_ms) * GST_MSECOND, nullptr);
  }
}

void VideoPlayer::SetSubtitlesEnabled(bool enabled) {
  if (!playbin_)
    return;
  gint flags = 0;
  g_object_get(playbin_, "flags", &flags, nullptr);
  if (enabled) {
    flags |= GST_PLAY_FLAG_TEXT;
  } else {
    flags &= ~GST_PLAY_FLAG_TEXT;
  }
  g_object_set(playbin_, "flags", flags, nullptr);
}

int VideoPlayer::GetSubtitleTrackCount() {
  if (!playbin_)
    return 0;
  gint n = 0;
  g_object_get(playbin_, "n-text", &n, nullptr);
  return n;
}

void VideoPlayer::SetSubtitleTrack(int index) {
  if (!playbin_)
    return;
  gint n = 0;
  g_object_get(playbin_, "n-text", &n, nullptr);
  if (index < 0 || index >= n) {
    ihs::log::warn("[VideoPlayer] SetSubtitleTrack({}) out of range (n={})",
                   index, n);
    return;
  }
  g_object_set(playbin_, "current-text", index, nullptr);
}

void VideoPlayer::SetSubtitleUri(const std::string& uri) {
  if (!playbin_)
    return;
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(playbin_), "suburi")) {
    g_object_set(playbin_, "suburi", uri.empty() ? nullptr : uri.c_str(),
                 nullptr);
  }
}

void VideoPlayer::SetSubtitleFont(const std::string& font_desc) {
  if (!playbin_)
    return;
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(playbin_),
                                   "subtitle-font-desc")) {
    g_object_set(playbin_, "subtitle-font-desc", font_desc.c_str(), nullptr);
  }
}

// Query the negotiated input channel count on audio_convert_'s sink pad.
// Returns 0 if the pad has no current caps (pipeline not yet rolling).
static int QueryInputChannels(GstElement* audio_convert) {
  if (!audio_convert)
    return 0;
  GstPad* sink = gst_element_get_static_pad(audio_convert, "sink");
  if (!sink)
    return 0;
  int channels = 0;
  if (GstCaps* caps = gst_pad_get_current_caps(sink)) {
    if (gst_caps_get_size(caps) > 0) {
      const GstStructure* s = gst_caps_get_structure(caps, 0);
      gst_structure_get_int(s, "channels", &channels);
    }
    gst_caps_unref(caps);
  }
  gst_object_unref(sink);
  return channels;
}

void VideoPlayer::SetChannelMixPreset(const std::string& preset) {
  if (!audio_convert_) {
    ihs::log::warn("[VideoPlayer] SetChannelMixPreset: audio bin not built");
    return;
  }
  if (!g_object_class_find_property(G_OBJECT_GET_CLASS(audio_convert_),
                                    "mix-matrix")) {
    ihs::log::warn(
        "[VideoPlayer] audioconvert mix-matrix unsupported (GStreamer < 1.20)");
    return;
  }

  // The mix-matrix property must match the actual input channel count
  // (cols) × output channel count (rows) of the negotiated caps. If the
  // source isn't 5.1, the surround presets are meaningless — fall back to
  // a plain capsfilter-driven downmix and let audioconvert pick stock
  // coefficients.
  const int in_ch = QueryInputChannels(audio_convert_);
  if (in_ch != 6) {
    ihs::log::debug(
        "[VideoPlayer] SetChannelMixPreset('{}'): input has {} channels "
        "(need 6 for the matrix presets); applying capsfilter only.",
        preset, in_ch);
    if (preset == "surround") {
      SetOutputChannels(in_ch > 0 ? in_ch : 6);
    } else {
      SetOutputChannels(2);
    }
    return;
  }

  // Standard 5.1 input order (SMPTE): FL, FR, FC, LFE, RL, RR
  // Each preset defines two output rows (stereo) × six input columns.
  // Coefficients are applied as a flat row-major GValueArray of GValueArrays.
  struct Preset {
    const char* name;
    int out_channels;
    double matrix[2][6];
  };
  static constexpr Preset kPresets[] = {
      // ITU-R BS.775 standard stereo downmix.
      {"stereo",
       2,
       {{1.0, 0.0, 0.707, 0.707, 0.707, 0.0},
        {0.0, 1.0, 0.707, 0.707, 0.0, 0.707}}},
      // Boosted dialog (FC) for the driver, modest LFE bleed.
      {"driver",
       2,
       {{1.0, 0.0, 0.85, 0.5, 0.5, 0.0}, {0.0, 1.0, 0.85, 0.5, 0.0, 0.5}}},
      // Reduced dynamic range for night listening.
      {"night",
       2,
       {{1.0, 0.0, 0.707, 0.1, 0.5, 0.0}, {0.0, 1.0, 0.707, 0.1, 0.0, 0.5}}},
      // Rear-cabin optimised — emphasise rear channels.
      {"rear",
       2,
       {{1.0, 0.0, 0.5, 0.3, 1.0, 0.0}, {0.0, 1.0, 0.5, 0.3, 0.0, 1.0}}},
  };

  if (preset == "surround") {
    // Identity-ish: clear matrix, force 5.1 output.
    GValue empty = G_VALUE_INIT;
    g_value_init(&empty, GST_TYPE_ARRAY);
    g_object_set_property(G_OBJECT(audio_convert_), "mix-matrix", &empty);
    g_value_unset(&empty);
    SetOutputChannels(6);
    return;
  }

  const Preset* match = nullptr;
  for (const auto& p : kPresets) {
    if (preset == p.name) {
      match = &p;
      break;
    }
  }
  if (!match) {
    ihs::log::warn("[VideoPlayer] Unknown channel mix preset '{}'", preset);
    return;
  }

  // Build a GST_TYPE_ARRAY of GST_TYPE_ARRAY of doubles.
  GValue matrix = G_VALUE_INIT;
  g_value_init(&matrix, GST_TYPE_ARRAY);
  for (int row = 0; row < match->out_channels; ++row) {
    GValue gst_row = G_VALUE_INIT;
    g_value_init(&gst_row, GST_TYPE_ARRAY);
    for (int col = 0; col < 6; ++col) {
      GValue cell = G_VALUE_INIT;
      g_value_init(&cell, G_TYPE_DOUBLE);
      g_value_set_double(&cell, match->matrix[row][col]);
      gst_value_array_append_value(&gst_row, &cell);
      g_value_unset(&cell);
    }
    gst_value_array_append_value(&matrix, &gst_row);
    g_value_unset(&gst_row);
  }
  g_object_set_property(G_OBJECT(audio_convert_), "mix-matrix", &matrix);
  g_value_unset(&matrix);

  SetOutputChannels(match->out_channels);
  IHS_DEBUG("[VideoPlayer] Applied channel mix preset '{}'", preset);
}

// ────────────────────────────────────────────────────────────────────────────
// Phase 3 — Premium features: equalizer, video balance, passthrough, custom
// downmix matrix
// ────────────────────────────────────────────────────────────────────────────

void VideoPlayer::SetEqualizer(const std::vector<double>& bands) {
  if (bands.size() != 10) {
    ihs::log::warn("[VideoPlayer] SetEqualizer: expected 10 bands, got {}",
                   bands.size());
    return;
  }
  // Lazily insert equalizer-10bands into the audio sink bin on first
  // call. The current chain is:
  //   convert → resample → [scaletempo] → capsfilter → real_sink
  // We splice the EQ between scaletempo (or resample, when scaletempo
  // wasn't available) and capsfilter so it operates on the time-stretched
  // PCM that the user actually hears. Inserted in READY with a brief
  // audio dropout.
  if (!equalizer_) {
    if (!audio_bin_ || !audio_capsfilter_) {
      ihs::log::warn("[VideoPlayer] SetEqualizer: audio bin not available");
      return;
    }
    GstElement* upstream =
        audio_scaletempo_ ? audio_scaletempo_ : audio_resample_;
    if (!upstream) {
      ihs::log::warn("[VideoPlayer] SetEqualizer: upstream element missing");
      return;
    }
    GstElement* eq = gst_element_factory_make("equalizer-10bands", "audio-eq");
    if (!eq) {
      ihs::log::warn(
          "[VideoPlayer] equalizer-10bands element unavailable; install "
          "gst-plugins-good");
      return;
    }
    GstState cur = GST_STATE_NULL;
    if (playbin_)
      gst_element_get_state(playbin_, &cur, nullptr, 0);
    // READY round-trip resets segment rate; invalidate so the next
    // PLAYING re-applies pending_rate_.
    rate_.store(-2.0);
    if (playbin_)
      gst_element_set_state(playbin_, GST_STATE_READY);

    gst_element_unlink(upstream, audio_capsfilter_);
    gst_bin_add(GST_BIN(audio_bin_), eq);
    if (!gst_element_link_many(upstream, eq, audio_capsfilter_, nullptr)) {
      ihs::log::error(
          "[VideoPlayer] Failed to splice equalizer into audio bin");
      gst_element_unlink(upstream, eq);
      gst_element_unlink(eq, audio_capsfilter_);
      gst_bin_remove(GST_BIN(audio_bin_), eq);
      // Restore the original direct link.
      gst_element_link(upstream, audio_capsfilter_);
      if (playbin_)
        gst_element_set_state(playbin_, cur);
      return;
    }
    gst_element_sync_state_with_parent(eq);
    equalizer_ = eq;
    if (playbin_)
      gst_element_set_state(playbin_, cur);
  }

  for (size_t i = 0; i < bands.size(); ++i) {
    char prop[16];
    // Buffer is 16 bytes, output is "band0".."band9" (max 6 bytes), so
    // truncation is impossible. Cast to void to silence cert-err33-c.
    (void)std::snprintf(prop, sizeof(prop), "band%d", static_cast<int>(i));
    double v = bands[i];
    if (v < -24.0)
      v = -24.0;
    if (v > 12.0)
      v = 12.0;
    g_object_set(equalizer_, prop, v, nullptr);
  }
}

void VideoPlayer::SetVideoBalance(double brightness,
                                  double contrast,
                                  double saturation,
                                  double hue) {
  if (!has_video_) {
    return;
  }
  // Lazily insert videobalance between videoconvert and videoscale on
  // the first non-default call.
  const bool is_default =
      brightness == 0.0 && contrast == 1.0 && saturation == 1.0 && hue == 0.0;
  if (!videobalance_) {
    if (is_default || !pipeline_ || !video_convert_ || !video_scale_) {
      return;
    }
    GstElement* vb = gst_element_factory_make("videobalance", "video-balance");
    if (!vb) {
      ihs::log::warn("[VideoPlayer] videobalance element unavailable");
      return;
    }
    GstState cur = GST_STATE_NULL;
    if (playbin_)
      gst_element_get_state(playbin_, &cur, nullptr, 0);
    // READY round-trip resets segment rate; invalidate so the next
    // PLAYING re-applies pending_rate_.
    rate_.store(-2.0);
    if (playbin_)
      gst_element_set_state(playbin_, GST_STATE_READY);

    gst_element_unlink(video_convert_, video_scale_);
    gst_bin_add(GST_BIN(pipeline_), vb);
    // Preserve the NV12 caps the original chain depended on so the
    // downstream fakesink's handoff_handler still receives NV12 frames.
    GstCaps* nv12 = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
                                        "NV12", nullptr);
    const bool ok = gst_element_link(video_convert_, vb) &&
                    gst_element_link_filtered(vb, video_scale_, nv12);
    gst_caps_unref(nv12);
    if (!ok) {
      ihs::log::error("[VideoPlayer] Failed to splice videobalance");
      gst_element_unlink(video_convert_, vb);
      gst_element_unlink(vb, video_scale_);
      gst_bin_remove(GST_BIN(pipeline_), vb);
      // Restore the original direct link.
      GstCaps* restore = gst_caps_new_simple("video/x-raw", "format",
                                             G_TYPE_STRING, "NV12", nullptr);
      gst_element_link_filtered(video_convert_, video_scale_, restore);
      gst_caps_unref(restore);
      if (playbin_)
        gst_element_set_state(playbin_, cur);
      return;
    }
    gst_element_sync_state_with_parent(vb);
    videobalance_ = vb;
    if (playbin_)
      gst_element_set_state(playbin_, cur);
  }
  if (videobalance_) {
    g_object_set(videobalance_, "brightness", brightness, "contrast", contrast,
                 "saturation", saturation, "hue", hue, nullptr);
  }
}

void VideoPlayer::SetAudioPassthrough(bool enabled) {
  // Passthrough requires the playbin to forward encoded audio to a sink that
  // accepts encoded caps. autoaudiosink does not. The plugin can't safely
  // hot-swap the sink mid-stream, so this method updates a flag that takes
  // effect on the next media load. Document the env-var fallback as the
  // recommended deployment configuration.
  if (!playbin_)
    return;
  // The most we can do at runtime: tweak the playbin flags to permit
  // native (non-decoded) audio paths via GST_PLAY_FLAG_NATIVE_AUDIO (1<<5).
  constexpr int kNativeAudio = 1 << 5;
  gint flags = 0;
  g_object_get(playbin_, "flags", &flags, nullptr);
  if (enabled) {
    flags |= kNativeAudio;
  } else {
    flags &= ~kNativeAudio;
  }
  g_object_set(playbin_, "flags", flags, nullptr);
}

void VideoPlayer::SetChannelMixMatrix(int in_channels,
                                      int out_channels,
                                      const std::vector<double>& matrix) {
  if (!audio_convert_) {
    ihs::log::warn("[VideoPlayer] SetChannelMixMatrix: audio bin not built");
    return;
  }
  if (in_channels <= 0 || out_channels <= 0 || in_channels > 8 ||
      out_channels > 8) {
    ihs::log::warn(
        "[VideoPlayer] SetChannelMixMatrix: invalid dimensions {}x{}",
        in_channels, out_channels);
    return;
  }
  if (static_cast<int>(matrix.size()) != in_channels * out_channels) {
    ihs::log::warn(
        "[VideoPlayer] SetChannelMixMatrix: matrix size mismatch (got {}, "
        "expected {})",
        matrix.size(), in_channels * out_channels);
    return;
  }
  if (!g_object_class_find_property(G_OBJECT_GET_CLASS(audio_convert_),
                                    "mix-matrix")) {
    ihs::log::warn("[VideoPlayer] audioconvert mix-matrix unsupported");
    return;
  }

  GValue mat = G_VALUE_INIT;
  g_value_init(&mat, GST_TYPE_ARRAY);
  for (int row = 0; row < out_channels; ++row) {
    GValue gst_row = G_VALUE_INIT;
    g_value_init(&gst_row, GST_TYPE_ARRAY);
    for (int col = 0; col < in_channels; ++col) {
      GValue cell = G_VALUE_INIT;
      g_value_init(&cell, G_TYPE_DOUBLE);
      g_value_set_double(
          &cell,
          matrix[static_cast<size_t>(row) * static_cast<size_t>(in_channels) +
                 static_cast<size_t>(col)]);
      gst_value_array_append_value(&gst_row, &cell);
      g_value_unset(&cell);
    }
    gst_value_array_append_value(&mat, &gst_row);
    g_value_unset(&gst_row);
  }
  g_object_set_property(G_OBJECT(audio_convert_), "mix-matrix", &mat);
  g_value_unset(&mat);
  SetOutputChannels(out_channels);
}

}  // namespace video_player_linux
