#include "pet_palette.h"

#include "pet_render.h"
#include "pet_sprites.h"

namespace pet {

void applyColors(uint16_t palette565[], const uint32_t colors[2]) {
    palette565[kPaintSlot] = rgb565(colors[0]);
    palette565[kPaintLightSlot] = rgb565(colors[1]);
}

}  // namespace pet
