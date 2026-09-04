
#include "audio_player.h"

#include <flutter/event_stream_handler_functions.h>
#include <flutter/standard_method_codec.h>
#include <memory>
#include "logging/logging.h"

#include <gst/pbutils/gstdiscoverer.h>

#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <utility>

#define STR_LINK_TROUBLESHOOTING \
  "https://github.com/bluefireteam/audioplayers/blob/main/troubleshooting.md"

namespace {

// Post a task to the GLib main-loop thread. Used to marshal event delivery
// from GStreamer threads (bus, streaming) onto the thread that owns the
// Flutter engine's BinaryMessenger.
void PostToMainLoop(std::function<void()> task) {
  auto* heap = new std::function<void()>(std::move(task));
  g_idle_add_full(
      G_PRIORITY_DEFAULT,
      [](gpointer user_data) -> gboolean {
        auto* t = static_cast<std::function<void()>*>(user_data);
        (*t)();
        delete t;
        return G_SOURCE_REMOVE;
      },
      heap, nullptr);
}

}  // namespace

// static
void AudioPlayer::PostToMainLoop(std::function<void()> task) {
  ::PostToMainLoop(std::move(task));
}

AudioPlayer::AudioPlayer(std::string playerId, BinaryMessenger* messenger)
    : state_(std::make_shared<SharedState>()),
      media_state_(GST_STATE_VOID_PENDING) {
  state_->event_channel_name = std::move(playerId);

  auto state = state_;  // captured by stream handlers
  event_channel_ = std::make_unique<flutter::EventChannel<>>(
      messenger, state_->event_channel_name,
      &flutter::StandardMethodCodec::GetInstance());
  event_channel_->SetStreamHandler(
      std::make_unique<flutter::StreamHandlerFunctions<>>(
          [state](const EncodableValue* /* arguments */,
                  std::unique_ptr<flutter::EventSink<>>&& events)
              -> std::unique_ptr<flutter::StreamHandlerError<>> {
            std::lock_guard<std::mutex> lock(state->sink_mu);
            if (!state->cancelled) {
              state->sink = std::move(events);
            }
            return nullptr;
          },
          [state](const EncodableValue* /* arguments */)
              -> std::unique_ptr<flutter::StreamHandlerError<>> {
            std::lock_guard<std::mutex> lock(state->sink_mu);
            state->sink.reset();
            return nullptr;
          }));

  playbin_ = gst_element_factory_make("playbin", nullptr);
  if (!playbin_) {
    throw std::runtime_error("Not all elements could be created.");
  }

  // Setup stereo balance controller.
  panorama_ = gst_element_factory_make("audiopanorama", nullptr);
  if (panorama_) {
    audiobin_ = gst_bin_new(nullptr);
    audiosink_ = gst_element_factory_make("autoaudiosink", nullptr);

    gst_bin_add_many(GST_BIN(audiobin_), panorama_, audiosink_, nullptr);
    gst_element_link(panorama_, audiosink_);

    GstPad* sinkpad = gst_element_get_static_pad(panorama_, "sink");
    panoramaSinkPad_ = gst_ghost_pad_new("sink", sinkpad);
    gst_element_add_pad(audiobin_, panoramaSinkPad_);
    gst_object_unref(GST_OBJECT(sinkpad));

    g_object_set(G_OBJECT(playbin_), "audio-sink", audiobin_, nullptr);
    g_object_set(G_OBJECT(panorama_), "method", 1, nullptr);
  }

  // playbin fires about-to-finish when the current source is draining. We
  // use it for gapless looping (see AboutToFinish). EOS also fires reliably
  // now and is handled separately for completion notification.
  g_signal_connect(playbin_, "about-to-finish",
                   G_CALLBACK(AudioPlayer::AboutToFinish), this);

  bus_ = gst_element_get_bus(playbin_);

  // Watch bus messages.
  gst_bus_add_watch(bus_, reinterpret_cast<GstBusFunc>(OnBusMessage), this);
}

