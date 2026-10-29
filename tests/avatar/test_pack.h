// A made-up avatar asset pack (AvatarAssetPack.toc) for the tests: a few parts of each kind, with
// the console's layout (big-endian table, 0xF8-byte entries, UTF-16 names, STRB files with metadata,
// an empty model and colour tables), so x360tool::AvatarCatalog reads it as it reads the real one.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace test_pack {

struct Item {
  const char* name;
  uint32_t word0;   // category bits
  uint32_t style;   // hairstyles: their style group (the second word's second byte)
  uint8_t gender;   // 1 male, 2 female, 3 both
  uint8_t type;     // 1 component, 2 texture, 3 shape override, 0 no data
  int linked;       // the entry it links to (-1 none)
  int colorways;    // colour table entries
};

// The entries, by number (an entry's index is its asset number).
enum : uint32_t {
  kMaleBody, kFemaleBody, kHead, kCrewCut, kCrewCutHat, kShoulderLength, kMaleTee, kFemaleTop, kDress, kJeans, kSkirt,
  kSneakers, kCap, kShades, kWatch, kEyes, kEyebrows, kMouth, kBeard, kNose, kTeeTemplate, kEarrings, kFemaleEyes,
  kCount
};

inline const Item kItems[kCount] = {
    {"MaleBody", 0x2, 0, 1, 1, -1, 0},
    {"FemaleBody", 0x2, 0, 2, 1, -1, 0},
    {"Head", 0x1, 0, 3, 1, -1, 1},
    {"Crew Cut", 0x4, 1, 3, 1, kCrewCutHat, 0},
    {"Crew Cut (Hat)", 0x4, 1, 3, 1, -1, 0},
    {"Shoulder Length", 0x4, 2, 3, 1, -1, 0},
    {"Male Tee", 0x8, 0, 1, 1, kTeeTemplate, 3},
    {"Female Top", 0x8, 0, 2, 1, -1, 2},
    {"Dress", 0x18, 0, 2, 1, -1, 2},
    {"Jeans", 0x10, 0, 1, 1, -1, 2},
    {"Skirt", 0x10, 0, 2, 1, -1, 0},
    {"Sneakers", 0x20, 0, 3, 1, -1, 2},
    {"Cap", 0x40, 0, 3, 1, -1, 2},
    {"Shades", 0x100, 0, 3, 1, -1, 0},
    {"Watch", 0x200, 0, 3, 1, -1, 0},
    {"Eyes", 0x2000, 0, 3, 2, -1, 0},
    {"Eyebrows", 0x4000, 0, 3, 2, -1, 0},
    {"Mouth", 0x8000, 0, 3, 2, -1, 0},
    {"Beard", 0x10000, 0, 1, 2, -1, 0},
    {"Nose", 0x80000, 0, 3, 3, -1, 0},
    {"Tee Hiding Template", 0x01000000, 0, 1, 3, -1, 0},
    {"Earrings", 0x400, 0, 2, 1, -1, 0},
    {"Female Eyes", 0x2000, 0, 2, 2, -1, 0},
};

inline void Be(std::vector<uint8_t>& b, size_t at, uint32_t v, int bytes = 4) {
  for (int i = 0; i < bytes; ++i) b[at + i] = uint8_t(v >> (8 * (bytes - 1 - i)));
}
inline void Le(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
}

// A little-endian STRB file: metadata (block 8, version 2), and for components an empty model (no
// meshes, no textures, the end of the weld list) and the colour table.
inline std::vector<uint8_t> Strb(const Item& item, uint32_t index) {
  std::vector<uint8_t> f = {'S', 'T', 'R', 'B', 0, 1};
  f.resize(0x1A, 0);
  f[0x16] = 4, f[0x17] = 4;
  auto block = [&](uint32_t id, const std::vector<uint8_t>& data) {
    Le(f, id), Le(f, uint32_t(data.size())), Le(f, 1);
    f.insert(f.end(), data.begin(), data.end());
  };
  std::vector<uint8_t> meta = {2, item.gender};
  Le(meta, item.type), Le(meta, 0), Le(meta, 0);
  meta.push_back(2);
  block(8, meta);
  if (item.type == 1) {
    std::vector<uint8_t> model;
    for (int i = 0; i < 12; ++i) Le(model, 0);
    Le(model, 1), Le(model, 0), Le(model, 0);  // a weld block of one value: the list's end
    block(3, model);
    if (item.colorways) {
      std::vector<uint8_t> table;
      Le(table, uint32_t(item.colorways));
      for (int c = 0; c < item.colorways; ++c)
        for (uint32_t k = 0; k < 3; ++k) Le(table, 0xFF000000u | (index << 16) | (uint32_t(c) << 8) | k), Le(table, 0);
      block(7, table);
    }
  }
  return f;
}

inline std::vector<uint8_t> MakeToc() {
  constexpr size_t kHeader = 0x28, kEntry = 0xF8;
  std::vector<uint8_t> toc(kHeader + kCount * kEntry, 0);
  Be(toc, 0x1C, kCount);
  std::vector<size_t> names;
  for (uint32_t i = 0; i < kCount; ++i) {
    names.push_back(toc.size());
    for (const char* c = kItems[i].name; *c; ++c) toc.push_back(0), toc.push_back(uint8_t(*c));
    toc.push_back(0), toc.push_back(0);
  }
  for (uint32_t i = 0; i < kCount; ++i) {
    const Item& item = kItems[i];
    const size_t e = kHeader + i * kEntry;
    Be(toc, e, item.word0);
    Be(toc, e + 4, item.style << 16);
    if (item.linked >= 0) {
      Be(toc, e + 0x9C, kItems[item.linked].word0);
      Be(toc, e + 0xA0, uint32_t(item.linked), 2);
      Be(toc, e + 0xA2, kItems[item.linked].gender, 2);
      Be(toc, e + 0xA4, 0xC1C8F109);
      Be(toc, e + 0xA8, 0xA19CB2E0);
    }
    Be(toc, e + 0xBC, uint32_t(names[i]));
    if (item.type) {
      const std::vector<uint8_t> strb = Strb(item, i);
      Be(toc, e + 0xF0, uint32_t(toc.size()));
      Be(toc, e + 0xF4, uint32_t(strb.size()));
      toc.insert(toc.end(), strb.begin(), strb.end());
    }
  }
  return toc;
}

}  // namespace test_pack
