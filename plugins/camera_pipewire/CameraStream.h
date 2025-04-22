//
// Created by tcna on 3/24/25.
//

#ifndef CAMERASTREAM_H
#define CAMERASTREAM_H

#include <GLES2/gl2.h>

#include <flutter/plugin_registrar_homescreen.h>
#include <flutter/texture_registrar.h>
#include <pipewire/pipewire.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

/**
 * CameraStream manages a single PipeWire MJPEG camera stream and its Flutter
 * texture.
 */
class CameraStream {
 public:
  /**
   * Create a new CameraStream.
   * @param plugin_registrar  A Flutter TextureRegistrar used to create and update a
   * Flutter texture.
   * @param camera_name The name of the camera
   * @param width      Desired width of the MJPEG frames.
   * @param height     Desired height of the MJPEG frames.
   */
  CameraStream(flutter::PluginRegistrarDesktop* plugin_registrar,
               std::string camera_name,
               int width,
               int height);

  /**
   * Destructor. Automatically stops the camera stream if running.
   */
  ~CameraStream();

  /**
   * Start capturing from the given PipeWire node ID (camera).
   * @param nodeID  The PipeWire node ID to capture from (e.g. "42").
   * @return true if successful, false otherwise.
   */
  bool Start(const std::string& nodeID);

  /**
   * Stop capturing if the stream is running.
   */
  void Stop();

  void PauseStream() const;
  void ResumeStream() const;
  /**
   * Get the Flutter texture ID associated with this stream.
   * Use this ID in Flutter's Texture() widget to display the camera feed.
   */
  [[nodiscard]] GLuint texture_id() const { return texture_id_; }

  [[nodiscard]] std::string camera_name() const { return camera_name_; }
  [[nodiscard]] int camera_width() const { return width_; }
  [[nodiscard]] int camera_height() const { return height_; }
  static std::optional<std::string> GetFilePathForPicture();
  [[nodiscard]] std::string takePicture() const;

 private:
  // PipeWire objects
  flutter::PluginRegistrarDesktop* registrar_{};

  pw_stream* pw_stream_ = nullptr;

  // The listener hook must stay in scope; never store it on the stack.
  spa_hook stream_listener_{};

  GLuint texture_id_{};
  GLuint framebuffer_{};

  std::unique_ptr<flutter::GpuSurfaceTexture> gpu_surface_texture;
  FlutterDesktopGpuSurfaceDescriptor descriptor{};

  // Decoded buffer + sync
  std::unique_ptr<uint8_t[]> decoded_buffer_;
  std::mutex frame_mutex_;
  std::atomic<bool> new_frame_available_{false};

  // Dimensions
  int width_ = 640;
  int height_ = 480;

  // Private methods
  void HandleProcess();

  // Camera name
  std::string camera_name_ = "";
  // PipeWire callbacks (static => dispatch to instance)
  static void OnStreamStateChanged(void* data,
                                   pw_stream_state old_state,
                                   pw_stream_state new_state,
                                   const char* error);
  static void OnStreamParamChanged(void* data,
                                   uint32_t id,
                                   const spa_pod* param);
  static void OnStreamProcess(void* data);
};

#endif  // CAMERASTREAM_H
