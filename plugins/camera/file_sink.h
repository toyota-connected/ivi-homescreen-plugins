
#pragma once

#include <map>
#include <memory>
#include <string>

#include <libcamera/stream.h>

#include "frame_sink.h"
#include "image.h"
namespace camera_plugin {
class FileSink final : public FrameSink{
public:
  explicit FileSink(const libcamera::Camera *camera,
           const std::map<const libcamera::Stream *, std::string> &streamNames,
           const std::string &pattern = "");
  ~FileSink() override;
  int configure(const libcamera::CameraConfiguration& config, unsigned int texture_id) override;
  void map_buffer(libcamera::FrameBuffer* buffer) override;
  bool process_request(libcamera::Request* request) override;

private:
  std::map<libcamera::FrameBuffer*, std::unique_ptr<Image>> mMappedBuffers;
  void writeBuffer(const libcamera::Stream *stream,
                   libcamera::FrameBuffer *buffer,
                   const libcamera::ControlList &metadata);

#ifdef HAVE_TIFF
  const libcamera::Camera *camera_;
#endif
  std::map<const libcamera::Stream *, std::string> streamNames_;
  std::string pattern_;

};
}
