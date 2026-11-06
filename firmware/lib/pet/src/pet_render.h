#pragma once
// Draws the shared pixel art into ARGB8888 buffers (LVGL canvases) at whole-number scales,
// nearest-neighbor, like the app (app spec 4.6). Everything clips to the buffer.
#include <stdint.h>

#include "cta_types.h"
#include "pet_sprites.h"

namespace pet {

enum class PetState : uint8_t { Sleepy, Sad, Worried, Happy, Hyped, Asleep };

struct Canvas {
    uint32_t* px;   // 0xAARRGGBB
    int w, h;
};

PetState petStateFor(cta::Mood m, bool docked);   // docked → Asleep, else 1:1 with Mood
const char* moodWord(PetState s);                 // the bubble's word, from the handheld mockup

// An indexed sprite through `palette` (0xRRGGBB, slot 0 skipped), its top-left at (x, y).
void drawSprite(Canvas c, const Sprite& s, const uint32_t* palette, int scale, int x, int y);
// A one-color sprite (any nonzero index) in `rgb`.
void drawMono(Canvas c, const Sprite& s, uint32_t rgb, int scale, int x, int y);
// Trackie with his face for `state`, painted in the pet colors, and the weather accessory (none while asleep).
// (x, y) is the top-left of his 32x32 body; the umbrella reaches 6 art pixels above it.
void drawPet(Canvas c, PetState state, cta::Accessory acc, const uint32_t colors[2], int scale, int x, int y);

// The pixel digits: width in output pixels, and drawing (unknown characters draw as a blank).
int textWidth(const char* text, int scale);
void drawText(Canvas c, const char* text, uint32_t rgb, int scale, int x, int y);
constexpr int kFontHeight = 7;

// The status icon for an Open-Meteo code: rain and storms the cloud, snow the snowflake, clear the sun.
const Sprite& weatherIcon(int wmo);

}  // namespace pet
