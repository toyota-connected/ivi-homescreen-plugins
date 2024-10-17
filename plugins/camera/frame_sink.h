
#ifndef PLUGINS_CAMERA_FRAME_SINK_H_
#define PLUGINS_CAMERA_FRAME_SINK_H_

#include <libcamera/libcamera.h>

#include <plugins/common/common.h>

namespace camera_plugin {
class FrameSink {
 public:
  virtual ~FrameSink() { SPDLOG_DEBUG("FrameSink::~FrameSink"); };

  virtual int configure(const libcamera::CameraConfiguration& /* config */, unsigned int texture_id) {
    SPDLOG_DEBUG("[camera_plugin] FrameSink::configure, texture_id: {}", texture_id);
    return 0;
  };

  virtual void map_buffer(libcamera::FrameBuffer* /* buffer */) {
    SPDLOG_DEBUG("[camera_plugin] FrameSink::mapBuffer");
  }

  virtual int start() {
    SPDLOG_DEBUG("[camera_plugin] FrameSink::start");
    return 0;
  };

  virtual int stop() {
    SPDLOG_DEBUG("[camera_plugin] FrameSink::stop");
    return 0;
  };

  virtual bool process_request(libcamera::Request* request) = 0;
  libcamera::Signal<libcamera::Request*> request_processed;

  virtual int take_picture(std::string filename) {
    SPDLOG_DEBUG("[camera_plugin] FrameSink::stop");
    return 0;
  }
};
}  // namespace camera_plugin

#endif  // PLUGINS_CAMERA_FRAME_SINK_H_