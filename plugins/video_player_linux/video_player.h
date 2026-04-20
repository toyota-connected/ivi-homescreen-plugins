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

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <flutter/event_channel.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/plugin_registrar_homescreen.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "backend_interface.h"
#include "config.h"
#include "dmabuf_frame.h"
#include "nv12.h"
#include "stats.h"

extern "C" {
#include <gst/gst.h>
#include <gst/video/video.h>
#include <libudev.h>
}

#include "messages.g.h"

class Backend;

namespace video_player_linux {

/// Media stream information populated by `GstDiscoverer` before player
/// construction. Carries everything required to decide between A/V and
/// audio-only modes and to seed the initial Flutter event payloads.
struct MediaInfo {
  int width = 0;
  int height = 0;
  gint64 duration = 0;
  bool has_video = false;
  bool has_audio = false;
  gint n_audio_streams = 0;
  // Short codec name derived from the video stream's caps:
  // "h264", "h265", "vp8", "vp9", "av1", "mjpeg". Used as the key
  // for BackendRegistry::Select(). Empty when no video stream or an
  // unrecognized caps name — callers must then fall back to playbin
  // auto-plug.
  std::string video_codec;
  std::string audio_codec;
  int audio_channels = 0;
  int audio_sample_rate = 0;

  // Embedded album art (typically front cover) extracted from tags.
  std::vector<uint8_t> album_art;
  std::string album_art_mime;

  // Text metadata extracted from tags.
  std::string title;
  std::string artist;
  std::string album;
  std::string album_artist;
  std::string genre;
  int track_number = 0;
};

class VideoPlayer {
 public:
  VideoPlayer(flutter::PluginRegistrarDesktop* registrar,
              FlutterDesktopPluginRegistrarRef raw_registrar,
              std::string uri,
              std::map<std::string, std::string> http_headers,
              const MediaInfo& info,
              Config config,
              PlatformProfile platform_profile);
  ~VideoPlayer();

  void Dispose();
  void SetLooping(bool isLooping);
  void SetVolume(double volume);
  void SetPlaybackSpeed(double playbackSpeed);
  void Play();
  void Pause();
  int64_t GetPosition();
  void SendBufferingUpdate();
  void SeekTo(int64_t seek);
  int64_t GetTextureId() const { return m_texture_id; };
  bool IsValid();
  bool IsAudioOnly() const { return !has_video_; }

  // GL texture accessors used by the compositor-surface path (platform-view
  // presentation). `GetGlTextureName()` returns the GL_TEXTURE_2D name the
  // GStreamer pipeline is writing into this frame; 0 until the first frame
  // has been uploaded. `GetGlTextureWidth/Height()` report the decoded
  // frame dimensions, which may differ from the widget's layout size.
  [[nodiscard]] uint32_t GetGlTextureName() const;
  [[nodiscard]] int32_t GetGlTextureWidth() const { return width_; }
  [[nodiscard]] int32_t GetGlTextureHeight() const { return height_; }

  // Phase 1 — audio control surface
  int GetAudioTrackCount();
  void SetAudioTrack(int index);
  void SetOutputChannels(int channels);
  void SetMute(bool mute);

  // Phase 2 — quality & tuning
  void SetScaleMethod(int method);
  void SetAVOffset(int64_t offset_ms);
  void SetSubtitlesEnabled(bool enabled);
  int GetSubtitleTrackCount();
  void SetSubtitleTrack(int index);
  void SetSubtitleUri(const std::string& uri);
  void SetSubtitleFont(const std::string& font_desc);
  void SetChannelMixPreset(const std::string& preset);

  // Phase 3 — premium features
  void SetEqualizer(const std::vector<double>& bands);
  void SetVideoBalance(double brightness,
                       double contrast,
                       double saturation,
                       double hue);
  void SetAudioPassthrough(bool enabled);
  void SetChannelMixMatrix(int in_channels,
                           int out_channels,
                           const std::vector<double>& matrix);

