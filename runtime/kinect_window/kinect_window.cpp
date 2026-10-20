#include "kinect_window.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "d2d/canvas.h"
#include "x360/fonts.h"
#include "x360/nui.h"

namespace x360::kinect_window {

namespace {

using anyx360::ui::Align;
using anyx360::ui::Canvas;
using anyx360::ui::Color;
using anyx360::ui::ComPtr;
using anyx360::ui::RectF;
using anyx360::ui::TextStyle;

// The in-game panels' theme (runtime/src/gpu/ui_theme.h).
constexpr Color kBackground = Color::Hex(0x121417);
constexpr Color kPanel = Color::Hex(0x1A1D21);
constexpr Color kPanelHigh = Color::Hex(0x22262B);
constexpr Color kBorder = Color::Hex(0x2A2E33);
constexpr Color kText = Color::Hex(0xE6E8EB);
constexpr Color kTextSecondary = Color::Hex(0x8B9096);
constexpr Color kGreen = Color::Hex(0x3CB043);
constexpr Color kAmber = Color::Hex(0xF0B429);
constexpr Color kRed = Color::Hex(0xF2545B);
constexpr Color kImageBackground = Color::Hex(0x0E1012);
// A color per frame slot.
constexpr Color kBodyColors[] = {Color::Hex(0x3CB043), Color::Hex(0x3B8EDB), Color::Hex(0xF0B429),
                                 Color::Hex(0xE05A9A), Color::Hex(0x8E6CEF), Color::Hex(0x2EC4B6)};

constexpr float kWidth = 1280, kHeight = 820, kMinWidth = 960, kMinHeight = 640;  // DIPs
constexpr float kMargin = 16, kGap = 12;
constexpr float kHorizontalFov = 57.0f, kVerticalFov = 43.0f;  // Kinect's depth camera, degrees

const char* const kJointNames[nui::kJointCount] = {
    "Hip centre", "Spine",      "Shoulder centre", "Head",      "Shoulder left", "Elbow left", "Wrist left",
    "Hand left",  "Shoulder right", "Elbow right", "Wrist right", "Hand right", "Hip left",   "Knee left",
    "Ankle left", "Foot left",  "Hip right",       "Knee right", "Ankle right",  "Foot right"};

TextStyle Style(float size, bool semibold, Color color, Align align = Align::kStart) {
  TextStyle style;
  style.family = L"Inter";
  style.size = size;
  style.weight = semibold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
  style.color = color;
  style.align = align;
  return style;
}

std::wstring Wide(const char* text) { return std::wstring(text, text + std::strlen(text)); }

std::wstring Printf(const wchar_t* format, ...) {
  wchar_t text[256];
  va_list args;
  va_start(args, format);
  _vsnwprintf_s(text, _TRUNCATE, format, args);
  va_end(args);
  return text;
}

// What the window draws, kept between frames (the images' bitmaps).
class Renderer {
 public:
  void Draw(Canvas& c);

 private:
  RectF Panel(Canvas& c, const RectF& r, const wchar_t* title);
  void DrawHeader(Canvas& c, const nui::DebugView& v, float rate);
  void DrawCamera(Canvas& c, const RectF& r, const nui::DebugView& v);
  void DrawDepth(Canvas& c, const RectF& r, const nui::DebugView& v);
  void DrawTop(Canvas& c, const RectF& r, const nui::DebugView& v);
  void DrawSide(Canvas& c, const RectF& r, const nui::DebugView& v);
  void DrawJoints(Canvas& c, const RectF& r, const nui::DebugView& v);
  void DrawSensor(Canvas& c, const RectF& r, const nui::DebugView& v);
  void DrawTimings(Canvas& c, const RectF& r, const std::vector<nui::TimingSample>& samples);
  ID2D1Bitmap* ColorBitmap(Canvas& c, const nui::DebugView& v);
  ID2D1Bitmap* DepthBitmap(Canvas& c, const nui::DebugView& v);

