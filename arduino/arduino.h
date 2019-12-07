#pragma once

#ifndef AMBILIGHT_SERIAL_BAUD_RATE
#define AMBILIGHT_SERIAL_BAUD_RATE 1000000
#endif

#ifndef AMBILIGHT_SERIAL_CHUNK
// Minimal size of an update, in bytes. Should be strictly less than 63, else
// WriteFile to a serial device will hang forever for some reason. Also must
// be divisible by `sizeof(LED)` for all required strip types.
#define AMBILIGHT_SERIAL_CHUNK 60
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
// TODO move to runtime configuration
#define AMBILIGHT_USE_SPI 1
#endif

static_assert(AMBILIGHT_CHUNKS_PER_STRIP < 63, "exceeding protocol limitations");
static_assert(AMBILIGHT_SERIAL_CHUNK < 63, "chunk size too big");
static constexpr uint32_t MAX_LEDS = AMBILIGHT_SERIAL_CHUNK * AMBILIGHT_CHUNKS_PER_STRIP / 4;
