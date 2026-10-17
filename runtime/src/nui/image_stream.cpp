// Image streams of the virtual Kinect (color and depth).
#include "image_stream.h"

#include <memory>
#include <mutex>

#include "x360/kernel.h"

using namespace x360::kernel;

namespace x360::nui {

namespace {

struct Stream : Object {
  const char* TypeName() const override { return "NuiImageStream"; }
  uint32_t type = 0, resolution = 0, frame_limit = 0, event = 0;
  StreamFormat format;
};

std::mutex g_mutex;
TitleSupport g_title;
std::vector<std::shared_ptr<Stream>> g_streams;

}  // namespace

void SetImageTitleSupport(const TitleSupport& support) {
  std::lock_guard lock(g_mutex);
  g_title = support;
}

std::vector<uint32_t> PublishImageFrames(const Snapshot& snapshot, uint32_t frame_number, uint64_t time_ms) {
  (void)snapshot, (void)frame_number, (void)time_ms;
  return {};
}

void CloseImageStreams() {
  std::lock_guard lock(g_mutex);
  g_streams.clear();
}

uint32_t ImageStreamOpen(PPCContext* ctx, uint32_t type, uint32_t resolution, uint32_t flags, uint32_t frame_limit,
                         uint32_t event, uint32_t stream_ptr) {
  (void)ctx, (void)flags;
  if (!stream_ptr || !frame_limit) return kInvalidArg;
  std::lock_guard lock(g_mutex);
  auto stream = std::make_shared<Stream>();
  if (!g_title.stream_format || !g_title.stream_format(type, resolution, stream->format)) return kInvalidArg;
  stream->type = type;
  stream->resolution = resolution;
  stream->frame_limit = frame_limit;
  stream->event = event;
  g_streams.push_back(stream);
  Store<uint32_t>(stream_ptr, CreateHandle(stream));
  KLOG_I("nui: image stream type %u, %ux%u, %u frames", type, stream->format.width, stream->format.height,
         frame_limit);
  return kOk;
}

uint32_t ImageStreamGetNextFrame(uint32_t stream, uint32_t timeout_ms, uint32_t frame_ptr) {
  (void)stream, (void)timeout_ms;
  if (frame_ptr) Store<uint32_t>(frame_ptr, 0);
  return kFrameNoData;
}

uint32_t ImageStreamReleaseFrame(uint32_t stream, uint32_t frame) {
  (void)stream, (void)frame;
  return kOk;
}

}  // namespace x360::nui
