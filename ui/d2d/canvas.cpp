#include "d2d/canvas.h"

#include <dwrite_3.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <tuple>

namespace anyx360::ui {

namespace {

std::mutex g_fonts_mutex;
ComPtr<IDWriteFontCollection> g_fonts;              // installed fonts plus the ones given from memory
ComPtr<IDWriteFontSetBuilder1> g_font_builder;      // what g_fonts is made of
ComPtr<IDWriteInMemoryFontFileLoader> g_font_loader;
std::vector<std::vector<uint8_t>> g_font_data;      // kept alive for the loader

D2D1_COLOR_F ToD2D(const Color& c) { return {c.r, c.g, c.b, c.a}; }
D2D1_RECT_F ToD2D(const RectF& r) { return {r.x, r.y, r.x + r.w, r.y + r.h}; }

}  // namespace

Color Color::Mix(const Color& other, float t) const {
  return {r + (other.r - r) * t, g + (other.g - g) * t, b + (other.b - b) * t, a + (other.a - a) * t};
}

ID2D1Factory* D2DFactory() {
  thread_local ComPtr<ID2D1Factory> factory = [] {
    ComPtr<ID2D1Factory> created;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, created.GetAddressOf());
    return created;
  }();
  return factory.Get();
}

IDWriteFactory* DWriteFactory() {
  static ComPtr<IDWriteFactory> factory = [] {
    ComPtr<IDWriteFactory> created;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(created.GetAddressOf()));
    return created;
  }();
  return factory.Get();
}

bool AddFont(std::span<const uint8_t> ttf) {
  std::lock_guard lock(g_fonts_mutex);
  ComPtr<IDWriteFactory5> factory;
  if (FAILED(DWriteFactory()->QueryInterface(IID_PPV_ARGS(&factory)))) return false;
  if (!g_font_loader) {
    if (FAILED(factory->CreateInMemoryFontFileLoader(&g_font_loader)) ||
        FAILED(factory->RegisterFontFileLoader(g_font_loader.Get())) ||
        FAILED(factory->CreateFontSetBuilder(&g_font_builder)))
      return false;
    ComPtr<IDWriteFontSet> system;
    if (SUCCEEDED(factory->GetSystemFontSet(&system))) g_font_builder->AddFontSet(system.Get());
  }
  g_font_data.emplace_back(ttf.begin(), ttf.end());
  ComPtr<IDWriteFontFile> file;
  if (FAILED(g_font_loader->CreateInMemoryFontFileReference(factory.Get(), g_font_data.back().data(),
                                                            UINT32(g_font_data.back().size()), nullptr, &file)) ||
      FAILED(g_font_builder->AddFontFile(file.Get())))
    return false;
  ComPtr<IDWriteFontSet> set;
  ComPtr<IDWriteFontCollection1> collection;
  if (FAILED(g_font_builder->CreateFontSet(&set)) || FAILED(factory->CreateFontCollectionFromFontSet(set.Get(), &collection)))
    return false;
  g_fonts = collection;
  return true;
}

Canvas::Canvas(HWND hwnd) : hwnd_(hwnd) {}

Canvas::Canvas(ComPtr<ID2D1RenderTarget> target) : target_(std::move(target)) { ++generation_; }

Canvas::~Canvas() = default;

bool Canvas::EnsureTarget() {
  if (target_) return true;
  if (!hwnd_) return false;
  RECT client{};
  GetClientRect(hwnd_, &client);
  if (client.right <= 0 || client.bottom <= 0) return false;
  const float dpi = float(GetDpiForWindow(hwnd_));
  const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi,
      dpi);
  if (FAILED(D2DFactory()->CreateHwndRenderTarget(
          properties, D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(UINT32(client.right), UINT32(client.bottom))),
          &hwnd_target_)))
    return false;
  target_ = hwnd_target_;
  ++generation_;
  return true;
}

void Canvas::Resize() {
  if (!hwnd_target_) return;
  RECT client{};
  GetClientRect(hwnd_, &client);
  if (client.right <= 0 || client.bottom <= 0) return;
  const float dpi = float(GetDpiForWindow(hwnd_));
  hwnd_target_->SetDpi(dpi, dpi);
  hwnd_target_->Resize(D2D1::SizeU(UINT32(client.right), UINT32(client.bottom)));
}

bool Canvas::Begin() {
  if (!EnsureTarget()) return false;
  if (hwnd_target_ && (hwnd_target_->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED)) return false;
  target_->BeginDraw();
  target_->SetTransform(D2D1::Matrix3x2F::Identity());
  target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  return true;
}

