// Images through the Windows Imaging Component: decoding any format Windows reads (JPEG, PNG, BMP,
// TIFF, HEIC...), turned upright by its EXIF orientation and converted to premultiplied BGRA
// whatever its pixel format (CMYK included), scaled while decoding; box art's front cover; JPEG
// and PNG output; a blurred copy for backgrounds. Callers initialise COM on their thread first. Not used in the game process.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace anyx360::ui {

struct Image {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> bgra;  // premultiplied BGRA, rows of width * 4 bytes
};

struct Rect {
  uint32_t x = 0, y = 0, width = 0, height = 0;
};

// Where the front cover is in a picture of a game's box: in a wraparound scan (back, spine and
// front of an Xbox 360 case, 273x183 mm: an aspect between 1.42 and 1.56) its right 47.3 %;
// otherwise the centre of the picture at 2:3.
Rect FrontCoverRect(uint32_t width, uint32_t height);

// Decodes `path` upright; `crop` (in the upright picture's pixels, a zero width meaning all of it)
// is taken first, then the result is scaled to fit within max_width x max_height keeping its
// aspect (0: no limit). False and `error` on failure.
bool DecodePicture(const std::filesystem::path& path, Image& out, std::string& error, uint32_t max_width = 0,
               uint32_t max_height = 0, const Rect* crop = nullptr);

// The front cover of a box picture (FrontCoverRect), scaled to fit width x height.
bool LoadFrontCover(const std::filesystem::path& path, uint32_t width, uint32_t height, Image& out,
                    std::string& error);

// Writes a JPEG (quality 0-1); alpha is dropped.
bool SaveJpeg(const Image& image, const std::filesystem::path& path, float quality, std::string& error);
// Writes a PNG (alpha kept).
bool SavePng(const Image& image, const std::filesystem::path& path, std::string& error);

// A small, blurred copy of `image` (`width` x `height`, cropped to that aspect from its centre):
// drawn stretched with smoothing it is a soft background. CPU only.
Image Blurred(const Image& image, uint32_t width, uint32_t height, int passes = 3);

}  // namespace anyx360::ui