AudioPlayer::~AudioPlayer() {
  // Cancel event delivery BEFORE tearing down the pipeline. Any idle
  // callbacks that were queued before this point and haven't fired yet will
  // see cancelled=true on the shared state and become no-ops. The detached
  // GstDiscoverer thread also observes this via the shared state ref.
  {
    std::lock_guard<std::mutex> lock(state_->sink_mu);
    state_->cancelled = true;
    state_->sink.reset();
  }

  if (event_channel_) {
    event_channel_->SetStreamHandler(nullptr);
  }
  if (playbin_) {
    gst_element_set_state(playbin_, GST_STATE_NULL);
  }
}

void AudioPlayer::SendEvent(const EncodableValue& value) {
  // Marshal to the GLib main loop. Capturing state_ by value takes a shared
  // ref; the callback can safely check state->cancelled without touching the
  // AudioPlayer itself (which may have been destroyed by then).
  auto state = state_;
  auto payload = std::make_shared<EncodableValue>(value);
  ::PostToMainLoop([state, payload]() {
    std::lock_guard<std::mutex> lock(state->sink_mu);
    if (state->cancelled || !state->sink) {
      return;
    }
    state->sink->Success(*payload);
  });
}

void AudioPlayer::AboutToFinish(GstElement* playbin, AudioPlayer* self) {
  // Fires on the playbin streaming thread. Do not call Stop()/Pause() here —
  // they take GStreamer state-change locks and deadlock the thread driving
  // the pipeline.
  //
  // Note on EOS: GST_MESSAGE_EOS also fires reliably (see OnBusMessage).
  // We still hook about-to-finish because it's the only way to get GAPLESS
  // looping — EOS fires after pulsesink drains, producing a perceptible
  // silent gap before any Play() would restart. Setting playbin's uri from
  // this handler queues the next source before the current one finishes.
  if (!self->isLooping_.load(std::memory_order_relaxed)) {
    // Non-loop path: emit completion so Dart's state machine advances.
    if (!self->isPlaying_.exchange(false, std::memory_order_relaxed)) {
      return;
    }
    const EncodableValue value(EncodableMap{
        {EncodableValue("event"), EncodableValue("audio.onComplete")},
        {EncodableValue("value"), flutter::EncodableValue(true)},
    });
    self->SendEvent(value);
    return;
  }

  // Loop path: snapshot url_ under the mutex before touching playbin so we
  // don't race with SetSourceUrl on the platform thread.
  std::string url_copy;
  {
    std::lock_guard<std::mutex> lock(self->url_mu_);
    url_copy = self->url_;
  }
  if (!url_copy.empty()) {
    g_object_set(G_OBJECT(playbin), "uri", url_copy.c_str(), nullptr);
  }
}

// Returns true if `url` starts with one of the schemes we permit playbin to
// consume. Anything else (rtsp://, smb://, gst-pipeline://, etc.) lets the
// caller drive arbitrary GStreamer source elements, which is too much
// authority for a plugin that takes URLs from Dart.
static bool IsAllowedSourceUrl(const std::string& url) {
  static constexpr std::string_view kAllowedSchemes[] = {
      "file://",
      "http://",
      "https://",
      "data:",
  };
  return std::any_of(std::begin(kAllowedSchemes), std::end(kAllowedSchemes),
                     [&url](const std::string_view scheme) {
                       return url.compare(0, scheme.size(), scheme) == 0;
                     });
}

void AudioPlayer::SetSourceUrl(const std::string& url) {
  if (!url.empty() && !IsAllowedSourceUrl(url)) {
    ihs::log::warn(
        "[audioplayers] rejecting setSourceUrl with disallowed scheme "
        "channel={} url={}",
        state_->event_channel_name, url);
    OnError("LinuxAudioError",
            "URL scheme not permitted (allowed: file, http, https, data).",
            nullptr, nullptr);
    return;
  }
  // Always reset: Dart's setSource contract is "prepare this source from
  // scratch". A "same URL → no-op" shortcut would leave the pipeline wherever
  // the last playback left it (typically at EOS), so a subsequent resume of
  // the same clip would produce no audio.
  {
    std::lock_guard<std::mutex> lock(url_mu_);
    url_ = url;
  }
  gst_element_set_state(playbin_, GST_STATE_NULL);
  isInitialized_.store(false, std::memory_order_relaxed);
  isPlaying_.store(false, std::memory_order_relaxed);
  state_->discovered_duration_ms.store(-1, std::memory_order_relaxed);
  if (url.empty()) {
    return;
  }
  g_object_set(GST_OBJECT(playbin_), "uri", url.c_str(), NULL);
  StartDurationDiscovery(url);
  const GstStateChangeReturn ret =
      gst_element_set_state(playbin_, GST_STATE_READY);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    OnError("LinuxAudioError", "Unable to set the pipeline to GST_STATE_READY.",
            nullptr, nullptr);
  }
}