bool Canvas::End() {
  while (!clip_is_layer_.empty()) PopClip();
  const HRESULT hr = target_->EndDraw();
  if (hr == D2DERR_RECREATE_TARGET && hwnd_) {
    brush_.Reset();
    hwnd_target_.Reset();
    target_.Reset();
    return false;
  }
  return SUCCEEDED(hr);
}

float Canvas::dpi() const {
  float x = 96, y = 96;
  if (target_) target_->GetDpi(&x, &y);
  return x;
}

float Canvas::width() const { return target_ ? target_->GetSize().width : 0; }
float Canvas::height() const { return target_ ? target_->GetSize().height : 0; }

ID2D1SolidColorBrush* Canvas::Brush(const Color& color) {
  if (!brush_) target_->CreateSolidColorBrush(ToD2D(color), &brush_);
  else brush_->SetColor(ToD2D(color));
  return brush_.Get();
}

void Canvas::Clear(const Color& color) { target_->Clear(ToD2D(color)); }

void Canvas::FillRect(const RectF& r, const Color& color) { target_->FillRectangle(ToD2D(r), Brush(color)); }

void Canvas::FillRoundedRect(const RectF& r, float radius, const Color& color) {
  if (radius <= 0) return FillRect(r, color);
  target_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(r), radius, radius), Brush(color));
}

void Canvas::StrokeRect(const RectF& r, const Color& color, float width) {
  target_->DrawRectangle(ToD2D(r.Inset(width / 2)), Brush(color), width);
}

void Canvas::StrokeRoundedRect(const RectF& r, float radius, const Color& color, float width) {
  if (radius <= 0) return StrokeRect(r, color, width);
  target_->DrawRoundedRectangle(D2D1::RoundedRect(ToD2D(r.Inset(width / 2)), radius, radius), Brush(color), width);
}

void Canvas::Line(float x0, float y0, float x1, float y1, const Color& color, float width) {
  target_->DrawLine({x0, y0}, {x1, y1}, Brush(color), width);
}

void Canvas::FillCircle(float cx, float cy, float radius, const Color& color) {
  target_->FillEllipse(D2D1::Ellipse({cx, cy}, radius, radius), Brush(color));
}

void Canvas::StrokeCircle(float cx, float cy, float radius, const Color& color, float width) {
  target_->DrawEllipse(D2D1::Ellipse({cx, cy}, radius, radius), Brush(color), width);
}

void Canvas::FillGradient(const RectF& r, const Color& top, const Color& bottom) {
  D2D1_GRADIENT_STOP stops[] = {{0, ToD2D(top)}, {1, ToD2D(bottom)}};
  ComPtr<ID2D1GradientStopCollection> collection;
  ComPtr<ID2D1LinearGradientBrush> brush;
  if (FAILED(target_->CreateGradientStopCollection(stops, 2, &collection)) ||
      FAILED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({r.x, r.y}, {r.x, r.bottom()}),
                                                collection.Get(), &brush)))
    return;
  target_->FillRectangle(ToD2D(r), brush.Get());
}

void Canvas::Polyline(std::span<const D2D1_POINT_2F> points, const Color& color, float width) {
  if (points.size() < 2) return;
  ComPtr<ID2D1PathGeometry> path;
  ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(D2DFactory()->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) return;
  sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_HOLLOW);
  sink->AddLines(points.data() + 1, UINT32(points.size() - 1));
  sink->EndFigure(D2D1_FIGURE_END_OPEN);
  sink->Close();
  target_->DrawGeometry(path.Get(), Brush(color), width);
}

void Canvas::FillPolygon(std::span<const D2D1_POINT_2F> points, const Color& color) {
  if (points.size() < 3) return;
  ComPtr<ID2D1PathGeometry> path;
  ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(D2DFactory()->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) return;
  sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_FILLED);
  sink->AddLines(points.data() + 1, UINT32(points.size() - 1));
  sink->EndFigure(D2D1_FIGURE_END_CLOSED);
  sink->Close();
  target_->FillGeometry(path.Get(), Brush(color));
}

IDWriteTextFormat* Canvas::Format(const TextStyle& style) {
  // One cache per thread: a format's alignment is set for each use.
  thread_local std::map<std::tuple<std::wstring, float, int>, ComPtr<IDWriteTextFormat>> formats;
  auto key = std::make_tuple(style.family, style.size, int(style.weight));
  auto found = formats.find(key);
  if (found != formats.end()) return found->second.Get();
  ComPtr<IDWriteFontCollection> fonts;
  {
    std::lock_guard lock(g_fonts_mutex);
    fonts = g_fonts;
  }
  ComPtr<IDWriteTextFormat> format;
  if (FAILED(DWriteFactory()->CreateTextFormat(style.family.c_str(), fonts.Get(), style.weight,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, style.size,
                                               L"", &format)))
    DWriteFactory()->CreateTextFormat(L"Segoe UI", nullptr, style.weight, DWRITE_FONT_STYLE_NORMAL,
                                      DWRITE_FONT_STRETCH_NORMAL, style.size, L"", &format);
  return (formats[key] = format).Get();
}

