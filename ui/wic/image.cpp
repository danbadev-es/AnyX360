#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace anyx360::ui {

using Microsoft::WRL::ComPtr;

namespace {

std::string HrText(const char* what, HRESULT hr) {
  char text[96];
  std::snprintf(text, sizeof(text), "%s (0x%08lX)", what, static_cast<unsigned long>(hr));
  return text;
}

bool Factory(ComPtr<IWICImagingFactory>& factory, std::string& error) {
  const HRESULT hr =
      CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
  if (FAILED(hr)) error = HrText("the imaging component isn't available", hr);
  return SUCCEEDED(hr);
}

// EXIF orientation (1-8) to the transform that makes the picture upright.
WICBitmapTransformOptions UprightTransform(IWICBitmapFrameDecode* frame) {
  ComPtr<IWICMetadataQueryReader> reader;
  if (FAILED(frame->GetMetadataQueryReader(&reader))) return WICBitmapTransformRotate0;
  PROPVARIANT value;
  PropVariantInit(&value);
  USHORT orientation = 1;
  for (const wchar_t* query : {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}"}) {
    if (SUCCEEDED(reader->GetMetadataByName(query, &value))) {
      if (value.vt == VT_UI2) orientation = value.uiVal;
      PropVariantClear(&value);
      break;
    }
  }
  switch (orientation) {
    case 2: return WICBitmapTransformFlipHorizontal;
    case 3: return WICBitmapTransformRotate180;
    case 4: return WICBitmapTransformFlipVertical;
    case 5: return WICBitmapTransformOptions(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
    case 6: return WICBitmapTransformRotate90;
    case 7: return WICBitmapTransformOptions(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    case 8: return WICBitmapTransformRotate270;
    default: return WICBitmapTransformRotate0;
  }
}

}  // namespace

Rect FrontCoverRect(uint32_t width, uint32_t height) {
  if (!width || !height) return {};
  const double aspect = double(width) / height;
  if (aspect >= 1.42 && aspect <= 1.56) {  // a wraparound: back 129 mm, spine 15 mm, front 129 mm
    const uint32_t x = uint32_t(std::lround(width * (144.0 / 273.4)));
    return {x, 0, width - x, height};
  }
  // Anything else: the centre at 2:3.
  if (aspect > 2.0 / 3.0) {
    const uint32_t w = uint32_t(std::lround(height * 2.0 / 3.0));
    return {(width - w) / 2, 0, w, height};
  }
  const uint32_t h = uint32_t(std::lround(width * 3.0 / 2.0));
  return {0, (height - h) / 2, width, h};
}

bool LoadImage(const std::filesystem::path& path, Image& out, std::string& error, uint32_t max_width,
               uint32_t max_height, const Rect* crop) {
  out = {};
  ComPtr<IWICImagingFactory> factory;
  if (!Factory(factory, error)) return false;
  ComPtr<IWICBitmapDecoder> decoder;
  HRESULT hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &decoder);
  if (FAILED(hr)) {
    error = HrText("not a picture Windows can read", hr);
    return false;
  }
  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(hr = decoder->GetFrame(0, &frame))) {
    error = HrText("the picture has no image", hr);
    return false;
  }

  ComPtr<IWICBitmapSource> source = frame;
  if (const WICBitmapTransformOptions transform = UprightTransform(frame.Get()); transform != WICBitmapTransformRotate0) {
    ComPtr<IWICBitmapFlipRotator> rotator;
    if (SUCCEEDED(factory->CreateBitmapFlipRotator(&rotator)) && SUCCEEDED(rotator->Initialize(source.Get(), transform)))
      source = rotator;
  }
  UINT width = 0, height = 0;
  source->GetSize(&width, &height);

  if (crop && crop->width && crop->height) {
    WICRect rect{INT(std::min<uint32_t>(crop->x, width - 1)), INT(std::min<uint32_t>(crop->y, height - 1)), 0, 0};
    rect.Width = INT(std::min<uint32_t>(crop->width, width - rect.X));
    rect.Height = INT(std::min<uint32_t>(crop->height, height - rect.Y));
    ComPtr<IWICBitmapClipper> clipper;
    if (FAILED(hr = factory->CreateBitmapClipper(&clipper)) || FAILED(hr = clipper->Initialize(source.Get(), &rect))) {
      error = HrText("cannot crop the picture", hr);
      return false;
    }
    source = clipper;
    width = UINT(rect.Width);
    height = UINT(rect.Height);
  }

