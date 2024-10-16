
#include "audio_player.h"

#include <flutter/standard_message_codec.h>

#define STR_LINK_TROUBLESHOOTING \
  "https://github.com/bluefireteam/audioplayers/blob/main/troubleshooting.md"

AudioPlayer::AudioPlayer(const std::string& playerId,
                         BinaryMessenger* messenger)
    : BasicMessageChannel(messenger,
                          playerId,
                          &StandardMessageCodec::GetInstance()),
      media_state_(GST_STATE_VOID_PENDING) {
  SetMessageHandler([&](const EncodableValue& /* message */,
                        const MessageReply<EncodableValue>& reply) {
    reply(EncodableValue());
  });

  // Get the calling context.
  context_ = g_main_context_get_thread_default();

  playbin_ = gst_element_factory_make("playbin", nullptr);
  if (!playbin_) {
    throw std::runtime_error("Not all elements could be created.");
  }

  // Setup stereo balance controller
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

  // Setup source options
  g_signal_connect(playbin_, "source-setup",
                   G_CALLBACK(AudioPlayer::SourceSetup), &source_);

  bus_ = gst_element_get_bus(playbin_);

  // Watch bus messages for one time events
  gst_bus_add_watch(bus_, reinterpret_cast<GstBusFunc>(OnBusMessage), this);
}

AudioPlayer::~AudioPlayer() {
  gst_element_set_state(playbin_, GST_STATE_NULL);
}

void AudioPlayer::SourceSetup(GstElement* /* playbin */,
                              GstElement* source,
                              GstElement** /* p_src */) {
  // Allow sources from unencrypted / misconfigured connections
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "ssl-strict") !=
      nullptr) {
    g_object_set(G_OBJECT(source), "ssl-strict", FALSE, NULL);
  }
}

void AudioPlayer::SetSourceUrl(const std::string& url) {
  if (url_ != url) {
    url_ = url;
    // clear source
    gst_element_set_state(playbin_, GST_STATE_NULL);
    isInitialized_ = false;
    isPlaying_ = false;
    if (!url_.empty()) {
      g_object_set(GST_OBJECT(playbin_), "uri", url_.c_str(), NULL);
      if (playbin_ && playbin_->current_state != GST_STATE_READY) {
        const GstStateChangeReturn ret =
            gst_element_set_state(playbin_, GST_STATE_READY);
        if (ret == GST_STATE_CHANGE_FAILURE) {
          throw std::runtime_error(
              "Unable to set the pipeline to GST_STATE_READY.");
        }
      }
    }
  } else {
    OnPrepared(true);
  }
}

void AudioPlayer::ReleaseMediaSource() {
  if (isPlaying_)
    isPlaying_ = false;
  if (isInitialized_)
    isInitialized_ = false;
  url_.clear();

  GstState playbinState;
  gst_element_get_state(playbin_, &playbinState, nullptr, GST_CLOCK_TIME_NONE);
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
      data->OnMediaError(err, debug);
      g_error_free(err);
      g_free(debug);
      break;
    }
    case GST_MESSAGE_NEW_CLOCK:
      if (GST_MESSAGE_SRC(message) == GST_OBJECT(data->playbin_)) {
        data->OnDurationUpdate();
      }
      break;
    case GST_MESSAGE_STATE_CHANGED:
      GstState old_state, new_state;

      gst_message_parse_state_changed(message, &old_state, &new_state, nullptr);
      data->OnMediaStateChange(GST_MESSAGE_SRC(message), &old_state,
                               &new_state);
      break;
    case GST_MESSAGE_EOS:
      if (GST_MESSAGE_SRC(message) == GST_OBJECT(data->playbin_)) {
        if (data->isPlaying_) {
          data->OnPlaybackEnded();
        }
      }
      break;
    case GST_MESSAGE_DURATION_CHANGED:
      data->OnDurationUpdate();
      break;
    case GST_MESSAGE_ASYNC_DONE:
      if (GST_MESSAGE_SRC(message) == GST_OBJECT(data->playbin_)) {
        if (!data->isSeekCompleted_) {
          data->OnSeekCompleted();
          data->isSeekCompleted_ = true;
        }
      }
      break;
    default:
      // For more GstMessage types see:
      // https://gstreamer.freedesktop.org/documentation/gstreamer/gstmessage.html?gi-language=c#enumerations
      break;
  }

  // Continue watching for messages
  return TRUE;
}

