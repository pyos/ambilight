#pragma once

#ifndef AMBILIGHT_SERIAL_BAUD_RATE
#define AMBILIGHT_SERIAL_BAUD_RATE 1000000
#endif

#ifndef AMBILIGHT_SERIAL_CHUNK
// Minimal size of an update, in LEDs. Should be strictly less than 63 divided
// by sizeof(LED), else WriteFile to a serial device will hang forever for some
// unknown reason.
#define AMBILIGHT_SERIAL_CHUNK 15
#endif

#ifndef AMBILIGHT_CHUNKS_PER_STRIP
// Due to protocol limitations, there can be no more than 255/4=63 chunks per strip.
// 63*4 gives 252, meaning indices 252..254 are reserved for further extensions.
// Setting this lower is necessary if the Arduino does not have so much RAM (and it
// probably doesn't).
#define AMBILIGHT_CHUNKS_PER_STRIP 10
#endif

#ifndef AMBILIGHT_USE_SPI
// Set to 1 for SK9822 strips, or 0 for WS2815 strips.
#define AMBILIGHT_USE_SPI 1
#endif

#include <stdint.h>

namespace {
    struct Y5B8G8R8 /* APA102-like */ {
        uint8_t Z, B, G, R;

        Y5B8G8R8() : Z(0xE0), B(0), G(0), R(0) {}
        Y5B8G8R8(uint16_t r, uint16_t g, uint16_t b, uint16_t /*y*/) {
            auto m31d32 = [](uint16_t x) { return (x / 256 * 248) + (x % 256 * 31 / 32); };
            // APA102 has an effective 13 bit dynamic range thanks to its 5-bit global brightness
            // field. A lot of pairs in 1..32 are coprime, so picking the smallest possible
            // brightness is not always best; others may allow for colors closer to the desired one.
            r = m31d32(r) / 8, g = m31d32(g) / 8, b = m31d32(b) / 8;
            for (int d = 128, i = (r > b ? r > g ? r : g : b > g ? b : g) / 256 + 1; i < 32; i++)
                if (d > r % i + g % i + b % i)
                    d = r % i + g % i + b % i, Z = i;
            R = r / Z, G = g / Z, B = b / Z, Z |= 0xE0;
        }
    };

    struct G8R8B8 /* WS2812-like */ {
        uint8_t G, R, B;

        G8R8B8() : G(0), R(0), B(0) {}
        G8R8B8(uint16_t r, uint16_t g, uint16_t b, uint16_t y)
            // Rounding each component separately can magnify relative differences due to errors,
            // e.g. 16, 17, 16 (barely greenish dark gray) at gamma 2.0 and brightness 70% will
            // become 0, 1, 0 (obvious dark green), so round Cr/Cg/Cb instead.
            : G(((g - y + 128) >> 8) + ((y + 128) >> 8))
            , R(((r - y + 128) >> 8) + ((y + 128) >> 8))
            , B(((b - y + 128) >> 8) + ((y + 128) >> 8))
        {}
    };

#if AMBILIGHT_USE_SPI
    using LED = Y5B8G8R8;
#else
    using LED = G8R8B8;
#endif
}

static_assert(AMBILIGHT_CHUNKS_PER_STRIP < 63, "exceeding protocol limitations");
static_assert(AMBILIGHT_SERIAL_CHUNK * sizeof(LED) < 63, "chunk size too big");