void AudioPlayer::SetSourceBytes(const std::vector<uint8_t>& bytes) {
  CleanupByteSource();

  const char* tmpdir = std::getenv("XDG_RUNTIME_DIR");
  if (!tmpdir || !*tmpdir) {
    tmpdir = "/tmp";
  }
  std::string path_template =
      std::string(tmpdir) + "/audioplayers_linux_XXXXXX";
  std::vector<char> mutable_template(path_template.begin(),
                                     path_template.end());
  mutable_template.push_back('\0');
  const int fd = mkstemp(mutable_template.data());
  if (fd < 0) {
    OnError("LinuxAudioError", "Failed to create temp file for setSourceBytes.",
            nullptr, nullptr);
    return;
  }

  const char* data = reinterpret_cast<const char*>(bytes.data());
  size_t remaining = bytes.size();
  while (remaining > 0) {
    const ssize_t n = write(fd, data, remaining);
    if (n < 0) {
      close(fd);
      unlink(mutable_template.data());
      OnError("LinuxAudioError", "Failed to write bytes for setSourceBytes.",
              nullptr, nullptr);
      return;
    }
    data += n;
    remaining -= static_cast<size_t>(n);
  }
  close(fd);

  byte_source_path_ = mutable_template.data();
  SetSourceUrl(std::string("file://") + byte_source_path_);
}

void AudioPlayer::CleanupByteSource() {
  if (!byte_source_path_.empty()) {
    unlink(byte_source_path_.c_str());
    byte_source_path_.clear();
  }
}

void AudioPlayer::ReleaseMediaSource() {
  isPlaying_.store(false, std::memory_order_relaxed);
  isInitialized_.store(false, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(url_mu_);
    url_.clear();
  }

  // Bounded wait: GST_CLOCK_TIME_NONE could hang the caller forever if the
  // pipeline got wedged.
  GstState playbinState;
  gst_element_get_state(playbin_, &playbinState, nullptr, 2 * GST_SECOND);
  if (playbinState > GST_STATE_NULL) {
    gst_element_set_state(playbin_, GST_STATE_NULL);
  }
}

gboolean AudioPlayer::OnBusMessage(GstBus* /* bus */,
                                   GstMessage* message,
                                   AudioPlayer* data) {
  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
      GError* err;
      gchar* debug;
      gst_message_parse_error(message, &err, &debug);
      ihs::log::error(
          "[audioplayers] gst error channel={} domain={} code={} message={} "
          "debug={}",
          data->state_->event_channel_name, g_quark_to_string(err->domain),
          err->code, err->message ? err->message : "", debug ? debug : "");
      data->OnMediaError(GST_MESSAGE_SRC(message), err, debug);
      g_error_free(err);
      g_free(debug);
      break;
    }
    case GST_MESSAGE_WARNING: {
      GError* err;
      gchar* debug;
      gst_message_parse_warning(message, &err, &debug);
      ihs::log::warn(
          "[audioplayers] gst warning channel={} domain={} code={} message={} "
          "debug={}",
          data->state_->event_channel_name, g_quark_to_string(err->domain),
          err->code, err->message ? err->message : "", debug ? debug : "");
      g_error_free(err);
      g_free(debug);
      break;
    }
    case GST_MESSAGE_NEW_CLOCK:
      if (GST_MESSAGE_SRC(message) == GST_OBJECT(data->playbin_)) {
        data->OnDurationUpdate();
      }
      break;
    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state;
      gst_message_parse_state_changed(message, &old_state, &new_state, nullptr);
      data->OnMediaStateChange(GST_MESSAGE_SRC(message), &old_state,
                               &new_state);
      break;
    }
    case GST_MESSAGE_EOS:
      if (GST_MESSAGE_SRC(message) == GST_OBJECT(data->playbin_) &&
          data->isPlaying_.load(std::memory_order_relaxed)) {
        data->OnPlaybackEnded();
      }
      break;
    case GST_MESSAGE_DURATION_CHANGED:
      data->OnDurationUpdate();
      break;
    case GST_MESSAGE_ASYNC_DONE:
      if (GST_MESSAGE_SRC(message) == GST_OBJECT(data->playbin_)) {
        bool expected = false;
        if (data->isSeekCompleted_.compare_exchange_strong(expected, true)) {
          data->OnSeekCompleted();
        }
      }
      break;
    default:
      // For more GstMessage types see:
      // https://gstreamer.freedesktop.org/documentation/gstreamer/gstmessage.html?gi-language=c#enumerations
      break;
  }

  // Continue watching for messages.
  return TRUE;
}