void AudioPlayer::OnMediaError(GError* error, gchar* /* debug */) {
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
                          EncodableValue* /* details */,
                          GError** /* error */) {
  const EncodableValue value(
      EncodableMap{{EncodableValue("code"), EncodableValue(code)},
                   {EncodableValue("message"), EncodableValue(message)}});
  Send(value);
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
      // Need to set to pause state, in order to make player functional
      const GstStateChangeReturn ret =
          gst_element_set_state(playbin_, GST_STATE_PAUSED);
      if (ret == GST_STATE_CHANGE_FAILURE) {
        const auto error_description =
            "Unable to set the pipeline from GST_STATE_READY to "
            "GST_STATE_PAUSED.";
        if (isInitialized_) {
          OnError("LinuxAudioError", error_description, nullptr, nullptr);
        } else {
          EncodableValue details(error_description);
          OnError("LinuxAudioError",
                  "Failed to set source. For troubleshooting, "
                  "see: " STR_LINK_TROUBLESHOOTING,
                  &details, nullptr);
        }
      }
      if (isInitialized_) {
        isInitialized_ = false;
      }
    } else if (*old_state == GST_STATE_PAUSED &&
               *new_state == GST_STATE_PLAYING) {
      OnDurationUpdate();
    } else if (*new_state >= GST_STATE_PAUSED) {
      if (!isInitialized_) {
        isInitialized_ = true;
        OnPrepared(true);
        if (isPlaying_) {
          Resume();
        }
      }
    } else if (isInitialized_) {
      isInitialized_ = false;
    }
  }
}

void AudioPlayer::OnPrepared(bool isPrepared) {
  if (media_state_ != GST_STATE_PLAYING) {
    Resume();
  }
  const EncodableValue value(EncodableMap{
      {EncodableValue("event"), EncodableValue("audio.onPrepared")},
      {EncodableValue("value"), flutter::EncodableValue(isPrepared)},
  });
  Send(value);
}

void AudioPlayer::OnDurationUpdate() {
  const EncodableValue value(EncodableMap{
      {EncodableValue("event"), EncodableValue("audio.onDuration")},
      {EncodableValue("value"),
       flutter::EncodableValue(GetDuration().value_or(0))},
  });
  Send(value);
}

void AudioPlayer::OnSeekCompleted() {
  const EncodableValue value(EncodableMap{
      {EncodableValue("event"), EncodableValue("audio.onSeekComplete")},
      {EncodableValue("value"), flutter::EncodableValue(true)},
  });
  Send(value);
}

void AudioPlayer::OnPlaybackEnded() {
  const EncodableValue value(EncodableMap{
      {EncodableValue("event"), EncodableValue("audio.onComplete")},
      {EncodableValue("value"), flutter::EncodableValue(true)},
  });
  Send(value);

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
  Send(value);
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
  isLooping_ = isLooping;
}

bool AudioPlayer::GetLooping() const {
  return isLooping_;
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

  if (!isInitialized_) {
    return;
  }
  // See:
  // https://gstreamer.freedesktop.org/documentation/tutorials/basic/playback-speed.html?gi-language=c
  if (!isSeekCompleted_) {
    return;
  }
  if (rate == 0) {
    // Do not set rate if it's 0, rather pause.
    Pause();
    return;
  }

  isSeekCompleted_ = false;

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
    isSeekCompleted_ = true;
  }
}

void AudioPlayer::SetPlaybackRate(const double rate) {
  SetPlayback(GetPosition().value_or(0), rate);
}

