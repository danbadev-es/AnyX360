// Drawing with Direct2D and DirectWrite, for the launcher and the Kinect debug window: a canvas over
// any Direct2D render target (a window's, or a bitmap's for screenshots) in device-independent
// pixels (1/96 inch), text in Segoe UI or in fonts given from memory (Inter), pictures from
// premultiplied BGRA. No COM apartment needed (no WIC): safe in the game process.
//
// One Canvas per thread and target; the Direct2D factory is per thread (single-threaded), the
// DirectWrite one shared.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

namespace anyx360::ui {

using Microsoft::WRL::ComPtr;

struct Color {
  float r = 0, g = 0, b = 0, a = 1;
  // 0xRRGGBB.
  static constexpr Color Hex(uint32_t rgb, float alpha = 1.0f) {
    return {float((rgb >> 16) & 0xFF) / 255.0f, float((rgb >> 8) & 0xFF) / 255.0f, float(rgb & 0xFF) / 255.0f, alpha};
  }
  constexpr Color Alpha(float alpha) const { return {r, g, b, a * alpha}; }
  // Towards `other` by t (0-1).
  Color Mix(const Color& other, float t) const;
};

struct RectF {
  float x = 0, y = 0, w = 0, h = 0;
  float right() const { return x + w; }
  float bottom() const { return y + h; }
  float cx() const { return x + w / 2; }
  float cy() const { return y + h / 2; }
  bool Contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  RectF Inset(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
  RectF Inset(float d) const { return Inset(d, d); }
  RectF Offset(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
};

enum class Align { kStart, kCenter, kEnd };

struct TextStyle {
  std::wstring family = L"Segoe UI";
  float size = 14;            // DIPs
  DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
  Color color{1, 1, 1, 1};
  Align align = Align::kStart;
  Align vertical = Align::kStart;
  bool wrap = false;
  bool ellipsis = true;       // cut with "…" what doesn't fit (one line, or the last line)
  float line_height = 0;      // 0: the font's
};

// Fonts given from memory (an embedded TTF) join Windows' installed ones under their family name.
// Needs Windows 10 1703 or later; false otherwise (the family then falls back to Segoe UI).
bool AddFont(std::span<const uint8_t> ttf);

class Canvas {
 public:
  // The window's render target (created on first use, again after the device is lost), sized to its
  // client area at its DPI.
  explicit Canvas(HWND hwnd);
  // Any render target (a WIC bitmap's for screenshots).
  explicit Canvas(ComPtr<ID2D1RenderTarget> target);
  ~Canvas();

  // Starts a frame; false if there's nothing to draw on (a minimized window).
  bool Begin();
  // Ends it; false if the device was lost (everything device-bound is recreated on the next frame).
  bool End();
  // The window's client area changed size or DPI.
  void Resize();

  ID2D1RenderTarget* target() const { return target_.Get(); }
  float dpi() const;
  // The drawing area in DIPs.
  float width() const;
  float height() const;
  // Bumped whenever the target (and its bitmaps) is recreated.
  uint64_t generation() const { return generation_; }

  void Clear(const Color& color);
  void FillRect(const RectF& r, const Color& color);
  void FillRoundedRect(const RectF& r, float radius, const Color& color);
  void StrokeRect(const RectF& r, const Color& color, float width = 1);
  void StrokeRoundedRect(const RectF& r, float radius, const Color& color, float width = 1);
  void Line(float x0, float y0, float x1, float y1, const Color& color, float width = 1);
  void FillCircle(float cx, float cy, float radius, const Color& color);
  void StrokeCircle(float cx, float cy, float radius, const Color& color, float width = 1);
  // A vertical gradient from `top` to `bottom`.
  void FillGradient(const RectF& r, const Color& top, const Color& bottom);
  // An ellipse filled with a radial gradient from `center` to `edge` (transparent outside it).
  void FillRadial(float cx, float cy, float rx, float ry, const Color& center, const Color& edge);
  void StrokeEllipse(float cx, float cy, float rx, float ry, const Color& color, float width = 1);
  // A polyline through `points` (x, y pairs).
  void Polyline(std::span<const D2D1_POINT_2F> points, const Color& color, float width = 1);
  // The polygon `points` encloses, filled.
  void FillPolygon(std::span<const D2D1_POINT_2F> points, const Color& color);

  void Text(std::wstring_view text, const RectF& r, const TextStyle& style);
  // The size `text` takes, `max_width` wide at most (0: no limit).
  D2D1_SIZE_F MeasureText(std::wstring_view text, const TextStyle& style, float max_width = 0);

  // A bitmap from premultiplied BGRA pixels, bound to this target (recreate it when generation()
  // changes).
  ComPtr<ID2D1Bitmap> CreateBitmap(uint32_t width, uint32_t height, std::span<const uint8_t> bgra);
  // Draws `bitmap` into `dest`. `cover`: scaled to fill `dest` and cropped (like a poster); otherwise
  // stretched to it.
  void DrawBitmap(ID2D1Bitmap* bitmap, const RectF& dest, float opacity = 1, bool cover = false,
                  bool smooth = true);

  // Clips drawing to `r` (rounded with `radius`) until PopClip.
  void PushClip(const RectF& r, float radius = 0);
  void PopClip();
  // Fades what is drawn in `r` from `top` opacity at its top to `bottom` at its bottom, until PopClip.
  void PushFade(const RectF& r, float top, float bottom);
  // Draws mirrored upside down about the line y = `y` (a reflection), until PopMirror.
  void PushMirror(float y);
  void PopMirror();

 private:
  bool EnsureTarget();
  ID2D1SolidColorBrush* Brush(const Color& color);
  IDWriteTextFormat* Format(const TextStyle& style);

  HWND hwnd_ = nullptr;
  ComPtr<ID2D1RenderTarget> target_;
  ComPtr<ID2D1HwndRenderTarget> hwnd_target_;
  ComPtr<ID2D1SolidColorBrush> brush_;
  std::vector<bool> clip_is_layer_;
  std::vector<D2D1_MATRIX_3X2_F> transforms_;
  uint64_t generation_ = 0;
};

// The Direct2D factory of this thread.
ID2D1Factory* D2DFactory();
IDWriteFactory* DWriteFactory();

}  // namespace anyx360::ui