void AudioPlayer::OnMediaError(GstObject* src,
                               GError* error,
                               gchar* /* debug */) {
  // Suppress errors from elements no longer attached to playbin_. These are
  // transient artifacts of source transitions — when setSourceUrl tears down
  // the old uridecodebin or about-to-finish swaps the URI for gapless loop,
  // the old decoder chain keeps pushing buffers briefly and its srcpad has
  // no peer, producing GST_STREAM_ERROR "streaming stopped, reason
  // not-linked". Real errors come from elements still inside playbin and
  // reach Dart normally.
  if (src && playbin_ &&
      !gst_object_has_as_ancestor(src, GST_OBJECT(playbin_))) {
    ihs::log::debug(
        "[audioplayers] suppressing orphaned error channel={} src={} "
        "message={}",
        state_->event_channel_name,
        GST_OBJECT_NAME(src) ? GST_OBJECT_NAME(src) : "(null)",
        error->message ? error->message : "");
    return;
  }

  const auto code = "LinuxAudioError";
  gchar const* message;
  const auto details_str = std::string(error->message) + " (Domain: " +
                           std::string(g_quark_to_string(error->domain)) +
                           ", Code: " + std::to_string(error->code) + ")";
  EncodableValue details(details_str.c_str());
  // https://gstreamer.freedesktop.org/documentation/gstreamer/gsterror.html#enumerations
  if (error->domain == GST_STREAM_ERROR) {
    message =
        "Failed to set source. For troubleshooting, "
        "see: " STR_LINK_TROUBLESHOOTING;
  } else {
    message = "Unknown GstGError. See details.";
  }
  OnError(code, message, &details, &error);
}

void AudioPlayer::OnError(const gchar* code,
                          const gchar* message,
                          EncodableValue* details,
                          GError** /* error */) {
  // Errors must go through EventSink::Error, NOT Success with a map. Dart's
  // EventChannel parser delivers Success(value) through .listen().map() and
  // Error(code, msg, details) through the stream's onError. Sending a
  // {"code", "message"} map via Success made Dart's event-type dispatch
  // throw UnimplementedError because there was no "event" key.
  auto state = state_;
  std::string code_s = code ? code : "";
  std::string message_s = message ? message : "";
  std::shared_ptr<EncodableValue> details_copy;
  if (details) {
    details_copy = std::make_shared<EncodableValue>(*details);
  }
  ::PostToMainLoop([state, code_s = std::move(code_s),
                    message_s = std::move(message_s),
                    details_copy = std::move(details_copy)]() mutable {
    std::lock_guard<std::mutex> lock(state->sink_mu);
    if (state->cancelled || !state->sink) {
      return;
    }
    if (details_copy) {
      state->sink->Error(code_s, message_s, *details_copy);
    } else {
      state->sink->Error(code_s, message_s);
    }
  });
}

