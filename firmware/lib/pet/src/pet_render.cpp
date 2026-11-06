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

const char* moodWord(PetState s) {
    static const char* const kWords[] = {"Sleepy...", "Uh oh...", "Running late", "All on time", "Go now!", "Zzz..."};
    return kWords[(int)s];
}

static void fill(Canvas c, int x, int y, int scale, uint32_t rgb) {
    uint32_t argb = 0xFF000000u | (rgb & 0xFFFFFF);
    for (int dy = 0; dy < scale; dy++) {
        int py = y + dy;
        if (py < 0 || py >= c.h) continue;
        for (int dx = 0; dx < scale; dx++) {
            int px = x + dx;
            if (px >= 0 && px < c.w) c.px[py * c.w + px] = argb;
        }
    }
}

void drawSprite(Canvas c, const Sprite& s, const uint32_t* palette, int scale, int x, int y) {
    if (!s.px) return;
    for (int j = 0; j < s.h; j++)
        for (int i = 0; i < s.w; i++)
            if (uint8_t v = s.px[j * s.w + i]) fill(c, x + i * scale, y + j * scale, scale, palette[v]);
}

void drawMono(Canvas c, const Sprite& s, uint32_t rgb, int scale, int x, int y) {
    if (!s.px) return;
    for (int j = 0; j < s.h; j++)
        for (int i = 0; i < s.w; i++)
            if (s.px[j * s.w + i]) fill(c, x + i * scale, y + j * scale, scale, rgb);
}

void drawPet(Canvas c, PetState state, cta::Accessory acc, const uint32_t colors[2], int scale, int x, int y) {
    uint32_t palette[kPaletteSize];
    for (size_t i = 0; i < kPaletteSize; i++) palette[i] = kPalette[i];
    palette[kSlotBody] = colors[0];
    palette[kSlotLight] = colors[1];
    drawSprite(c, kPets[(int)state], palette, scale, x, y);
    if (state == PetState::Asleep) return;
    const Overlay* o = acc == cta::Accessory::Umbrella ? &kOverlayUmbrella
                       : acc == cta::Accessory::Scarf  ? &kOverlayScarf
                                                       : nullptr;
    if (o) drawSprite(c, o->sprite, palette, scale, x, y + o->row * scale);
}

static const Sprite* glyph(char ch) {
    const Sprite* blank = nullptr;
    for (size_t i = 0; i < kFontCount; i++) {
        if (kFont[i].c == ch) return &kFont[i].sprite;
        if (kFont[i].c == ' ') blank = &kFont[i].sprite;
    }
    return blank;
}

int textWidth(const char* text, int scale) {
    int w = 0, n = 0;
    for (const char* p = text; *p; p++, n++) {
        const Sprite* g = glyph(*p);
        w += (g ? g->w : 0) + (n ? 1 : 0);
    }
    return w * scale;
}

void drawText(Canvas c, const char* text, uint32_t rgb, int scale, int x, int y) {
    for (const char* p = text; *p; p++) {
        const Sprite* g = glyph(*p);
        if (!g) continue;
        drawMono(c, *g, rgb, scale, x, y);
        x += (g->w + 1) * scale;
    }
}

const Sprite& weatherIcon(int c) {
    if ((c >= 71 && c <= 77) || c == 85 || c == 86) return kGlyphSnow;
    if (c <= 1) return kGlyphSun;
    return kGlyphCloud;
}

}  // namespace pet