  // Initializes the video player.
  void Init(flutter::BinaryMessenger* messenger);

 private:
  flutter::PluginRegistrarDesktop* m_registrar;
  FlutterDesktopPluginRegistrarRef m_raw_registrar;
  std::string uri_;
  std::map<std::string, std::string> http_headers_;
  GLsizei width_{};
  GLsizei height_{};
  gint64 duration_{};
  bool has_video_{true};

  // Phase 2.6 — runtime configuration + chosen backend. Copy of the
  // Config because VideoPlayer outlives the plugin-owned Config if
  // the plugin is torn down mid-stream. `selected_backend_` is a
  // non-owning pointer into BackendRegistry (process-wide singleton,
  // so lifetime is safe). nullptr means "no backend claimed this
  // codec — fall back to playbin auto-plug".
  Config config_;
  PlatformProfile platform_profile_{PlatformProfile::Auto};
  VideoDecoderBackend* selected_backend_{nullptr};
  std::string video_codec_;  // short codec key, e.g. "h264"
  DecoderConfig decoder_config_{};

  // Initial album art / metadata captured at discovery time. Forwarded to
  // Dart via the event channel as soon as the event sink is attached.
  std::vector<uint8_t> initial_album_art_;
  std::string initial_album_art_mime_;
  std::string title_;
  std::string artist_;
  std::string album_;
  std::string album_artist_;
  std::string genre_;
  int track_number_{0};
  std::string audio_codec_;
  int audio_channels_{0};
  int audio_sample_rate_{0};

  int64_t m_texture_id{};
  std::atomic<bool> m_valid = true;
  std::unique_ptr<flutter::GpuSurfaceTexture> gpu_surface_texture_;

  // Phase 0.4 — dedicated shared EGL context per player. When available, the
  // GStreamer streaming thread keeps |egl_context_| current for its whole
  // lifetime, eliminating the global texture-context mutex and the
  // TextureMakeCurrent/Clear round-trip per frame. |use_legacy_context_|
  // true means the embedder did not expose an EGL share context (Vulkan
  // backend, headless, older embedder) and we fall back to the old path.
  EGLDisplay egl_display_{EGL_NO_DISPLAY};
  EGLContext egl_context_{EGL_NO_CONTEXT};
  EGLSurface egl_surface_{EGL_NO_SURFACE};
  bool use_legacy_context_{true};
  void CreateSharedGlContext();
  void DestroySharedGlContext();
  void MakeContextCurrent();  // idempotent, cheap when already current

  // Phase 1.5 — render-path selection. At ctor time we probe whether the
  // zero-copy dmabuf → EGLImage path can run on this platform and pick
  // one of two mutually exclusive sinks + renderers. PboShaderUpload is
  // the existing fakesink + NV12 shader path. DmabufZeroCopy replaces the
  // sink with appsink + dmabuf caps and binds EGLImages (implemented in
  // tasks 1.1–1.4). The probe is conservative: if anything looks wrong
  // we fall back to PboShaderUpload.
  enum class RenderPath {
    PboShaderUpload,
    DmabufZeroCopy,
  };
  RenderPath render_path_{RenderPath::PboShaderUpload};
  RenderPath ProbeRenderPath() const;

  // Phase 1.3/1.4 — dmabuf → EGLImage import state with a bounded
  // in-flight deque. Each InFlightFrame holds the EGLImage we bound
  // into the shader's texture name plus the GstSample the image was
  // created from (gst_sample_ref'd). Keeping the sample alive is
  // belt-and-suspenders: eglCreateImageKHR is documented to dup() the
  // FDs internally, but some drivers keep a backing reference to the
  // original GstBuffer memory and unref'ing the sample mid-compositor-
  // sample has been observed to show tearing on hardware-decoded
  // content. Deque is capped at kMaxInFlight — a new import pops the
  // oldest entry to make room, ensuring the frame the compositor most
  // recently saw is still live when the next import arrives.
  struct InFlightFrame {
    EGLImageKHR image{EGL_NO_IMAGE_KHR};
    GstSample* sample{nullptr};
  };
  static constexpr size_t kMaxInFlight = 2;
  std::deque<InFlightFrame> in_flight_frames_;
  std::mutex in_flight_mutex_;
  bool egl_dmabuf_modifiers_ok_{false};  // cached EGL modifier extension

