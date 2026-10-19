// Image streams of the virtual Kinect (color and depth).
//
// Each stream has frame_limit + 2 frames. A frame is a NUI_IMAGE_FRAME (timestamp in ms, frame number,
// image type, resolution, D3DTexture*, flags, view area) and its D3DTexture header in system memory,
// and its pixels in physical memory, as the title's library lays them out. The header is built by
// the title's own D3D library (TitleSupport::texture_header), so it is right by construction.
//
// Each sensor tick writes the newest image into a frame the title isn't holding: through the
// physical view, then telling the GPU the range changed, as NtReadFile does for physical buffers.
// GetNextFrame hands out the newest frame (held until ReleaseFrame).
#include "image_stream.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>

#include "x360/kernel.h"
#include "xenia/memory.h"

using namespace x360::kernel;

namespace x360::nui {

namespace {

constexpr uint32_t kImageFrameSize = 0x28;  // NUI_IMAGE_FRAME
constexpr uint32_t kHeaderOffset = 0x40;    // D3DTexture header after it, 16-byte aligned
constexpr uint32_t kRecordSize = 0x80;

enum class FrameState { kFree, kReady, kHeld };

struct Frame {
  uint32_t record = 0;  // NUI_IMAGE_FRAME, then the texture header
  uint32_t pixels = 0;  // physical memory
  FrameState state = FrameState::kFree;
  uint32_t number = 0;
};

struct Stream : Object {
  const char* TypeName() const override { return "NuiImageStream"; }
  uint32_t type = 0, resolution = 0, frame_limit = 0, event = 0;
  StreamFormat format;
  uint32_t pitch = 0, size = 0;
  std::vector<Frame> frames;
  uint32_t delivered = 0;  // number of the frame last handed out
};

std::mutex g_mutex;
TitleSupport g_title;
std::vector<std::shared_ptr<Stream>> g_streams;

uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

// ---- drawing bodies -----------------------------------------------------------------------------

struct Bone {
  Joint a, b;
  float radius;  // metres
};
const Bone kBones[] = {
    {kHead, kShoulderCenter, 0.11f},    {kShoulderCenter, kSpine, 0.16f},    {kSpine, kHipCenter, 0.15f},
    {kShoulderLeft, kShoulderRight, 0.08f}, {kHipLeft, kHipRight, 0.10f},     {kShoulderLeft, kElbowLeft, 0.06f},
    {kElbowLeft, kWristLeft, 0.05f},    {kWristLeft, kHandLeft, 0.05f},     {kShoulderRight, kElbowRight, 0.06f},
    {kElbowRight, kWristRight, 0.05f},  {kWristRight, kHandRight, 0.05f},   {kHipLeft, kKneeLeft, 0.08f},
    {kKneeLeft, kAnkleLeft, 0.06f},     {kAnkleLeft, kFootLeft, 0.05f},     {kHipRight, kKneeRight, 0.08f},
    {kKneeRight, kAnkleRight, 0.06f},   {kAnkleRight, kFootRight, 0.05f},
};

// Rasterizes the capsule a-b (camera space) into a w x h image with focal length f: `plot(x, y, z)`
// for each covered pixel, z in metres.
template <typename Plot>
void Capsule(const float* a, const float* b, float radius, uint32_t w, uint32_t h, float f, Plot plot) {
  if (a[2] <= 0.1f || b[2] <= 0.1f) return;
  const float ax = w / 2.0f + a[0] * f / a[2], ay = h / 2.0f - a[1] * f / a[2];
  const float bx = w / 2.0f + b[0] * f / b[2], by = h / 2.0f - b[1] * f / b[2];
  const float r = radius * f / std::min(a[2], b[2]);
  const int x0 = std::max(0, int(std::floor(std::min(ax, bx) - r))), x1 = std::min(int(w) - 1, int(std::ceil(std::max(ax, bx) + r)));
  const int y0 = std::max(0, int(std::floor(std::min(ay, by) - r))), y1 = std::min(int(h) - 1, int(std::ceil(std::max(ay, by) + r)));
  const float dx = bx - ax, dy = by - ay, length2 = std::max(dx * dx + dy * dy, 1e-6f);
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const float t = std::clamp(((x - ax) * dx + (y - ay) * dy) / length2, 0.0f, 1.0f);
      const float px = ax + t * dx - x, py = ay + t * dy - y;
      if (px * px + py * py <= r * r) plot(uint32_t(x), uint32_t(y), a[2] + t * (b[2] - a[2]));
    }
  }
}

// ---- pixel formats --------------------------------------------------------------------------

