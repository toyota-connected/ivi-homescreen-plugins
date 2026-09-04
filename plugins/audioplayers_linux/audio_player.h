#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <flutter/binary_messenger.h>
#include <flutter/encodable_value.h>
#include <flutter/event_channel.h>
#include <flutter/event_sink.h>

#include <gst/gst.h>

using namespace flutter;

class AudioPlayer {
 public:
  AudioPlayer(std::string playerId, BinaryMessenger* messenger);

  ~AudioPlayer();

  std::optional<int64_t> GetPosition();

  std::optional<int64_t> GetDuration();

  [[nodiscard]] bool GetLooping() const;

  void Play();

  void Pause();

  void Stop();

  void Resume();

  void Dispose();

  void SetBalance(float balance);

  void SetLooping(bool isLooping);

  void SetVolume(double volume) const;

  void SetPlaybackRate(double rate);

  void SetPosition(int64_t position);

  void SetSourceUrl(const std::string& url);

  void SetSourceBytes(const std::vector<uint8_t>& bytes);

  void ReleaseMediaSource();

  void OnError(const gchar* code,
               const gchar* message,
               EncodableValue* details,
               GError** error);

  void OnLog(const gchar* message);

 private:
  // State shared with background workers (GstDiscoverer thread, GLib idle
  // callbacks). Held by shared_ptr so callbacks can take a strong ref; the
  // AudioPlayer destructor sets `cancelled` so late-firing callbacks become
  // no-ops instead of dereferencing a destroyed player.
  struct SharedState {
    std::string event_channel_name;

    // Event delivery.
    std::mutex sink_mu;
    std::unique_ptr<flutter::EventSink<>> sink;
    bool cancelled = false;  // guarded by sink_mu

    // Cached duration discovered out-of-band via GstDiscoverer. Populated by
    // the detached discovery thread, read by GetDuration. -1 means "no
    // value, fall back to the playbin query". gst_element_query_duration on
    // playbin is unreliable for VBR MP3s.
    std::atomic<int64_t> discovered_duration_ms{-1};
  };

  const std::shared_ptr<SharedState> state_;
  GstState media_state_;

  // Gst members
  GstElement* playbin_{};
  GstElement* panorama_{};
  GstElement* audiobin_{};
  GstElement* audiosink_{};
  GstPad* panoramaSinkPad_{};
  GstBus* bus_{};

  std::atomic<bool> isInitialized_{false};
  std::atomic<bool> isPlaying_{false};
  std::atomic<bool> isLooping_{false};
  std::atomic<bool> isSeekCompleted_{true};
  double playbackRate_ = 1.0;

  // url_ is written by SetSourceUrl (platform thread) and read by
  // AboutToFinish (GStreamer streaming thread). std::string assignment isn't
  // atomic, so guard both sides.
  std::mutex url_mu_;
  std::string url_;
  std::string byte_source_path_;  // only touched from platform thread

  std::unique_ptr<flutter::EventChannel<>> event_channel_;

  void SendEvent(const EncodableValue& value);

  void StartDurationDiscovery(const std::string& uri);

  void CleanupByteSource();

  // Post a task to the GLib main-loop thread. Tasks capture a
  // shared_ptr<SharedState> and check `cancelled` before dereferencing
  // anything that lives on the AudioPlayer.
  static void PostToMainLoop(std::function<void()> task);

  static void AboutToFinish(GstElement* playbin, AudioPlayer* self);

  static gboolean OnBusMessage(GstBus* bus,
                               GstMessage* message,
                               AudioPlayer* data);

  void SetPlayback(int64_t seekTo, double rate);

  void OnMediaError(GstObject* src, GError* error, gchar* debug);

  void OnMediaStateChange(const GstObject* src,
                          const GstState* old_state,
                          const GstState* new_state);

  void OnDurationUpdate();

  void OnSeekCompleted();

  void OnPlaybackEnded();

  void OnPrepared(bool isPrepared);
};
