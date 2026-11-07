#pragma once
// Trackie's sprites, generated into pet_sprites.cpp by tools/sprite_build.py from art/ (plan 1, task 11).
#include <stddef.h>
#include <stdint.h>

#include "pet_render.h"

namespace pet {

constexpr size_t kPaletteSize = 17;   // art/palette.txt's slots; sprite_build.py checks it
constexpr uint8_t kPaintSlot = 2;     // body: the app's pet paint replaces it
constexpr uint8_t kPaintLightSlot = 3;  // body light: replaced with the paint mixed 45% toward white

extern const Sprite kPet[6];             // by PetState: the body with that face stamped on the door, 32 × 32
extern const Sprite* const kPetExtra[6];  // floats at his upper right; nullptr for Sad
extern const Sprite kOverlayUmbrella;    // rain: held over the roof (drawn above the body)
extern const Sprite kOverlayScarf;       // snow or frost: over the bumper
// The weather overlay to draw for a state: nullptr for Accessory::None, and never while Asleep.
const Sprite* overlayFor(PetState state, cta::Accessory accessory);
extern const uint16_t kDefaultPalette565[kPaletteSize];

}  // namespace pet