  uint64_t generation_ = 0;
  ComPtr<ID2D1Bitmap> color_, depth_;
  const void* color_source_ = nullptr;
  const void* depth_source_ = nullptr;
  std::vector<uint8_t> depth_pixels_;
};

RectF Renderer::Panel(Canvas& c, const RectF& r, const wchar_t* title) {
  c.FillRoundedRect(r, 6, kPanel);
  c.StrokeRoundedRect(r, 6, kBorder);
  c.Text(title, {r.x + 12, r.y + 9, r.w - 24, 18}, Style(12, true, kTextSecondary));
  return {r.x + 12, r.y + 32, r.w - 24, r.h - 44};
}

// Which body the numbers follow: the title's choice, else the first tracked, else the first.
const nui::DebugBody* MainBody(const nui::DebugView& v) {
  for (const nui::DebugBody& b : v.bodies)
    if (b.chosen) return &b;
  for (const nui::DebugBody& b : v.bodies)
    if (b.tracked) return &b;
  return v.bodies.empty() ? nullptr : &v.bodies[0];
}

void Renderer::Draw(Canvas& c) {
  if (c.generation() != generation_) {
    generation_ = c.generation();
    color_.Reset(), depth_.Reset();
    color_source_ = depth_source_ = nullptr;
  }
  const nui::DebugView v = nui::GetDebugView(true);
  const std::vector<nui::TimingSample> samples = nui::GetTimingHistory();
  // Sensor frames in the last second.
  float rate = 0;
  if (!samples.empty())
    for (const nui::TimingSample& s : samples) rate += s.time > samples.back().time - 1.0f;

  const float w = c.width(), h = c.height();
  c.Clear(kBackground);
  DrawHeader(c, v, rate);

  // Top: the camera, the main player's joints, the sensor. Below: depth, from above, from the side,
  // the timings.
  const float top = 64, bottom = h - kMargin, inner = w - 2 * kMargin;
  const float lower_h = std::clamp((bottom - top) * 0.36f, 200.0f, 330.0f);
  const float upper_h = bottom - top - kGap - lower_h, lower_y = bottom - lower_h;
  const float camera_w = std::round(std::min(inner * 0.5f, (upper_h - 44) * 4 / 3 + 24));
  const float joints_w = std::round((inner - camera_w - 2 * kGap) * 0.48f);
  const float sensor_w = inner - camera_w - joints_w - 2 * kGap;
  float x = kMargin;
  DrawCamera(c, {x, top, camera_w, upper_h}, v);
  x += camera_w + kGap;
  DrawJoints(c, {x, top, joints_w, upper_h}, v);
  x += joints_w + kGap;
  DrawSensor(c, {x, top, sensor_w, upper_h}, v);
  const float depth_w = std::round(std::min(inner * 0.24f, (lower_h - 44) * 4 / 3 + 24));
  const float rest = inner - depth_w - 3 * kGap;
  const float above_w = std::round(rest * 0.3f), side_w = std::round(rest * 0.3f), timings_w = rest - above_w - side_w;
  x = kMargin;
  DrawDepth(c, {x, lower_y, depth_w, lower_h}, v);
  x += depth_w + kGap;
  DrawTop(c, {x, lower_y, above_w, lower_h}, v);
  x += above_w + kGap;
  DrawSide(c, {x, lower_y, side_w, lower_h}, v);
  x += side_w + kGap;
  DrawTimings(c, {x, lower_y, timings_w, lower_h}, samples);
}

void Renderer::DrawHeader(Canvas& c, const nui::DebugView& v, float rate) {
  c.Text(L"Kinect", {kMargin + 2, 14, 200, 26}, Style(20, true, kText));
  uint32_t tracked = 0;
  for (const nui::DebugBody& b : v.bodies) tracked += b.tracked;
  std::wstring line = Printf(L"%hs  ·  %.0f frames a second  ·  %u %s in view, %u tracked  ·  frame %u",
                             v.status.source.empty() ? "no source" : v.status.source.c_str(), rate, uint32_t(v.bodies.size()),
                             v.bodies.size() == 1 ? L"person" : L"people", tracked, v.frame);
  c.Text(line, {kMargin + 92, 19, c.width() - 2 * kMargin - 260, 20}, Style(13, false, kTextSecondary));
  // The state: tracking someone, waiting for a player, or the source's problem.
  const bool problem = !v.status.initialized || (v.status.message != "ok" && v.status.message != "no player" &&
                                                 !v.status.message.empty() && !v.status.camera_running &&
                                                 v.status.source == "webcam");
  const std::wstring state = !v.status.initialized ? L"not started"
                             : problem             ? Wide(v.status.message.c_str())
                             : tracked             ? L"tracking"
                                                   : L"waiting for a player";
  const Color color = problem ? kRed : tracked ? kGreen : kAmber;
  const TextStyle pill = Style(12, true, color, Align::kCenter);
  const float pill_w = std::min(c.MeasureText(state, pill).width + 24, 360.0f);
  const RectF r{c.width() - kMargin - pill_w, 16, pill_w, 24};
  c.FillRoundedRect(r, 12, color.Alpha(0.14f));
  TextStyle centred = pill;
  centred.vertical = Align::kCenter;
  c.Text(state, r, centred);
}

ID2D1Bitmap* Renderer::ColorBitmap(Canvas& c, const nui::DebugView& v) {
  if (!v.color || v.color->size() < size_t(nui::kColorWidth) * nui::kColorHeight * 4) return nullptr;
  if (!color_) color_ = c.CreateBitmap(nui::kColorWidth, nui::kColorHeight, *v.color);
  else if (color_source_ != v.color.get())
    color_->CopyFromMemory(nullptr, v.color->data(), nui::kColorWidth * 4);
  color_source_ = v.color.get();
  return color_.Get();
}

ID2D1Bitmap* Renderer::DepthBitmap(Canvas& c, const nui::DebugView& v) {
  if (!v.depth || v.depth->size() < size_t(nui::kDepthWidth) * nui::kDepthHeight) return nullptr;
  if (depth_ && depth_source_ == v.depth.get()) return depth_.Get();
  // Near bright, far dark (0.5-4.5 m); each player in their color.
  depth_pixels_.resize(size_t(nui::kDepthWidth) * nui::kDepthHeight * 4);
  for (size_t i = 0; i < size_t(nui::kDepthWidth) * nui::kDepthHeight; ++i) {
    const uint16_t value = (*v.depth)[i];
    const uint32_t mm = value >> 3, player = value & 7;
    uint8_t* p = &depth_pixels_[i * 4];
    if (!mm) {
      p[0] = p[1] = p[2] = 0x10, p[3] = 0xFF;
      continue;
    }
    const float closeness = 1.0f - std::clamp((float(mm) - 500.0f) / 4000.0f, 0.0f, 1.0f);
    float r = 40 + 170 * closeness, g = r, b = r;
    if (player) {
      const Color tint = kBodyColors[(player - 1) % 6];
      r = r * 0.35f + tint.r * 255 * 0.65f * (0.5f + 0.5f * closeness);
      g = g * 0.35f + tint.g * 255 * 0.65f * (0.5f + 0.5f * closeness);
      b = b * 0.35f + tint.b * 255 * 0.65f * (0.5f + 0.5f * closeness);
    }
    p[0] = uint8_t(std::min(b, 255.0f)), p[1] = uint8_t(std::min(g, 255.0f)), p[2] = uint8_t(std::min(r, 255.0f));
    p[3] = 0xFF;
  }
  if (!depth_) depth_ = c.CreateBitmap(nui::kDepthWidth, nui::kDepthHeight, depth_pixels_);
  else depth_->CopyFromMemory(nullptr, depth_pixels_.data(), nui::kDepthWidth * 4);
  depth_source_ = v.depth.get();
  return depth_.Get();
}

// `r` fitted to `aspect` (width / height), centred.
RectF Fit(const RectF& r, float aspect) {
  float w = r.w, h = r.w / aspect;
  if (h > r.h) h = r.h, w = r.h * aspect;
  return {std::round(r.x + (r.w - w) / 2), std::round(r.y + (r.h - h) / 2), std::round(w), std::round(h)};
}

void Renderer::DrawCamera(Canvas& c, const RectF& panel, const nui::DebugView& v) {
  const RectF area = Panel(c, panel, v.camera_images ? L"CAMERA" : L"CAMERA (drawn from the bodies: the source has no camera)");
  const RectF img = Fit(area, 4.0f / 3.0f);
  c.FillRect(img, kImageBackground);
  if (ID2D1Bitmap* bitmap = ColorBitmap(c, v)) c.DrawBitmap(bitmap, img, v.camera_images ? 1.0f : 0.5f);
  // Where the virtual color camera sees each body: (w/2 + x f/z, h/2 - y f/z), scaled to the view.
  auto project = [&](const float* j) {
    const float z = std::max(j[2], 0.2f);
    return D2D1_POINT_2F{img.x + (0.5f + j[0] * nui::kColorFocal / z / nui::kColorWidth) * img.w,
                         img.y + (0.5f - j[1] * nui::kColorFocal / z / nui::kColorHeight) * img.h};
  };
  c.PushClip(img);
  for (const nui::DebugBody& b : v.bodies) {
    const Color color = kBodyColors[b.slot % 6].Alpha(b.tracked ? 1.0f : 0.55f);
    for (const auto& bone : nui::kSkeletonBones) {
      const D2D1_POINT_2F a = project(b.joints[bone[0]]), e = project(b.joints[bone[1]]);
      c.Line(a.x, a.y, e.x, e.y, color, b.tracked ? 3.0f : 1.5f);
    }
    for (uint32_t j = 0; j < nui::kJointCount; ++j) {
      const D2D1_POINT_2F p = project(b.joints[j]);
      const Color state = b.joint_state[j] == nui::kJointTracked    ? kText
                          : b.joint_state[j] == nui::kJointInferred ? kAmber
                                                                    : kRed;
      c.FillCircle(p.x, p.y, j == nui::kHead ? 6.0f : 3.5f, state);
    }
    const D2D1_POINT_2F head = project(b.joints[nui::kHead]);
    const std::wstring label = Printf(L"#%u%s%s", b.id, b.tracked ? L"" : L"  position only", b.chosen ? L"  chosen" : L"");
    const TextStyle style = Style(12, true, kText);
    const float label_w = c.MeasureText(label, style).width + 14;
    const RectF tag{head.x - label_w / 2, head.y - 36, label_w, 20};
    c.FillRoundedRect(tag, 10, kBodyColors[b.slot % 6].Alpha(0.85f));
    TextStyle centred = Style(12, true, Color::Hex(0x0B0C0E), Align::kCenter);
    centred.vertical = Align::kCenter;
    c.Text(label, tag, centred);
  }
  c.PopClip();
  // The view's edges that cut the main player.
  if (const nui::DebugBody* main = MainBody(v)) {
    const float t = 4;
    if (main->quality & nui::kClippedLeft) c.FillRect({img.x, img.y, t, img.h}, kRed);
    if (main->quality & nui::kClippedRight) c.FillRect({img.right() - t, img.y, t, img.h}, kRed);
    if (main->quality & nui::kClippedTop) c.FillRect({img.x, img.y, img.w, t}, kRed);
    if (main->quality & nui::kClippedBottom) c.FillRect({img.x, img.bottom() - t, img.w, t}, kRed);
  }
  c.StrokeRect(img, kBorder);
}

void Renderer::DrawDepth(Canvas& c, const RectF& panel, const nui::DebugView& v) {
  const RectF area = Panel(c, panel, L"DEPTH");
  const RectF img = Fit(area, 4.0f / 3.0f);
  c.FillRect(img, kImageBackground);
  if (ID2D1Bitmap* bitmap = DepthBitmap(c, v)) c.DrawBitmap(bitmap, img, 1.0f, false, false);
  c.StrokeRect(img, kBorder);
}

// The play space from above: the sensor at the top, x across, z away from it.
void Renderer::DrawTop(Canvas& c, const RectF& panel, const nui::DebugView& v) {
  const RectF area = Panel(c, panel, L"FROM ABOVE");
  const float range_x = 5.0f, range_z = 5.0f;
  const float s = std::min(area.w / range_x, area.h / range_z);
  const float cx = area.cx(), sy = area.y;
  auto at = [&](float x, float z) { return D2D1_POINT_2F{cx + x * s, sy + z * s}; };
  c.PushClip(area);
  for (int m = 1; m <= 5; ++m) {  // a metre's grid
    const D2D1_POINT_2F a = at(-range_x / 2, float(m)), b = at(range_x / 2, float(m));
    c.Line(a.x, a.y, b.x, b.y, kBorder, 1);
    c.Text(Printf(L"%d m", m), {area.x, a.y - 15, 40, 14}, Style(10, false, kTextSecondary));
  }
  // The depth camera's view.
  const float half = kHorizontalFov / 2 * 3.14159265f / 180;
  const D2D1_POINT_2F sensor = at(0, 0);
  for (float side : {-1.0f, 1.0f}) {
    const D2D1_POINT_2F e = at(side * std::tan(half) * range_z, range_z);
    c.Line(sensor.x, sensor.y, e.x, e.y, kTextSecondary.Alpha(0.6f), 1);
  }
  // Where a Kinect player stands (2.3 m).
  for (float x = -1.2f; x < 1.2f; x += 0.2f) {
    const D2D1_POINT_2F a = at(x, 2.3f), b = at(x + 0.1f, 2.3f);
    c.Line(a.x, a.y, b.x, b.y, kGreen.Alpha(0.7f), 2);
  }
  c.FillRect({sensor.x - 14, sensor.y, 28, 6}, kText);
  for (const nui::DebugBody& b : v.bodies) {
    const D2D1_POINT_2F p = at(b.position[0], b.position[2]);
    const Color color = kBodyColors[b.slot % 6];
    if (b.tracked) c.FillCircle(p.x, p.y, 9, color);
    else c.StrokeCircle(p.x, p.y, 9, color, 2);
    c.Text(Printf(L"#%u  %.1f m", b.id, b.position[2]), {p.x + 12, p.y - 26, 120, 16}, Style(11, true, color));
  }
  c.PopClip();
}

// From the side: the floor, the sensor at its height and tilt, the bodies' y and z.
void Renderer::DrawSide(Canvas& c, const RectF& panel, const nui::DebugView& v) {
  RectF area = Panel(c, panel, L"FROM THE SIDE");
  const float camera_h = v.status.floor_height > 0 ? v.status.floor_height : v.camera_height;
  c.Text(Printf(L"camera %.2f m above the floor%s, %.0f° down, %.0f° roll", camera_h,
                v.status.floor_height > 0 ? L" (measured)" : L"", v.status.tilt_pitch, v.status.tilt_roll),
         {area.x, area.y - 2, area.w, 16}, Style(11, false, kTextSecondary));
  area = {area.x, area.y + 18, area.w, area.h - 18};
  const float range_z = 5.0f, top_y = 2.4f - camera_h, bottom_y = -camera_h - 0.15f;
  const float s = std::min(area.w / (range_z + 0.4f), area.h / (top_y - bottom_y));
  const float x0 = area.x + 0.3f * s, y0 = area.y + top_y * s;  // the sensor
  auto at = [&](float z, float y) { return D2D1_POINT_2F{x0 + z * s, y0 - y * s}; };
  c.PushClip(area);
  const D2D1_POINT_2F floor_a = at(-0.3f, -camera_h), floor_b = at(range_z, -camera_h);
  c.Line(floor_a.x, floor_a.y, floor_b.x, floor_b.y, kTextSecondary, 2);
  // The view: pitched down by the measured tilt and the title's elevation.
  const float pitch = (v.status.tilt_pitch - float(v.elevation)) * 3.14159265f / 180;
  const float half = kVerticalFov / 2 * 3.14159265f / 180;
  for (float side : {-1.0f, 1.0f}) {
    const float angle = -pitch + side * half;
    const D2D1_POINT_2F e = at(range_z * std::cos(angle), range_z * std::sin(angle));
    c.Line(x0, y0, e.x, e.y, kTextSecondary.Alpha(0.6f), 1);
  }
  c.FillRect({x0 - 6, y0 - 4, 12, 8}, kText);
  for (const nui::DebugBody& b : v.bodies) {
    const Color color = kBodyColors[b.slot % 6].Alpha(b.tracked ? 1.0f : 0.55f);
    for (const auto& bone : nui::kSkeletonBones) {
      const D2D1_POINT_2F a = at(b.joints[bone[0]][2], b.joints[bone[0]][1]);
      const D2D1_POINT_2F e = at(b.joints[bone[1]][2], b.joints[bone[1]][1]);
      c.Line(a.x, a.y, e.x, e.y, color, 2);
    }
  }
  c.PopClip();
}

void Renderer::DrawJoints(Canvas& c, const RectF& panel, const nui::DebugView& v) {
  const nui::DebugBody* main = MainBody(v);
  const RectF area = Panel(c, panel, main ? Printf(L"JOINTS OF #%u", main->id).c_str() : L"JOINTS");
  if (!main) {
    c.Text(L"Nobody in view.", area, Style(13, false, kTextSecondary));
    return;
  }
  const float row = std::clamp(area.h / 21, 12.0f, 18.0f);
  const float font = std::min(12.0f, row - 3);
  const float cols[] = {area.x, area.x + area.w * 0.46f, area.x + area.w * 0.56f, area.x + area.w * 0.71f,
                        area.x + area.w * 0.86f};
  const wchar_t* headings[] = {L"joint", L"state", L"x", L"y", L"z"};
  for (int i = 0; i < 5; ++i)
    c.Text(headings[i], {cols[i], area.y, (i < 4 ? cols[i + 1] : area.right()) - cols[i] - 2, row},
           Style(font, true, kTextSecondary));
  c.PushClip(area);
  for (uint32_t j = 0; j < nui::kJointCount; ++j) {
    const float y = area.y + row * float(j + 1);
    if (j % 2 == 0) c.FillRect({area.x - 4, y, area.w + 8, row}, Color::Hex(0xFFFFFF, 0.025f));
    c.Text(Wide(kJointNames[j]), {cols[0], y, cols[1] - cols[0], row}, Style(font, false, kText));
    const uint8_t state = main->joint_state[j];
    const Color color = state == nui::kJointTracked ? kGreen : state == nui::kJointInferred ? kAmber : kRed;
    c.FillCircle(cols[1] + 8, y + row / 2, 4, color);  // green tracked, amber inferred, red not tracked
    for (int k = 0; k < 3; ++k)
      c.Text(Printf(L"%+.2f", main->joints[j][k]), {cols[2 + k], y, cols[3] - cols[2], row}, Style(font, false, kText));
  }
  c.PopClip();
}

void Renderer::DrawSensor(Canvas& c, const RectF& panel, const nui::DebugView& v) {
  const RectF area = Panel(c, panel, L"SENSOR");
  const float label_w = std::min(124.0f, area.w * 0.4f), row = 20;
  float y = area.y;
  auto line = [&](const wchar_t* label, const std::wstring& value, Color color = kText) {
    c.Text(label, {area.x, y, label_w, row}, Style(12, false, kTextSecondary));
    c.Text(value, {area.x + label_w, y, area.w - label_w, row}, Style(12, false, color));
    y += row;
  };
  const nui::Status& s = v.status;
  line(L"Source", Printf(L"%hs  (%hs)", s.source.empty() ? "none" : s.source.c_str(), s.message.c_str()));
  line(L"Camera", s.camera_running ? Printf(L"%ux%u at %.0f fps, latency %.0f ms", s.camera_width, s.camera_height,
                                           s.camera_fps, s.latency_ms)
                                    : std::wstring(L"none"));
  static const wchar_t* const kTiltSources[] = {L"measuring", L"from the body", L"from the floor", L"set by hand"};
  line(L"Tilt", Printf(L"%.0f° down, %.0f° roll (%ls)", s.tilt_pitch, s.tilt_roll,
                       kTiltSources[std::min<uint32_t>(s.tilt_source, 3)]));
  line(L"Title's elevation", Printf(L"%+d°", v.elevation));
  line(L"Streams opened", v.streams & 3 ? Printf(L"%ls%ls%ls", v.streams & 1 ? L"color" : L"",
                                                 (v.streams & 3) == 3 ? L", " : L"", v.streams & 2 ? L"depth" : L"")
                                        : std::wstring(L"none"));
  std::wstring tracking = !v.tracking ? L"off"
                          : !v.title_chooses
                              ? L"on, the first in view"
                              : Printf(L"on, the title chooses: %ls%ls", v.chosen[0] ? Printf(L"#%u", v.chosen[0]).c_str() : L"none",
                                       v.chosen[1] ? Printf(L", #%u", v.chosen[1]).c_str() : L"");
  line(L"Skeleton tracking", tracking, v.tracking ? kText : kTextSecondary);
  // The Kinect Guide gesture: how long the pose has been held.
  c.Text(L"Guide gesture", {area.x, y, label_w, row}, Style(12, false, kTextSecondary));
  const RectF bar{area.x + label_w, y + 6, std::min(160.0f, area.w - label_w - 80), 7};
  c.FillRoundedRect(bar, 3.5f, kPanelHigh);
  c.FillRoundedRect({bar.x, bar.y, bar.w * v.guide_progress, bar.h}, 3.5f, v.guide_open ? kGreen : kAmber);
  c.Text(v.guide_open ? L"open" : Printf(L"%.0f %%", v.guide_progress * 100), {bar.right() + 10, y, 70, row},
         Style(12, false, kText));
  y += row;
  line(L"Bridge", Printf(L"%u restart%ls, %u frames dropped", s.restarts, s.restarts == 1 ? L"" : L"s", s.dropped_frames));
  line(L"Pose", Printf(L"%.0f ms a frame", s.inference_ms));
}

void Renderer::DrawTimings(Canvas& c, const RectF& panel, const std::vector<nui::TimingSample>& samples) {
  const RectF area = Panel(c, panel, L"TIMINGS, LAST 10 SECONDS");
  const RectF graph{area.x + 28, area.y, area.w - 28, area.h - 36};
  float top = 50;
  for (const nui::TimingSample& s : samples)
    top = std::max({top, s.capture_ms + s.detect_ms + s.process_ms + s.render_ms + s.transfer_ms, s.period_ms});
  top = std::ceil(top / 25) * 25;
  auto y_of = [&](float ms) { return graph.bottom() - std::min(ms, top) / top * graph.h; };
  for (float ms = 0; ms <= top; ms += 25) {
    c.Line(graph.x, y_of(ms), graph.right(), y_of(ms), kBorder, 1);
    c.Text(Printf(L"%.0f", ms), {area.x, y_of(ms) - 7, 26, 14}, Style(10, false, kTextSecondary, Align::kEnd));
  }
  const float frame_line = y_of(1000.0f / 30);
  c.Line(graph.x, frame_line, graph.right(), frame_line, kGreen.Alpha(0.35f), 1);
  struct Stage {
    const wchar_t* name;
    Color color;
    float nui::TimingSample::*field;
  };
  const Stage stages[] = {{L"waiting", Color::Hex(0x6C7A89), &nui::TimingSample::capture_ms},
                          {L"pose", Color::Hex(0x3CB043), &nui::TimingSample::detect_ms},
                          {L"mapping", Color::Hex(0x3B8EDB), &nui::TimingSample::process_ms},
                          {L"images", Color::Hex(0xF0B429), &nui::TimingSample::render_ms},
                          {L"to the game", Color::Hex(0xE05A9A), &nui::TimingSample::transfer_ms}};
  if (samples.size() >= 2) {
    const float t0 = samples.back().time - 10.0f;
    auto x_of = [&](float t) { return graph.x + std::clamp((t - t0) / 10.0f, 0.0f, 1.0f) * graph.w; };
    // The stages stacked, as filled bands.
    std::vector<float> below(samples.size(), 0.0f);
    for (const Stage& stage : stages) {
      std::vector<D2D1_POINT_2F> band;
      for (size_t i = 0; i < samples.size(); ++i)
        band.push_back({x_of(samples[i].time), y_of(below[i] + samples[i].*stage.field)});
      for (size_t i = samples.size(); i-- > 0;) band.push_back({x_of(samples[i].time), y_of(below[i])});
      c.FillPolygon(band, stage.color.Alpha(0.75f));
      for (size_t i = 0; i < samples.size(); ++i) below[i] += samples[i].*stage.field;
    }
    // The time between sensor frames.
    std::vector<D2D1_POINT_2F> period;
    for (const nui::TimingSample& s : samples) period.push_back({x_of(s.time), y_of(s.period_ms)});
    c.Polyline(period, kText.Alpha(0.85f), 1.2f);
  }
  // The legend, on as many lines as it needs (a narrow window).
  const TextStyle style = Style(11, false, kTextSecondary);
  float x = graph.x, y = area.bottom() - 13;
  auto item = [&](const wchar_t* name, const Color& color, bool line) {
    const float width = c.MeasureText(name, style).width + 30;
    if (x + width > area.right() && x > graph.x) x = graph.x, y += 15;
    if (line) c.Line(x, y + 7, x + 10, y + 7, color, 1.5f);
    else c.FillRect({x, y + 3, 9, 9}, color);
    c.Text(name, {x + 14, y, width, 15}, style);
    x += width;
  };
  float lines = 1, probe = graph.x;
  for (const Stage& stage : stages) {
    const float width = c.MeasureText(stage.name, style).width + 30;
    if (probe + width > area.right() && probe > graph.x) probe = graph.x, ++lines;
    probe += width;
  }
  if (probe + c.MeasureText(L"between frames", style).width + 30 > area.right()) ++lines;
  y -= (lines - 1) * 15;
  for (const Stage& stage : stages) item(stage.name, stage.color, false);
  item(L"between frames", kText, true);
}

// ---- the window and its thread ------------------------------------------------------------------

std::mutex g_mutex;
std::thread g_thread;
std::atomic<bool> g_open{false}, g_stop{false};
std::atomic<HWND> g_hwnd{nullptr};
std::function<void(bool, const std::string&)> g_changed;
Canvas* g_canvas = nullptr;

std::string PlacementOf(HWND hwnd) {
  WINDOWPLACEMENT placement{sizeof(placement)};
  if (!GetWindowPlacement(hwnd, &placement)) return {};
  const RECT& r = placement.rcNormalPosition;
  char text[96];
  std::snprintf(text, sizeof(text), "%ld,%ld,%ld,%ld,%d", r.left, r.top, r.right, r.bottom,
                placement.showCmd == SW_SHOWMAXIMIZED ? 1 : 0);
  return text;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CLOSE:
      g_stop = true;
      return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
      ValidateRect(hwnd, nullptr);  // drawn by the thread's loop
      return 0;
    case WM_SIZE:
      if (g_canvas) g_canvas->Resize();
      return 0;
    case WM_DPICHANGED: {
      const RECT* r = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      if (g_canvas) g_canvas->Resize();
      return 0;
    }
    case WM_GETMINMAXINFO: {
      const UINT dpi = GetDpiForWindow(hwnd);
      RECT frame{0, 0, LONG(kMinWidth * dpi / 96), LONG(kMinHeight * dpi / 96)};
      AdjustWindowRectExForDpi(&frame, DWORD(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE, 0, dpi);
      reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = {frame.right - frame.left, frame.bottom - frame.top};
      return 0;
    }
    case WM_EXITSIZEMOVE:
      if (g_changed) g_changed(true, PlacementOf(hwnd));
      return 0;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

// The first time: on a monitor the game isn't on, else on the game's, centred.
void PlaceFirstTime(HWND hwnd, HWND game) {
  struct Search {
    HMONITOR avoid;
    HMONITOR found = nullptr;
  } search{game ? MonitorFromWindow(game, MONITOR_DEFAULTTOPRIMARY) : nullptr};
  EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
    Search& s = *reinterpret_cast<Search*>(data);
    if (monitor == s.avoid) return TRUE;
    s.found = monitor;
    return FALSE;
  }, LPARAM(&search));
  HMONITOR monitor = search.found ? search.found : search.avoid ? search.avoid : MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO info{sizeof(info)};
  GetMonitorInfoW(monitor, &info);
  UINT dpi_x = 96, dpi_y = 96;
  using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
  if (HMODULE shcore = LoadLibraryW(L"shcore.dll"))
    if (auto get = reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor"))) get(monitor, 0, &dpi_x, &dpi_y);
  RECT frame{0, 0, LONG(kWidth * dpi_x / 96), LONG(kHeight * dpi_x / 96)};
  AdjustWindowRectExForDpi(&frame, DWORD(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE, 0, dpi_x);
  const RECT& work = info.rcWork;
  const int w = std::min<int>(frame.right - frame.left, work.right - work.left);
  const int h = std::min<int>(frame.bottom - frame.top, work.bottom - work.top);
  SetWindowPos(hwnd, nullptr, (work.left + work.right - w) / 2, (work.top + work.bottom - h) / 2, w, h,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

// Where it was, if that is still on a monitor.
bool PlaceAsBefore(HWND hwnd, const std::string& saved, bool& maximized) {
  RECT r{};
  int max = 0;
  if (std::sscanf(saved.c_str(), "%ld,%ld,%ld,%ld,%d", &r.left, &r.top, &r.right, &r.bottom, &max) != 5) return false;
  if (r.right - r.left < 200 || r.bottom - r.top < 150 || !MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) return false;
  WINDOWPLACEMENT placement{sizeof(placement)};
  placement.showCmd = SW_HIDE;
  placement.rcNormalPosition = r;
  SetWindowPlacement(hwnd, &placement);
  maximized = max != 0;
  return true;
}

void Thread(std::string placement, HWND game) {
  SetThreadDescription(GetCurrentThread(), L"Kinect window");
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  static const bool fonts = anyx360::ui::AddFont(fonts::InterRegular()) && anyx360::ui::AddFont(fonts::InterSemiBold());
  (void)fonts;
  static const ATOM window_class = [] {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), L"MAINICON");  // the game's (runtime/game.rc)
    wc.hbrBackground = CreateSolidBrush(RGB(0x12, 0x14, 0x17));
    wc.lpszClassName = L"AnyX360KinectWindow";
    return RegisterClassExW(&wc);
  }();
  (void)window_class;
  // No owner: an owned window would share the game's input queue.
  HWND hwnd = CreateWindowExW(0, L"AnyX360KinectWindow", L"AnyX360 Kinect", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                              CW_USEDEFAULT, 800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!hwnd) {
    g_open = false;
    return;
  }
  BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
  COLORREF caption = RGB(0x12, 0x14, 0x17);
  DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
  bool maximized = false;
  if (placement.empty() || !PlaceAsBefore(hwnd, placement, maximized)) PlaceFirstTime(hwnd, game);
  g_hwnd = hwnd;
  // Shown without taking the focus: activating it would release the game's captured cursor.
  ShowWindow(hwnd, SW_SHOWNOACTIVATE);  // not maximized again: that would activate it
  (void)maximized;
  nui::SetDebugImages(true);

  {
    Canvas canvas(hwnd);
    Renderer renderer;
    g_canvas = &canvas;
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LARGE_INTEGER due;
    due.QuadPart = -1;
    SetWaitableTimer(timer, &due, 33, nullptr, nullptr, FALSE);  // 30 times a second
    while (!g_stop) {
      const DWORD woke = MsgWaitForMultipleObjectsEx(1, &timer, 250, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      if (woke == WAIT_OBJECT_0 && !g_stop && !IsIconic(hwnd) && canvas.Begin()) {
        renderer.Draw(canvas);
        canvas.End();
      }
    }
    CloseHandle(timer);
    g_canvas = nullptr;
  }
  nui::SetDebugImages(false);
  const std::string last = PlacementOf(hwnd);
  g_hwnd = nullptr;
  DestroyWindow(hwnd);
  g_open = false;
  if (g_changed) g_changed(false, last);
}

}  // namespace

void Open(const std::string& placement, void* game_window,
          std::function<void(bool open, const std::string& placement)> changed) {
  std::lock_guard lock(g_mutex);
  if (g_open) return;
  if (g_thread.joinable()) g_thread.join();
  g_stop = false;
  g_open = true;
  g_changed = std::move(changed);
  g_thread = std::thread(Thread, placement, static_cast<HWND>(game_window));
}

void Close() {
  if (!g_open) return;
  g_stop = true;
  if (HWND hwnd = g_hwnd) PostMessageW(hwnd, WM_NULL, 0, 0);
}

bool IsOpen() { return g_open; }

}  // namespace x360::kinect_window
