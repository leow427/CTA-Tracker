#pragma once
// The hash of one panel row of a strip (main.cpp sends only the rows whose hash changed). Plain C++, so
// tools/frame_test.cpp checks it on a laptop.
#include <stdint.h>

// FNV-1a over a panel row's 32-bit words (n pixels, n even), in two interleaved lanes so one lane's multiply runs
// while the other's waits. Each step of a lane is a bijection of its state, so a row that differs in one word always
// hashes differently, as with one lane; rows that differ in more collide with odds of about 2^-32.
template <int kWords>
inline uint32_t hashWords(const uint32_t* w) {
    uint32_t a = 2166136261u, b = 2166136261u ^ 0x9e3779b9u;
    for (int i = 0; i + 1 < kWords; i += 2) {
        a = (a ^ w[i]) * 16777619u;
        b = (b ^ w[i + 1]) * 16777619u;
    }
    if (kWords & 1) a = (a ^ w[kWords - 1]) * 16777619u;
    return a ^ (b << 16 | b >> 16);
}

// Unrolled for the device's strips: 48 rows, and 34 in the last (466 = 9 * 48 + 34).
inline uint32_t hashRow(const uint16_t* p, int n) {
    const uint32_t* w = reinterpret_cast<const uint32_t*>(p);
    if (n == 48) return hashWords<24>(w);
    if (n == 34) return hashWords<17>(w);
    uint32_t h = 2166136261u;
    for (int i = 0; i < n / 2; ++i) h = (h ^ w[i]) * 16777619u;
    return h;
}