  double scale = 1.0;
  if (max_width) scale = std::min(scale, double(max_width) / width);
  if (max_height) scale = std::min(scale, double(max_height) / height);
  if (scale < 1.0) {
    const UINT w = std::max<UINT>(1, UINT(std::lround(width * scale)));
    const UINT h = std::max<UINT>(1, UINT(std::lround(height * scale)));
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(hr = factory->CreateBitmapScaler(&scaler)) ||
        FAILED(hr = scaler->Initialize(source.Get(), w, h, WICBitmapInterpolationModeHighQualityCubic))) {
      error = HrText("cannot scale the picture", hr);
      return false;
    }
    source = scaler;
    width = w;
    height = h;
  }

  ComPtr<IWICFormatConverter> converter;
  if (FAILED(hr = factory->CreateFormatConverter(&converter)) ||
      FAILED(hr = converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                        0.0, WICBitmapPaletteTypeCustom))) {
    error = HrText("cannot convert the picture's colors", hr);
    return false;
  }
  out.width = width;
  out.height = height;
  out.bgra.resize(size_t(width) * height * 4);
  if (FAILED(hr = converter->CopyPixels(nullptr, width * 4, UINT(out.bgra.size()), out.bgra.data()))) {
    out = {};
    error = HrText("cannot decode the picture", hr);
    return false;
  }
  return true;
}

bool LoadFrontCover(const std::filesystem::path& path, uint32_t width, uint32_t height, Image& out,
                    std::string& error) {
  // The upright size first (cheap: no pixels are decoded), to know where the front is.
  ComPtr<IWICImagingFactory> factory;
  if (!Factory(factory, error)) return false;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  HRESULT hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                  &decoder);
  if (FAILED(hr) || FAILED(hr = decoder->GetFrame(0, &frame))) {
    error = HrText("not a picture Windows can read", hr);
    return false;
  }
  UINT w = 0, h = 0;
  frame->GetSize(&w, &h);
  const WICBitmapTransformOptions transform = UprightTransform(frame.Get());
  if (transform & (WICBitmapTransformRotate90 | WICBitmapTransformRotate270)) std::swap(w, h);
  const Rect front = FrontCoverRect(w, h);
  return LoadImage(path, out, error, width, height, &front);
}

bool SaveJpeg(const Image& image, const std::filesystem::path& path, float quality, std::string& error) {
  if (!image.width || !image.height) {
    error = "no picture to write";
    return false;
  }
  ComPtr<IWICImagingFactory> factory;
  if (!Factory(factory, error)) return false;
  ComPtr<IWICBitmap> bitmap;
  HRESULT hr = factory->CreateBitmapFromMemory(image.width, image.height, GUID_WICPixelFormat32bppPBGRA, image.width * 4,
                                              UINT(image.bgra.size()), const_cast<BYTE*>(image.bgra.data()), &bitmap);
  ComPtr<IWICFormatConverter> converter;
  if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
  if (SUCCEEDED(hr))
    hr = converter->Initialize(bitmap.Get(), GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
  ComPtr<IWICStream> stream;
  if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
  if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  ComPtr<IWICBitmapEncoder> encoder;
  if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);
  if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> options;
  if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &options);
  if (SUCCEEDED(hr)) {
    PROPBAG2 option{};
    option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_R4;
    value.fltVal = std::clamp(quality, 0.0f, 1.0f);
    options->Write(1, &option, &value);
    hr = frame->Initialize(options.Get());
  }
  if (SUCCEEDED(hr)) hr = frame->SetSize(image.width, image.height);
  WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
  if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
  if (SUCCEEDED(hr)) hr = frame->WriteSource(converter.Get(), nullptr);
  if (SUCCEEDED(hr)) hr = frame->Commit();
  if (SUCCEEDED(hr)) hr = encoder->Commit();
  if (FAILED(hr)) {
    error = HrText("cannot write the JPEG", hr);
    return false;
  }
  return true;
}

}  // namespace anyx360::ui
