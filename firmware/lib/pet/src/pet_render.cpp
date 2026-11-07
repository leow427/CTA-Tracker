#include "pet_render.h"

namespace pet {

PetState petStateFor(cta::Mood m, bool docked) {
    if (docked) return PetState::Asleep;
    switch (m) {
        case cta::Mood::Sleepy: return PetState::Sleepy;
        case cta::Mood::Sad: return PetState::Sad;
        case cta::Mood::Worried: return PetState::Worried;
        case cta::Mood::Happy: return PetState::Happy;
        case cta::Mood::Hyped: return PetState::Hyped;
    }
    return PetState::Sleepy;
}

uint16_t rgb565(uint32_t rgb) {
    const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void renderSprite(const Sprite& s, int x, int y, const uint16_t* palette565, uint8_t scale, uint16_t* dst,
                  uint16_t dstW, uint16_t dstH) {
    if (s.idx == nullptr || dst == nullptr || palette565 == nullptr || scale == 0) return;
    for (int j = 0; j < s.h; ++j) {
        const int y0 = y + (s.dy + j) * scale;
        for (int i = 0; i < s.w; ++i) {
            const uint8_t index = s.idx[j * s.w + i];
            if (index == 0) continue;
            const uint16_t color = palette565[index];
            const int x0 = x + (s.dx + i) * scale;
            for (int py = y0; py < y0 + scale; ++py) {
                if (py < 0 || py >= dstH) continue;
                for (int px = x0; px < x0 + scale; ++px) {
                    if (px >= 0 && px < dstW) dst[py * dstW + px] = color;
                }
            }
        }
    }
}

}  // namespace pet