  // Imports `frame` as a new EGLImage, pushes it onto the deque
  // (taking ownership of the passed-in GstSample reference), binds
  // the image to shader_->textureId, and evicts the oldest deque
  // entry when the bound is exceeded. On success returns true and
  // the caller must NOT unref `sample` — the deque owns it now. On
  // failure returns false and `sample` is left untouched for the
  // caller to decide (typically: fall through to CPU upload, unref).
  bool ImportDmabufFrame(const DmabufFrame& frame, GstSample* sample);

  // Destroy every pending EGLImage + unref every retained GstSample.
  // Caller must hold the player's EGL context current.
  void DrainInFlightFrames();

  GMainContext* context_;

  // Gst members
  GstElement* playbin_{};
  GstElement* pipeline_{};
  GstElement* sink_{};
  GstElement* video_convert_{};
  GstElement* video_scale_{};
  GstVideoInfo info_{};
  std::atomic<gint64> position_{0};
  // `rate_` starts at a sentinel (-2.0) so the first ApplyPlaybackSpeed
  // call always sends a real seek to the pipeline — otherwise a fresh
  // playbin can inherit a stray segment rate from a previous instance and
  // play the first second or two too fast before correcting itself.
  // Both fields are touched from the GLib main loop, the GStreamer
  // streaming thread (audio recovery / upgrade idle callbacks) and the
  // Flutter platform thread (Pigeon dispatchers), so they're atomic.
  std::atomic<double> rate_{-2.0};
  std::atomic<double> pending_rate_{1.0};
  GstBus* bus_{};

  // Custom audio sink bin elements (audioconvert → audioresample →
  // capsfilter → real sink). Owned by the bin once added.
  GstElement* audio_bin_{};
  GstElement* audio_convert_{};
  GstElement* audio_resample_{};
  GstElement* audio_scaletempo_{};  // time-stretch for playback rate changes
  GstElement* audio_capsfilter_{};
  GstElement* equalizer_{};     // optional, inserted on first SetEqualizer
  GstElement* videobalance_{};  // optional, inserted on first SetVideoBalance
  int output_channels_{2};

  gulong handoff_handler_id_{};
  gulong on_bus_msg_id_{};
  gulong source_setup_id_{};
  gulong deep_element_added_id_{};

  std::atomic<GstState> target_state_{GST_STATE_PAUSED};

  gint n_video_{};
  gint current_video_{};
  std::unique_ptr<nv12::Shader> shader_;
  std::atomic<bool> is_looping_{};
  std::atomic<bool> is_buffering_{};
  gboolean is_live_{};
  double volume_ = 0.0;

  std::mutex gst_mutex_;
  std::mutex event_mutex_;

  std::atomic<bool> audio_recovery_{false};
  std::atomic<bool> audio_upgraded_{false};
  std::atomic<bool> is_initialized_{false};
  std::atomic<bool> sent_initialized_{false};

  // Latches true the first time the pipeline reaches PLAYING. The
  // GST_MESSAGE_BUFFERING handler uses this to tell a cold-start
  // buffering fill (force PAUSED/resume PLAYING) apart from an expected
  // post-EOS queue2 drain on finite HTTP sources (let playbin handle it
  // internally, just surface bufferingStart/End to Dart).
  std::atomic<bool> ever_played_{false};

  VideoPlayerStats stats_;
  void SetBuffering(bool buffering);

  // udev monitor for audio device hotplug
  struct udev* udev_{};
  struct udev_monitor* udev_mon_{};
  GIOChannel* udev_channel_{};
  guint udev_watch_id_{};
  // GSource id for the pending OnAudioUpgrade idle callback (if any).
  // Tracked so it can be cancelled in StopAudioMonitor / Dispose to
  // prevent the callback from firing after `this` has been destroyed.
  guint audio_upgrade_idle_id_{};
  void StartAudioMonitor();
  void StopAudioMonitor();
  static gboolean OnUdevEvent(GIOChannel* channel,
                              GIOCondition cond,
                              gpointer user_data);