void AudioPlayer::OnMediaStateChange(const GstObject* src,
                                     const GstState* old_state,
                                     const GstState* new_state) {
  media_state_ = *new_state;

  if (!playbin_) {
    OnError("LinuxAudioError",
            "Player was already disposed (OnMediaStateChange).", nullptr,
            nullptr);
    return;
  }

  if (src == GST_OBJECT(playbin_)) {
    if (*new_state == GST_STATE_READY) {
      // Need to set to pause state, in order to make player functional.
      const GstStateChangeReturn ret =
          gst_element_set_state(playbin_, GST_STATE_PAUSED);
      if (ret == GST_STATE_CHANGE_FAILURE) {
        const auto error_description =
            "Unable to set the pipeline from GST_STATE_READY to "
            "GST_STATE_PAUSED.";
        if (isInitialized_.load(std::memory_order_relaxed)) {
          OnError("LinuxAudioError", error_description, nullptr, nullptr);
        } else {
          EncodableValue details(error_description);
          OnError("LinuxAudioError",
                  "Failed to set source. For troubleshooting, "
                  "see: " STR_LINK_TROUBLESHOOTING,
                  &details, nullptr);
        }
      }
      isInitialized_.store(false, std::memory_order_relaxed);
    } else if (*old_state == GST_STATE_PAUSED &&
               *new_state == GST_STATE_PLAYING) {
      OnDurationUpdate();
    } else if (*new_state >= GST_STATE_PAUSED) {
      bool was_initialized = isInitialized_.exchange(true);
      if (!was_initialized) {
        OnPrepared(true);
        if (isPlaying_.load(std::memory_order_relaxed)) {
          Resume();
        }
      }
    } else {
      isInitialized_.store(false, std::memory_order_relaxed);
    }
  }
}

void AudioPlayer::OnPrepared(bool isPrepared) {
  const EncodableValue value(EncodableMap{
      {EncodableValue("event"), EncodableValue("audio.onPrepared")},
      {EncodableValue("value"), flutter::EncodableValue(isPrepared)},
  });
  SendEvent(value);
}

void AudioPlayer::OnDurationUpdate() {
  const EncodableValue value(EncodableMap{
      {EncodableValue("event"), EncodableValue("audio.onDuration")},
      {EncodableValue("value"),
       flutter::EncodableValue(GetDuration().value_or(0))},
  });
  SendEvent(value);
}

void AudioPlayer::OnSeekCompleted() {
  const EncodableValue value(EncodableMap{
      {EncodableValue("event"), EncodableValue("audio.onSeekComplete")},
      {EncodableValue("value"), flutter::EncodableValue(true)},
  });
  SendEvent(value);
}

void AudioPlayer::OnPlaybackEnded() {
  const EncodableValue value(EncodableMap{
      {EncodableValue("event"), EncodableValue("audio.onComplete")},
      {EncodableValue("value"), flutter::EncodableValue(true)},
  });
  SendEvent(value);

  if (GetLooping()) {
    Play();
  } else {
    Stop();
  }
}

void AudioPlayer::OnLog(const gchar* message) {
  const EncodableValue value(EncodableMap{
      {EncodableValue("event"), EncodableValue("audio.onLog")},
      {EncodableValue("value"), flutter::EncodableValue(std::string(message))},
  });
  SendEvent(value);
}

void AudioPlayer::SetBalance(float balance) {
  if (!panorama_) {
    OnLog("Audiopanorama was not initialized");
    return;
  }

  if (balance > 1.0f) {
    balance = 1.0f;
  } else if (balance < -1.0f) {
    balance = -1.0f;
  }
  g_object_set(G_OBJECT(panorama_), "panorama", balance, NULL);
}

void AudioPlayer::SetLooping(const bool isLooping) {
  isLooping_.store(isLooping, std::memory_order_relaxed);
}

bool AudioPlayer::GetLooping() const {
  return isLooping_.load(std::memory_order_relaxed);
}

void AudioPlayer::SetVolume(double volume) const {
  if (volume > 1) {
    volume = 1;
  } else if (volume < 0) {
    volume = 0;
  }
  g_object_set(G_OBJECT(playbin_), "volume", volume, NULL);
}

