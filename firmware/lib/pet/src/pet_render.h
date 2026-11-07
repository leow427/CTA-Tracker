#pragma once
// Trackie, the pet (spec 6.3, 11; plan 1, task 11): states and an index-sprite renderer.
#include <stdint.h>

#include "cta_types.h"

namespace pet {

// Indexed like kPet: the five moods in cta::Mood order, then Asleep.
enum class PetState : uint8_t { Sleepy, Sad, Worried, Happy, Hyped, Asleep };

// Docked → Asleep; otherwise one-to-one with the mood.
PetState petStateFor(cta::Mood m, bool docked);

// 0xRRGGBB to RGB565.
uint16_t rgb565(uint32_t rgb);

// A palette-index sprite: w × h indexes, 0 transparent, drawn dx, dy art pixels from the body's top left.
struct Sprite {
    const uint8_t* idx;
    uint8_t w, h;
    int8_t dx, dy;
};

// Draws s with the body's top left at destination pixel (x, y): each art pixel is a scale × scale block of
// palette565[idx], and index 0 leaves the destination untouched. Pixels outside dstW × dstH are clipped.
void renderSprite(const Sprite& s, int x, int y, const uint16_t* palette565, uint8_t scale, uint16_t* dst,
                  uint16_t dstW, uint16_t dstH);

}  // namespace pet