namespace {

ComPtr<IDWriteTextLayout> Layout(IDWriteTextFormat* format, std::wstring_view text, const TextStyle& style, float width,
                                 float height) {
  ComPtr<IDWriteTextLayout> layout;
  if (!format || FAILED(DWriteFactory()->CreateTextLayout(text.data(), UINT32(text.size()), format,
                                                          std::max(width, 0.0f), std::max(height, 0.0f), &layout)))
    return nullptr;
  layout->SetTextAlignment(style.align == Align::kCenter ? DWRITE_TEXT_ALIGNMENT_CENTER
                           : style.align == Align::kEnd  ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                         : DWRITE_TEXT_ALIGNMENT_LEADING);
  layout->SetParagraphAlignment(style.vertical == Align::kCenter ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                : style.vertical == Align::kEnd  ? DWRITE_PARAGRAPH_ALIGNMENT_FAR
                                                                 : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  layout->SetWordWrapping(style.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
  if (style.line_height > 0)
    layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, style.line_height, style.line_height * 0.8f);
  if (style.ellipsis) {
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    ComPtr<IDWriteInlineObject> sign;
    DWriteFactory()->CreateEllipsisTrimmingSign(format, &sign);
    layout->SetTrimming(&trimming, sign.Get());
  }
  return layout;
}

}  // namespace

void Canvas::Text(std::wstring_view text, const RectF& r, const TextStyle& style) {
  if (text.empty()) return;
  ComPtr<IDWriteTextLayout> layout = Layout(Format(style), text, style, r.w, r.h);
  if (!layout) return;
  target_->DrawTextLayout({r.x, r.y}, layout.Get(), Brush(style.color), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

D2D1_SIZE_F Canvas::MeasureText(std::wstring_view text, const TextStyle& style, float max_width) {
  TextStyle measured = style;
  measured.ellipsis = false;
  if (!max_width) measured.wrap = false;
  ComPtr<IDWriteTextLayout> layout = Layout(Format(style), text, measured, max_width ? max_width : 100000.0f, 100000.0f);
  DWRITE_TEXT_METRICS metrics{};
  if (!layout || FAILED(layout->GetMetrics(&metrics))) return {0, 0};
  return {metrics.widthIncludingTrailingWhitespace, metrics.height};
}

ComPtr<ID2D1Bitmap> Canvas::CreateBitmap(uint32_t width, uint32_t height, std::span<const uint8_t> bgra) {
  ComPtr<ID2D1Bitmap> bitmap;
  if (!EnsureTarget() || !width || !height || bgra.size() < size_t(width) * height * 4) return bitmap;
  target_->CreateBitmap(D2D1::SizeU(width, height), bgra.data(), width * 4,
                        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                        &bitmap);
  return bitmap;
}

void Canvas::DrawBitmap(ID2D1Bitmap* bitmap, const RectF& dest, float opacity, bool cover, bool smooth) {
  if (!bitmap) return;
  const D2D1_SIZE_F size = bitmap->GetSize();
  D2D1_RECT_F source{0, 0, size.width, size.height};
  if (cover && size.width > 0 && size.height > 0 && dest.w > 0 && dest.h > 0) {
    const float scale = std::max(dest.w / size.width, dest.h / size.height);
    const float w = dest.w / scale, h = dest.h / scale;
    source = {(size.width - w) / 2, (size.height - h) / 2, (size.width + w) / 2, (size.height + h) / 2};
  }
  target_->DrawBitmap(bitmap, ToD2D(dest), opacity,
                      smooth ? D2D1_BITMAP_INTERPOLATION_MODE_LINEAR : D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                      source);
}

void Canvas::PushClip(const RectF& r, float radius) {
  if (radius <= 0) {
    target_->PushAxisAlignedClip(ToD2D(r), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    clip_is_layer_.push_back(false);
    return;
  }
  ComPtr<ID2D1RoundedRectangleGeometry> shape;
  ComPtr<ID2D1Layer> layer;
  D2DFactory()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(ToD2D(r), radius, radius), &shape);
  target_->CreateLayer(&layer);
  target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), shape.Get()), layer.Get());
  clip_is_layer_.push_back(true);
}

void Canvas::PopClip() {
  if (clip_is_layer_.empty()) return;
  if (clip_is_layer_.back()) target_->PopLayer();
  else target_->PopAxisAlignedClip();
  clip_is_layer_.pop_back();
}

}  // namespace anyx360::ui