/**
 * A rate of 1.0 means normal playback rate, 2.0 means double speed.
 * Negative values means backwards playback.
 * A value of 0.0 will pause the player.
 *
 * @param seekTo the position in milliseconds
 * @param rate the playback rate (speed)
 */
void AudioPlayer::SetPlayback(const int64_t seekTo, const double rate) {
  if (rate != 0 && playbackRate_ != rate) {
    playbackRate_ = rate;
  }

  if (!isInitialized_.load(std::memory_order_relaxed)) {
    return;
  }
  // See:
  // https://gstreamer.freedesktop.org/documentation/tutorials/basic/playback-speed.html?gi-language=c
  if (!isSeekCompleted_.load(std::memory_order_relaxed)) {
    return;
  }
  if (rate == 0) {
    // Do not set rate if it's 0, rather pause.
    Pause();
    return;
  }

  isSeekCompleted_.store(false, std::memory_order_relaxed);

  GstEvent* seek_event;
  if (rate > 0) {
    seek_event = gst_event_new_seek(
        rate, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        GST_SEEK_TYPE_SET, seekTo * GST_MSECOND, GST_SEEK_TYPE_NONE, -1);
  } else {
    seek_event = gst_event_new_seek(
        rate, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, seekTo * GST_MSECOND);
  }

  if (!gst_element_send_event(playbin_, seek_event)) {
    OnLog((std::string("Could not set playback to position ") +
           std::to_string(seekTo) + std::string(" and rate ") +
           std::to_string(rate) + std::string("."))
              .c_str());
    isSeekCompleted_.store(true, std::memory_order_relaxed);
  }
}

void AudioPlayer::SetPlaybackRate(const double rate) {
  SetPlayback(GetPosition().value_or(0), rate);
}

/**
 * @param position the position in milliseconds
 */
void AudioPlayer::SetPosition(const int64_t position) {
  if (!isInitialized_.load(std::memory_order_relaxed)) {
    return;
  }
  // Clamp to [0, duration]. gst_event_new_seek accepts out-of-range values
  // but their behavior depends on the source — some elements reject, some
  // clamp silently, some seek past EOS. Normalize here so Dart gets
  // consistent behavior regardless of the format.
  int64_t clamped = position < 0 ? 0 : position;
  if (const auto duration = GetDuration(); duration.has_value()) {
    if (clamped > *duration) {
      clamped = *duration;
    }
  }
  SetPlayback(clamped, playbackRate_);
}

/**
 * @return int64_t the position in milliseconds
 */
std::optional<int64_t> AudioPlayer::GetPosition() {
  gint64 current = 0;
  if (!gst_element_query_position(playbin_, GST_FORMAT_TIME, &current)) {
    // Position queries fail transiently while the pipeline preroll/seek is
    // in flight. Dart polls several times per second; firing OnLog here
    // floods the app log for no actionable benefit. Return nullopt and let
    // the caller fall back to its cached value.
    return std::nullopt;
  }
  return std::make_optional(current / 1000000);
}

/**
 * @return int64_t the duration in milliseconds
 */
std::optional<int64_t> AudioPlayer::GetDuration() {
  // Prefer the GstDiscoverer result when available — gst_element_query_duration
  // is unreliable for variable-bitrate MP3s (the original FIXME).
  const int64_t discovered =
      state_->discovered_duration_ms.load(std::memory_order_relaxed);
  if (discovered >= 0) {
    return std::make_optional(discovered);
  }
  gint64 duration = 0;
  if (!gst_element_query_duration(playbin_, GST_FORMAT_TIME, &duration)) {
    // Duration queries are unreliable before PAUSED/PLAYING and for some
    // formats. GstDiscoverer result (cached above) is the preferred path; if
    // we got here, just return nullopt instead of log-spamming Dart.
    return std::nullopt;
  }
  return std::make_optional(duration / 1000000);
}