uint8_t Clamp8(float v) { return uint8_t(std::clamp(v, 0.0f, 255.0f)); }

// BGRA (host) to the stream's color format, row by row into `out` (guest layout, big-endian).
void ConvertColor(const StreamFormat& f, uint32_t pitch, const std::vector<uint8_t>& bgra, uint8_t* out) {
  for (uint32_t y = 0; y < f.height; ++y) {
    const uint8_t* src = bgra.data() + size_t(y * kColorHeight / f.height) * kColorWidth * 4;
    uint8_t* row = out + size_t(y) * pitch;
    auto pixel = [&](uint32_t x) { return src + size_t(x * kColorWidth / f.width) * 4; };
    if (f.kind == StreamFormat::kColorRgb) {
      for (uint32_t x = 0; x < f.width; ++x) {
        const uint8_t* p = pixel(x);
        row[x * 4 + 0] = 0xFF;  // X8R8G8B8, big-endian
        row[x * 4 + 1] = p[2];
        row[x * 4 + 2] = p[1];
        row[x * 4 + 3] = p[0];
      }
    } else {  // UYVY: U Y0 V Y1 for each pair of pixels (BT.601)
      for (uint32_t x = 0; x + 1 < f.width; x += 2) {
        const uint8_t *p0 = pixel(x), *p1 = pixel(x + 1);
        auto luma = [](const uint8_t* p) { return 16 + 0.257f * p[2] + 0.504f * p[1] + 0.098f * p[0]; };
        const float r = (p0[2] + p1[2]) / 2.0f, g = (p0[1] + p1[1]) / 2.0f, b = (p0[0] + p1[0]) / 2.0f;
        row[x * 2 + 0] = Clamp8(128 - 0.148f * r - 0.291f * g + 0.439f * b);
        row[x * 2 + 1] = Clamp8(luma(p0));
        row[x * 2 + 2] = Clamp8(128 + 0.439f * r - 0.368f * g - 0.071f * b);
        row[x * 2 + 3] = Clamp8(luma(p1));
      }
    }
  }
}

void ConvertDepth(const StreamFormat& f, uint32_t pitch, const std::vector<uint16_t>& depth, uint8_t* out) {
  for (uint32_t y = 0; y < f.height; ++y) {
    const uint16_t* src = depth.data() + size_t(y * kDepthHeight / f.height) * kDepthWidth;
    uint8_t* row = out + size_t(y) * pitch;
    for (uint32_t x = 0; x < f.width; ++x) {
      uint16_t v = src[x * kDepthWidth / f.width];
      row[x * 2 + 0] = uint8_t(v >> 8);  // big-endian
      row[x * 2 + 1] = uint8_t(v);
    }
  }
}

// The body indices (1 + index in snapshot.bodies) of a source's depth image, as frame slots + 1.
void RemapPlayers(std::vector<uint16_t>& depth, const uint32_t slots[kSkeletonCount]) {
  uint16_t map[8] = {0};
  for (uint32_t i = 0; i < kSkeletonCount; ++i) map[i + 1] = uint16_t(slots[i] < kSkeletonCount ? slots[i] + 1 : 0);
  for (uint16_t& v : depth) v = uint16_t((v & ~7u) | map[v & 7]);
}

void WriteFrame(Stream& s, Frame& frame, uint32_t number, uint64_t time_ms, const std::vector<uint8_t>* color,
                const std::vector<uint16_t>* depth) {
  xe::BaseHeap* heap = XeMemory()->LookupHeap(frame.pixels);
  auto* physical = heap && heap->heap_type() == xe::HeapType::kGuestPhysical ? static_cast<xe::PhysicalHeap*>(heap)
                                                                              : nullptr;
  auto* out = static_cast<uint8_t*>(physical ? XeMemory()->TranslatePhysical(physical->GetPhysicalAddress(frame.pixels))
                                             : static_cast<void*>(Ptr(frame.pixels)));
  if (s.format.kind == StreamFormat::kColorYuv || s.format.kind == StreamFormat::kColorRgb) {
    if (color) ConvertColor(s.format, s.pitch, *color, out);
  } else if (depth) {
    ConvertDepth(s.format, s.pitch, *depth, out);
  }
  if (physical) physical->TriggerCallbacks(xe::global_critical_region::AcquireDirect(), frame.pixels, s.size, true, true);
  Store<uint64_t>(frame.record + 0x00, time_ms);
  Store<uint32_t>(frame.record + 0x08, number);
  frame.number = number;
}

}  // namespace

