#pragma once
// Declarations for the generated pet_sprites.cpp (tools/sprite_build.py). Indexed sprites use the palette
// in art/palette.txt (slot 0 transparent); one-color glyphs and font glyphs use 0 = clear, 1 = ink.
#include <stddef.h>
#include <stdint.h>

namespace pet {

struct Sprite { uint8_t w, h; const uint8_t* px; };   // row-major indices; px == nullptr means none
struct Overlay { Sprite sprite; int8_t row; };          // drawn with its top at this pet row
struct FontGlyph { char c; Sprite sprite; };

constexpr size_t kPaletteSize = 17;
constexpr uint8_t kSlotBody = 2;    // B: the pet's first color
constexpr uint8_t kSlotLight = 3;   // L: its light shade

extern const uint32_t kPalette[kPaletteSize];   // 0xRRGGBB
extern const Sprite kPets[6];                   // composed body + face, by PetState
extern const Sprite kExtras[6];                 // the heart, sweat drop, Zz... floating beside the pet
extern const Overlay kOverlayUmbrella, kOverlayScarf;
extern const Sprite kGlyphLive0, kGlyphLive1, kGlyphAlert, kGlyphSun, kGlyphCloud, kGlyphSnow, kGlyphWifi, kGlyphOff;
extern const FontGlyph kFont[];
extern const size_t kFontCount;

}  // namespace pet
