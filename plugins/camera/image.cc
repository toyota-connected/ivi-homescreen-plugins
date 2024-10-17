
#include "image.h"

#include <sys/mman.h>
#include <unistd.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <map>

#include <plugins/common/common.h>

namespace camera_plugin {

std::unique_ptr<Image> Image::fromFrameBuffer(
    const libcamera::FrameBuffer* buffer,
    MapMode mode) {
  std::unique_ptr<Image> image{new Image()};

  assert(!buffer->planes().empty());

  int mmapFlags = 0;

  if (static_cast<int>(mode) & static_cast<int>(MapMode::ReadOnly)) {
    mmapFlags |= PROT_READ;
  }

  if (static_cast<int>(mode) & static_cast<int>(MapMode::WriteOnly)) {
    mmapFlags |= PROT_WRITE;
  }

  struct MappedBufferInfo {
    uint8_t* address = nullptr;
    size_t mapLength = 0;
    size_t dmabufLength = 0;
  };
  std::map<int, MappedBufferInfo> mappedBuffers;

  for (const libcamera::FrameBuffer::Plane& plane : buffer->planes()) {
    const int fd = plane.fd.get();
    if (mappedBuffers.find(fd) == mappedBuffers.end()) {
      const size_t length = lseek(fd, 0, SEEK_END);
      mappedBuffers[fd] = MappedBufferInfo{nullptr, 0, length};
    }

    const size_t length = mappedBuffers[fd].dmabufLength;

    if (plane.offset > length || plane.offset + plane.length > length) {
      spdlog::error("plane is out of buffer: buffer length={}, plane offset={}, plane length={}", length, plane.offset, plane.length);
      return nullptr;
    }
    size_t& mapLength = mappedBuffers[fd].mapLength;
    mapLength =
        std::max(mapLength, static_cast<size_t>(plane.offset + plane.length));
  }

  for (const libcamera::FrameBuffer::Plane& plane : buffer->planes()) {
    const int fd = plane.fd.get();
    auto& info = mappedBuffers[fd];
    if (!info.address) {
      void* address =
          mmap(nullptr, info.mapLength, mmapFlags, MAP_SHARED, fd, 0);
      if (address == MAP_FAILED) {
        int error = -errno;
        spdlog::error("Failed to mmap plane: {}", strerror(-error));
        return nullptr;
      }

      info.address = static_cast<uint8_t*>(address);
      image->mMaps.emplace_back(info.address, info.mapLength);
    }

    image->mPlanes.emplace_back(info.address + plane.offset, plane.length);
  }

  return image;
}

Image::Image() = default;

Image::~Image() {
  for (libcamera::Span<uint8_t>& map : mMaps)
    munmap(map.data(), map.size());
}

unsigned int Image::numPlanes() const {
  return mPlanes.size();
}

libcamera::Span<uint8_t> Image::data(unsigned int plane) {
  assert(plane <= mPlanes.size());
  return mPlanes[plane];
}

libcamera::Span<const uint8_t> Image::data(unsigned int plane) const {
  assert(plane <= mPlanes.size());
  return mPlanes[plane];
}
}  // namespace camera_plugin