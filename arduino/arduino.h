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

static_assert(AMBILIGHT_CHUNKS_PER_STRIP < 63, "exceeding protocol limitations");

#include <stdint.h>

namespace {
    struct LED {
        uint8_t G, R, B; // In native format.

        LED() : LED(0, 0, 0) {}
        LED(uint8_t r, uint8_t g, uint8_t b) : G(g), R(r), B(b) {} // Convert here.

        bool operator==(const LED& x) const { return R == x.R && G == x.G && B == x.B; }
        bool operator!=(const LED& x) const { return R != x.R || G != x.G || B != x.B; }
    };
}