void DrawDepth(const Snapshot& snapshot, const uint32_t slots[kSkeletonCount], std::vector<uint16_t>& depth) {
  // A room: the wall 4 m away; the floor nearer towards the bottom of the image.
  depth.assign(size_t(kDepthWidth) * kDepthHeight, uint16_t(4000 << 3));
  for (uint32_t y = kDepthHeight / 2; y < kDepthHeight; ++y) {
    const float down = (y - kDepthHeight / 2.0f) / kDepthFocal;  // tan of the angle below the axis
    const uint32_t mm = std::min<uint32_t>(4000, uint32_t(800.0f / std::max(down, 0.2f)));
    std::fill_n(depth.begin() + size_t(y) * kDepthWidth, kDepthWidth, uint16_t(mm << 3));
  }
  for (uint32_t i = 0; i < snapshot.body_count; ++i) {
    const Body& body = snapshot.bodies[i];
    const uint16_t player = uint16_t(slots[i] < kSkeletonCount ? slots[i] + 1 : 0);
    for (const Bone& bone : kBones) {
      Capsule(body.joints[bone.a], body.joints[bone.b], bone.radius, kDepthWidth, kDepthHeight, kDepthFocal,
              [&](uint32_t x, uint32_t y, float z) {
                uint16_t& v = depth[size_t(y) * kDepthWidth + x];
                const uint32_t mm = uint32_t(z * 1000);
                if (mm < uint32_t(v >> 3) || (v & 7) == 0) v = uint16_t((std::min<uint32_t>(mm, 8000) << 3) | player);
              });
    }
  }
}

void DrawColor(const Snapshot& snapshot, std::vector<uint8_t>& bgra) {
  bgra.assign(size_t(kColorWidth) * kColorHeight * 4, 0);
  for (uint32_t y = 0; y < kColorHeight; ++y) {  // a dim room, lighter towards the floor
    const uint8_t shade = uint8_t(40 + 40 * y / kColorHeight);
    for (uint32_t x = 0; x < kColorWidth; ++x) {
      uint8_t* p = &bgra[(size_t(y) * kColorWidth + x) * 4];
      p[0] = p[1] = p[2] = shade;
      p[3] = 0xFF;
    }
  }
  static const uint8_t kColors[][3] = {{60, 140, 230}, {230, 120, 60}, {90, 200, 90}};  // BGR per player
  for (uint32_t i = 0; i < snapshot.body_count; ++i) {
    const uint8_t* c = kColors[i % 3];
    for (const Bone& bone : kBones) {
      Capsule(snapshot.bodies[i].joints[bone.a], snapshot.bodies[i].joints[bone.b], bone.radius, kColorWidth,
              kColorHeight, kColorFocal, [&](uint32_t x, uint32_t y, float) {
                uint8_t* p = &bgra[(size_t(y) * kColorWidth + x) * 4];
                p[0] = c[0], p[1] = c[1], p[2] = c[2];
              });
    }
  }
}

void SetImageTitleSupport(const TitleSupport& support) {
  std::lock_guard lock(g_mutex);
  g_title = support;
}

uint32_t OpenStreamMask() {
  std::lock_guard lock(g_mutex);
  uint32_t mask = 0;
  for (const auto& s : g_streams)
    mask |= s->format.kind == StreamFormat::kColorYuv || s->format.kind == StreamFormat::kColorRgb ? 1u : 2u;
  return mask ? mask : 4u;
}

std::vector<uint32_t> PublishImageFrames(const Snapshot& snapshot, const uint32_t slots[kSkeletonCount],
                                         uint32_t frame_number, uint64_t time_ms) {
  std::lock_guard lock(g_mutex);
  if (g_streams.empty()) return {};
  // The camera's images of the last frame again: the title has them already.
  static const void *last_color = nullptr, *last_depth = nullptr;
  if ((snapshot.color || snapshot.depth) && snapshot.color.get() == last_color && snapshot.depth.get() == last_depth)
    return {};
  last_color = snapshot.color.get();
  last_depth = snapshot.depth.get();
  // The images: the source's, or drawn from the bodies (only those some stream needs).
  bool want_color = false, want_depth = false;
  for (const auto& s : g_streams) {
    if (s->format.kind == StreamFormat::kColorYuv || s->format.kind == StreamFormat::kColorRgb)
      want_color = true;
    else
      want_depth = true;
  }
  std::vector<uint8_t> drawn_color;
  std::vector<uint16_t> depth;
  const std::vector<uint8_t>* color = snapshot.color.get();
  if (want_color && !color) {
    DrawColor(snapshot, drawn_color);
    color = &drawn_color;
  }
  if (want_depth) {
    if (snapshot.depth) {
      depth = *snapshot.depth;
      RemapPlayers(depth, slots);
    } else {
      DrawDepth(snapshot, slots, depth);
    }
  }

  std::vector<uint32_t> events;
  for (const auto& s : g_streams) {
    // The frame to write: free, and not the one the title may be about to take.
    auto it = std::find_if(s->frames.begin(), s->frames.end(), [](const Frame& f) { return f.state == FrameState::kFree; });
    if (it == s->frames.end()) continue;  // the title holds them all: drop this image
    WriteFrame(*s, *it, frame_number, time_ms, color, want_depth ? &depth : nullptr);
    for (Frame& f : s->frames)
      if (f.state == FrameState::kReady) f.state = FrameState::kFree;
    it->state = FrameState::kReady;
    if (s->event) events.push_back(s->event);
  }
  return events;
}

