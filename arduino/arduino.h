#pragma once

#ifndef AMBILIGHT_SERIAL_BAUD_RATE
#define AMBILIGHT_SERIAL_BAUD_RATE 1000000
#endif

#ifndef AMBILIGHT_SERIAL_CHUNK
// Minimal size of an update, in LEDs. The reason for 20 is that 1. a byte of
// overhead per 60 bytes of data is good; 2. 60 byte granularity for
// single-pixel updates is fine; 3. if you send 64 bytes or more, WriteFile
// freezes forever without sending even one of them for some reason.
#define AMBILIGHT_SERIAL_CHUNK 20
#endif

#ifndef AMBILIGHT_CHUNKS_PER_STRIP
// Due to protocol limitations, there can be no more than 255/4=63 chunks per strip.
// 63*4 gives 252, meaning indices 252..254 are reserved for further extensions.
// Setting this lower is necessary if the Arduino does not have so much RAM (and it
// probably doesn't).
#define AMBILIGHT_CHUNKS_PER_STRIP 6
#endif

#ifndef AMBILIGHT_USE_SPI
// Set to 1 for SK9822 strips, or 0 for WS2815 strips.
#define AMBILIGHT_USE_SPI 0
#endif

static_assert(AMBILIGHT_CHUNKS_PER_STRIP < 63, "exceeding protocol limitations");

#include <stdint.h>

namespace {
    struct LED {
        LED() : LED(0, 0, 0) {}
        bool operator==(const LED& x) const { return R == x.R && G == x.G && B == x.B; }
        bool operator!=(const LED& x) const { return R != x.R || G != x.G || B != x.B; }

#if AMBILIGHT_USE_SPI
        static constexpr int scale = 8191;

        uint8_t Z, B, G, R;

        LED(int r, int g, int b) {
            // Divide all channels by some number from 1 to 32 that minimizes the rounding error
            // while keeping the result below 256. This feature is the major advantage of APA102.
            // TODO round to gray, not down; maybe figure out a faster way to compute this
            for (int d = INT_MAX, i = (r > b ? r > g ? r : g : b > g ? b : g) / 256 + 1; i < 33; i++) {
                int rd = r / i, gd = g / i, bd = b / i,
                    rm = r % i, gm = g % i, bm = b % i;
                if (d > rm + gm + bm)
                    d = rm + gm + bm, Z = 0xE0 | (i - 1), R = rd, G = gd, B = bd;
            }
        }
#else
        static constexpr int scale = 255;

        uint8_t G, R, B;

        LED(int r, int g, int b) : G((uint8_t)g), R((uint8_t)r), B((uint8_t)b) {}
#endif
    };
}