  void ApplyPlaybackSpeed();
  void OnPlaybackEnded();
  static gboolean OnAudioRecovery(gpointer user_data);
  static gboolean OnAudioUpgrade(gpointer user_data);
  static void OnMediaInitialized();
  void OnMediaStateChange(GstState state);
  void OnMediaError(GstMessage* msg);
  void OnMediaDurationChange();
  void SendInitialized();
  void SendMediaMetadata();
  void SendAlbumArt(const std::vector<uint8_t>& bytes, const std::string& mime);
  void SendAudioInfo();

  // Build the audio sink bin (audioconvert → audioresample → capsfilter →
  // real sink). Returns nullptr on failure.
  GstElement* BuildAudioSinkBin();

  // Bus tag handling: extract embedded GST_TAG_IMAGE on the fly.
  void HandleAlbumArt(GstSample* sample);

  static void OnTag(const GstTagList* list,
                    const gchar* tag,
                    gpointer user_data);

  // Connected to playbin's `source-setup` signal so souphttpsrc / rtspsrc
  // properties (timeout, user-agent, proxy, latency, …) can be configured
  // before the source is linked.
  static void OnSourceSetup(GstElement* playbin,
                            GstElement* source,
                            gpointer user_data);

  // The Surface Descriptor sent to Flutter when a texture frame is available.
  FlutterDesktopGpuSurfaceDescriptor m_descriptor{};

  // A mutex is used to synchronize access to the texture descriptor.
  std::mutex buffer_mutex_;

  // The internal Flutter event channel instance.
  std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>>
      event_channel_;

  // The internal Flutter event sink instance, used to send events to the Dart
  // side.
  std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> event_sink_;

  // Phase 0.2 — debug stats channel. Emits one EncodableMap per second while
  // a listener is subscribed; silent otherwise. Guarded by stats_event_mutex_
  // because the sink is attached on the platform thread and read from a GLib
  // timer on the main loop thread.
  std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>>
      stats_event_channel_;
  std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>
      stats_event_sink_;
  std::mutex stats_event_mutex_;
  guint stats_tick_source_id_{};
  static gboolean OnStatsTick(gpointer user_data);
  void EmitStats();

  // deep-element-added on playbin — populates stats_.decoder_name when the
  // uridecodebin auto-plugs a real decoder element (e.g. avdec_h264,
  // v4l2h264dec, vpudec). Safe to attach even when no listener is active.
  static void OnDeepElementAdded(GstBin* bin,
                                 GstBin* sub_bin,
                                 GstElement* element,
                                 gpointer user_data);

  /**
   * @brief Callback called when fakesink receives new frame data
   * @param[in] fakesink No use
   * @param[in] buffer Pointer to New frame data
   * @param[in] pad No use
   * @param[in,out] user_data Pointer to User data
   * @return void
   * @relation
   * flutter
   */
  static void handoff_handler(GstElement* fakesink,
                              GstBuffer* buffer,
                              GstPad* pad,
                              void* user_data);

  // appsink new-sample callback used on the DmabufZeroCopy render path.
  // Pulls the GstSample and dispatches based on memory type (dmabuf →
  // Phase 1.2/1.3 EGLImage import; raw → fall through to the NV12
  // shader upload path so non-dmabuf-capable decoders still work).
  static GstFlowReturn OnNewSample(void* appsink, void* user_data);

  static gboolean OnBusMessage(GstBus* bus, GstMessage* msg, void* user_data);

  /**
   * @brief Prepare
   * @param[in,out] user_data Pointer to User data
   * @return void
   * @relation
   * flutter
   */
  static void prepare(VideoPlayer* user_data);
};
}  // namespace video_player_linux