void CloseImageStreams() {
  std::lock_guard lock(g_mutex);
  g_streams.clear();  // their memory stays allocated: the title may still hold frames
}

uint32_t ImageStreamOpen(PPCContext* ctx, uint32_t type, uint32_t resolution, uint32_t flags, uint32_t frame_limit,
                         uint32_t event, uint32_t stream_ptr) {
  (void)flags;
  if (!stream_ptr || !frame_limit || frame_limit > 48) return kInvalidArg;
  std::lock_guard lock(g_mutex);
  auto stream = std::make_shared<Stream>();
  if (!g_title.stream_format || !g_title.stream_format(type, resolution, stream->format)) return kInvalidArg;
  const StreamFormat& f = stream->format;
  stream->type = type;
  stream->resolution = resolution;
  stream->frame_limit = frame_limit;
  stream->event = event;
  // Linear texture rows are 256-byte aligned; the library allocates whole 32-row tiles.
  stream->pitch = AlignUp(f.width * f.bytes_per_pixel, 256);
  stream->size = stream->pitch * AlignUp(f.height, 32);
  for (uint32_t i = 0; i < frame_limit + 2; ++i) {
    Frame frame;
    frame.record = SystemAlloc(kRecordSize, 64);
    frame.pixels = PhysicalAlloc(stream->size, 0x1000, 0x1000, 0, 0, 0);
    if (!frame.record || !frame.pixels) return kFail;
    std::memset(Ptr(frame.record), 0, kRecordSize);
    std::memset(Ptr(frame.pixels), 0, stream->size);
    Store<uint32_t>(frame.record + 0x0C, type);
    Store<uint32_t>(frame.record + 0x10, resolution);
    Store<uint32_t>(frame.record + 0x14, frame.record + kHeaderOffset);
    if (g_title.texture_header && ctx) g_title.texture_header(*ctx, frame.record + kHeaderOffset, f, frame.pixels);
    stream->frames.push_back(frame);
  }
  g_streams.push_back(stream);
  Store<uint32_t>(stream_ptr, CreateHandle(stream));
  KLOG_I("nui: image stream type %u, %ux%u (format %08X, pitch %u), %u frames", type, f.width, f.height,
         f.d3d_format, stream->pitch, frame_limit);
  return kOk;
}

uint32_t ImageStreamGetNextFrame(uint32_t stream_handle, uint32_t timeout_ms, uint32_t frame_ptr) {
  (void)timeout_ms;  // the title polls (0 ms); a frame comes every 33 ms
  if (!frame_ptr) return kInvalidArg;
  auto stream = LookupHandleAs<Stream>(stream_handle);
  if (!stream) return kInvalidArg;
  std::lock_guard lock(g_mutex);
  uint32_t held = 0;
  for (const Frame& f : stream->frames) held += f.state == FrameState::kHeld;
  for (Frame& f : stream->frames) {
    if (f.state != FrameState::kReady || f.number == stream->delivered) continue;
    if (held >= stream->frame_limit) return 0x83010004;  // E_NUI_FRAME_LIMIT_EXCEEDED
    f.state = FrameState::kHeld;
    stream->delivered = f.number;
    Store<uint32_t>(frame_ptr, f.record);
    return kOk;
  }
  return kFrameNoData;
}

uint32_t ImageStreamReleaseFrame(uint32_t stream_handle, uint32_t frame) {
  auto stream = LookupHandleAs<Stream>(stream_handle);
  if (!stream) return kInvalidArg;
  std::lock_guard lock(g_mutex);
  for (Frame& f : stream->frames)
    if (f.record == frame && f.state == FrameState::kHeld) f.state = FrameState::kFree;
  return kOk;
}

}  // namespace x360::nui