void AudioPlayer::StartDurationDiscovery(const std::string& uri) {
  // GstDiscoverer runs a 5s-timeout probe on a worker thread so we don't
  // stall the calling thread. The thread captures the SharedState by shared
  // ref — if AudioPlayer is destroyed mid-probe, the state survives until
  // the thread finishes, and the store into discovered_duration_ms is safe
  // (the consumer is gone too, but the memory is still valid).
  auto state = state_;
  std::thread([state, uri]() {
    GError* err = nullptr;
    GstDiscoverer* discoverer = gst_discoverer_new(5 * GST_SECOND, &err);
    if (!discoverer) {
      ihs::log::warn("[audioplayers] gst_discoverer_new failed: {}",
                     err ? err->message : "unknown");
      if (err)
        g_error_free(err);
      return;
    }
    GstDiscovererInfo* info =
        gst_discoverer_discover_uri(discoverer, uri.c_str(), &err);
    if (info) {
      const GstClockTime dur = gst_discoverer_info_get_duration(info);
      if (GST_CLOCK_TIME_IS_VALID(dur)) {
        state->discovered_duration_ms.store(
            static_cast<int64_t>(dur / GST_MSECOND), std::memory_order_relaxed);
      }
      gst_discoverer_info_unref(info);
    } else if (err) {
      ihs::log::warn("[audioplayers] gst_discoverer probe failed for {}: {}",
                     uri, err->message);
      g_error_free(err);
    }
    g_object_unref(discoverer);
  }).detach();
}

void AudioPlayer::Play() {
  SetPosition(0);
  Resume();
}

void AudioPlayer::Pause() {
  isPlaying_.store(false, std::memory_order_relaxed);
  if (!isInitialized_.load(std::memory_order_relaxed)) {
    return;
  }
  const GstStateChangeReturn ret =
      gst_element_set_state(playbin_, GST_STATE_PAUSED);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    OnError("LinuxAudioError",
            "Unable to set the pipeline to GST_STATE_PAUSED.", nullptr,
            nullptr);
  }
}

void AudioPlayer::Stop() {
  Pause();
  if (!isInitialized_.load(std::memory_order_relaxed)) {
    return;
  }
  SetPosition(0);
  // Wait for the seek/state-change to settle. Bounded so a wedged element
  // can't hang the calling thread.
  constexpr GstClockTime kStopTimeout = 2 * GST_SECOND;
  const GstStateChangeReturn ret =
      gst_element_get_state(playbin_, nullptr, nullptr, kStopTimeout);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    OnError("LinuxAudioError",
            "Unable to seek playback to '0' while stopping the player.",
            nullptr, nullptr);
  } else if (ret == GST_STATE_CHANGE_ASYNC) {
    ihs::log::warn(
        "[audioplayers] Stop timed out after 2s waiting for state settle "
        "channel={}",
        state_->event_channel_name);
  }
}

void AudioPlayer::Resume() {
  isPlaying_.store(true, std::memory_order_relaxed);
  if (!isInitialized_.load(std::memory_order_relaxed)) {
    return;
  }
  const GstStateChangeReturn ret =
      gst_element_set_state(playbin_, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_SUCCESS) {
    // Update duration when start playing, as no event is emitted elsewhere.
    OnDurationUpdate();
  } else if (ret == GST_STATE_CHANGE_FAILURE) {
    OnError("LinuxAudioError",
            "Unable to set the pipeline to GST_STATE_PLAYING.", nullptr,
            nullptr);
  }
}

void AudioPlayer::Dispose() {
  if (!playbin_) {
    ihs::log::warn(
        "[audioplayers] Dispose() called on already-disposed player "
        "channel={}",
        state_->event_channel_name);
    return;
  }

  ReleaseMediaSource();
  CleanupByteSource();

  if (bus_) {
    gst_bus_remove_watch(bus_);
    gst_object_unref(GST_OBJECT(bus_));
    bus_ = nullptr;
  }

  if (panorama_) {
    gst_element_set_state(audiobin_, GST_STATE_NULL);

    gst_element_remove_pad(audiobin_, panoramaSinkPad_);
    gst_bin_remove(GST_BIN(audiobin_), audiosink_);
    gst_bin_remove(GST_BIN(audiobin_), panorama_);

    // audiobin gets unreferenced (2x) via playbin.
    panorama_ = nullptr;
  }

  gst_object_unref(GST_OBJECT(playbin_));
  playbin_ = nullptr;
}