/**
 * @param position the position in milliseconds
 */
void AudioPlayer::SetPosition(const int64_t position) {
  if (!isInitialized_) {
    return;
  }
  SetPlayback(position, playbackRate_);
}

/**
 * @return int64_t the position in milliseconds
 */
std::optional<int64_t> AudioPlayer::GetPosition() {
  gint64 current = 0;
  if (!gst_element_query_position(playbin_, GST_FORMAT_TIME, &current)) {
    OnLog("Could not query current position.");
    return std::nullopt;
  }
  return std::make_optional(current / 1000000);
}

/**
 * @return int64_t the duration in milliseconds
 */
std::optional<int64_t> AudioPlayer::GetDuration() {
  gint64 duration = 0;
  if (!gst_element_query_duration(playbin_, GST_FORMAT_TIME, &duration)) {
    // FIXME: Get duration for MP3 with variable bit rate with gst-discoverer:
    // https://gstreamer.freedesktop.org/documentation/pbutils/gstdiscoverer.html?gi-language=c#gst_discoverer_info_get_duration
    OnLog("Could not query current duration.");
    return std::nullopt;
  }
  return std::make_optional(duration / 1000000);
}

void AudioPlayer::Play() {
  SetPosition(0);
  Resume();
}

void AudioPlayer::Pause() {
  if (isPlaying_) {
    isPlaying_ = false;
  }
  if (!isInitialized_) {
    return;
  }
  if (const GstStateChangeReturn ret =
          gst_element_set_state(playbin_, GST_STATE_PAUSED);
      ret == GST_STATE_CHANGE_SUCCESS) {
  } else if (ret == GST_STATE_CHANGE_FAILURE) {
    throw std::runtime_error(
        std::runtime_error("Unable to set the pipeline to GST_STATE_PAUSED."));
  }
}

void AudioPlayer::Stop() {
  Pause();
  if (!isInitialized_) {
    return;
  }
  SetPosition(0);
  // Block thread to wait for state, as it is not expected to be waited to
  // "seek complete" event on the dart side.
  const GstStateChangeReturn ret =
      gst_element_get_state(playbin_, nullptr, nullptr, GST_CLOCK_TIME_NONE);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    throw std::runtime_error(
        "Unable to seek playback to '0' while stopping the player.");
  }
}

void AudioPlayer::Resume() {
  if (!isPlaying_) {
    isPlaying_ = true;
  }
  if (!isInitialized_) {
    return;
  }
  if (const GstStateChangeReturn ret =
          gst_element_set_state(playbin_, GST_STATE_PLAYING);
      ret == GST_STATE_CHANGE_SUCCESS) {
    // Update duration when start playing, as no event is emitted elsewhere
    OnDurationUpdate();
  } else if (ret == GST_STATE_CHANGE_FAILURE) {
    throw std::runtime_error(
        "Unable to set the pipeline to GST_STATE_PLAYING.");
  }
}

void AudioPlayer::Dispose() {
  if (!playbin_)
    throw std::runtime_error("Player was already disposed (Dispose)");

  ReleaseMediaSource();

  if (bus_) {
    gst_bus_remove_watch(bus_);
    gst_object_unref(GST_OBJECT(bus_));
    bus_ = nullptr;
  }

  if (source_) {
    gst_object_unref(GST_OBJECT(source_));
    source_ = nullptr;
  }

  if (panorama_) {
    gst_element_set_state(audiobin_, GST_STATE_NULL);

    gst_element_remove_pad(audiobin_, panoramaSinkPad_);
    gst_bin_remove(GST_BIN(audiobin_), audiosink_);
    gst_bin_remove(GST_BIN(audiobin_), panorama_);

    // audiobin gets unreferenced (2x) via playbin
    panorama_ = nullptr;
  }

  gst_object_unref(GST_OBJECT(playbin_));
  playbin_ = nullptr;
}
