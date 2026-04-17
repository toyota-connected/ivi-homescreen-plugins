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
#include <mutex>
#include <string>

namespace video_player_linux {

// Per-player counters surfaced over the video_player_linux/stats/<id> event
// channel. The integer counters are atomic so the GStreamer streaming thread
// can write and the GLib main thread can read without locking; the string
// fields are short and infrequently updated, so they sit behind meta_mutex.
struct VideoPlayerStats {
  std::atomic<uint64_t> frames_received{0};  // from GStreamer
  std::atomic<uint64_t> frames_rendered{0};  // uploaded + drawn
  std::atomic<uint64_t> frames_dropped{0};   // map failed, geometry bad
  std::atomic<uint64_t> frames_late{0};      // arrived > 33ms past PTS wall

  std::atomic<uint64_t> upload_ns_total{0};  // load_pixels wall-time sum
  std::atomic<uint64_t> render_ns_total{0};  // draw_core wall-time sum

  std::atomic<int64_t> last_frame_pts_ns{0};  // GST_BUFFER_PTS of latest frame
  std::atomic<int64_t> last_wallclock_ns{0};  // steady_clock at latest frame

  std::mutex meta_mutex;
  std::string negotiated_format;      // "NV12", "NV12_4L4", …
  std::string negotiated_colorspace;  // "bt709:limited", …
  std::string decoder_name;           // "avdec_h264", "v4l2h264dec", …
  // VideoDecoderBackend::name() of the backend chosen for this stream
  // (or empty when falling back to playbin auto-plug because no
  // backend claimed the codec). Decoupled from decoder_name because
  // a backend may observe an auto-plugged factory without owning its
  // construction.
  std::string selected_backend;

  std::atomic<bool> uses_dmabuf{false};
  std::atomic<bool> uses_hw_decoder{false};
  std::atomic<bool> uses_shared_gl_context{false};
};

}  // namespace video_player_linux
