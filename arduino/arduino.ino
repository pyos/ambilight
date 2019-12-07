// A general Arduino program to control up to 4 strips of WS2812B-like or APA102-like
// LEDs each over the serial interface.
//
// The data consists of transactions, each a series of request-response pairs. The first
// request is always "<RGBDATA"; the response is ">" if there is a known last state, "<"
// if a complete frame is needed. After that, new LED data may follow in chunks of
// AMBILIGHT_SERIAL_CHUNK bytes, prefixed with a (chunk index * 4 + strip index) byte.
// The last request should be 254 (for SPI strips) or 255 (for WS281x strips), which
// refreshes all LEDs updated in this transaction. Requests starting with bytes 252 and
// 253 are reserved.
//
// Strips are driven in pairs (0+1 and 2+3), so the total refresh time depends on the
// maximum number of LEDs updated in the strips of each pair. For WS2812B-like LEDs,
// each pair of LEDs takes 27.9us to refresh.
//
// If incorrect serial data is received, or any data takes more than a second to arrive,
// the Arduino will blink its integrated LED. If no data at all (not even incorrect data
// is received for 4 seconds in a row, all strips will only display a dim red light on
// the first LED. To keep the same state, periodic empty transactions can be used.

#include "arduino.h"

#include "pins_arduino.h"

struct LEDStripPair {
  LEDStripPair(int pinA, int pinB, int clk)
    : port(&PORTA + digital_pin_to_port[pinA])
    , maskA(1 << digital_pin_to_bit_position[pinA])
    , maskB(1 << digital_pin_to_bit_position[pinB])
    , maskC(clk < 0 ? 0 : 1 << digital_pin_to_bit_position[clk])
  {
    // assert(digital_pin_to_port[pinA] == digital_pin_to_port[pinB]);
    // assert(digital_pin_to_port[pinA] == digital_pin_to_port[clk]);
    port->DIRSET = maskA | maskB | maskC;
    port->OUTCLR = maskA | maskB | maskC;
  }

  void show(const uint8_t* A, const uint8_t* B, size_t n, bool spi) {
    if (!n) return;
    if (spi) showSPI(A, B, n); else showTimed(A, B, n);
    endTime = micros() / 256;
  }

private:
  void showTimed(const uint8_t* A, const uint8_t* B, size_t n) {
    while ((uint16_t)(micros() / 256) == endTime);
    uint8_t a = 0, b = 0, z = 0, m = 8;
    __asm__ volatile (
      "cli"               "\n" // /--------------------- cycles (1 cycle = 50 ns)
    "%=0:"                "\n" // v   nextByte:                             ---+
      "ld %[a], %a[A]+"   "\n" // 2     uint8_t a = *A++;                      |
      "ld %[b], %a[B]+"   "\n" // 2     uint8_t b = *B++;                      | 250ns before byte
      "ldi %[m], 8"       "\n" // 1     m = 8;                                 |
    "%=1:"                "\n" //     nextBit:                              ---/
      "lsl %[a]"          "\n" // 1     bool setA = a & 0x80; a <<= 1;         | 250ns before bit
      "brcs %=2f"         "\n" // 1/2   if (!setA)                             | T1L = 500ns
      "or  %[z], %[mA]"   "\n" // 1/0     z |= mA;                             | T0L = 850ns
    "%=2:"                "\n" //                                              | (+ 100ns after byte)
      "st -%a[P], %[mAB]" "\n" // 2     port->OUTSET = mAB;                 ---/
      "inc %A[P]"         "\n" // 1                                            |
      "lsl %[b]"          "\n" // 1     bool setB = b & 0x80; b <<= 1;         |
      "brcs %=3f"         "\n" // 1/2   if (!setB)                             | T0H = 300ns
      "or  %[z], %[mB]"   "\n" // 1/0     z |= mB;                             |
    "%=3:"                "\n" //                                              |
      "st %a[P], %[z]"    "\n" // 2     port->OUTCLR = z;                   ---/
      "clr %[z]"          "\n" // 1     z = 0;                                 |
      "dec %[m]"          "\n" // 1     bool haveMoreBits = --m;               | T1H = T0H + 350ns = 650ns
      "brne %=4f"         "\n" // 1/    if (!haveMoreBits) {                   |
      "sbiw %[n], 1"      "\n" // 2       bool haveMoreBytes = --n;            |
      "st %a[P], %[mAB]"  "\n" // 2       port->OUTCLR = mAB;               ---/
      "brne %=0b"         "\n" // 2       if (haveMoreBytes) goto nextByte;    \-- up after 100ns
      "rjmp %=5f"         "\n" //         goto end; }
    "%=4:"                "\n" //  /2   else {                                 |
      "nop"               "\n" //   1                                          |
      "st %a[P], %[mAB]"  "\n" //   2     port->OUTCLR = mAB;               ---/
      "nop"               "\n" //   1     // Give the next bit some room.      |
      "nop"               "\n" //   1     // The LEDs reshape signals, so this |
      "nop"               "\n" //   1     // allows for some variation.        |
      "rjmp %=1b"         "\n" //   2     goto nextBit; }                      \-- up after 250ns
    "%=5:"                "\n" //     end:
      "sei"               "\n" // total 27.9us per pixel pair (100ns between bytes + 1150ns per bit) @ 20MHz
      : [a]  "+r"  (a)
      , [b]  "+r"  (b)
      , [z]  "+r"  (z)
      , [m]  "+a"  (m)
      , [A]  "+e"  (A)
      , [B]  "+e"  (B)
      , [n]  "+w"  (n)
      : [P]   "e"  (&port->OUTCLR)
      , [mA]  "la" (maskA)
      , [mB]  "la" (maskB)
      , [mAB] "la" (maskA | maskB)
    );
  }

  void showSPI(const uint8_t* A, const uint8_t* B, size_t n) {
    const uint8_t maskABC = maskA | maskB | maskC;
    // Start frame: 32 zeros
    // Data: 111xxxxxbbbbbbbbggggggggrrrrrrrr
    // End frame: max(32, n/2) zeros
    for (uint8_t i = 0; i < 32; i++) {
      port->OUTSET = maskC;
      port->OUTCLR = maskC;
    }
    for (size_t m = n; m--;) {
      uint8_t a = *A++, b = *B++;
      for (uint8_t i = 0; i < 8; i++) {
        uint8_t z = 0;
        if (a & 0x80) z |= maskA; a <<= 1;
        if (b & 0x80) z |= maskB; b <<= 1;
        port->OUTSET = z;
        port->OUTSET = maskC;
        port->OUTCLR = maskABC;
      }
    }
    for (size_t m = n > 64 ? n / 2 : 32; m--; ) {
      port->OUTSET = maskC;
      port->OUTCLR = maskC;
    }
  }

private:
  PORT_struct* port;
  uint8_t maskA;
  uint8_t maskB;
  uint8_t maskC;
  uint16_t endTime = 0;
};

// TODO allow strips of different kinds & select strip kind at runtime
static LEDStripPair strip01{ 9, 10,  5};
static LEDStripPair strip23{11, 12, 13};
static uint8_t data[4][AMBILIGHT_CHUNKS_PER_STRIP][AMBILIGHT_SERIAL_CHUNK];
static bool valid = false;

static void fallbackPattern() {
  memset(data, 0, sizeof(data));
  // data = {SPI 0/1, SPI 2/3, WS281x 0/1, WS281x 2/3}
  for (auto& chunk : data[0]) for (size_t i = 0; i < AMBILIGHT_SERIAL_CHUNK; i += 4) chunk[i] = 0xE0;
  for (auto& chunk : data[1]) for (size_t i = 0; i < AMBILIGHT_SERIAL_CHUNK; i += 4) chunk[i] = 0xE0;
  data[0][0][0] /* APA102 Y */ = 0xFF;
  data[1][0][3] /* APA102 R */ = data[3][0][1] /* WS2812B R */ = 10;
  // Show using SPI first because the timed data will be ignored by SPI strips.
  for (int i = 0; i < 4; i++) (i & 1 ? strip23 : strip01).show(data[i][0], data[i][0], sizeof(data[i]), i < 2);
  valid = false;
}

void setup() {
  fallbackPattern();
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(AMBILIGHT_SERIAL_BAUD_RATE);
  while (!Serial);
}

void loop() {
  for (uint8_t i = 1; !Serial.find("<RGBDATA"); i++)
    if (i == 4 /* seconds */)
      fallbackPattern();

  uint8_t index;
  uint8_t ns[] = {0, 0};
  while (Serial.write(valid ? '>' : '<') && Serial.readBytes(&index, 1) == 1) {
    valid = true;
    if (index == 254 || index == 255) {
      strip01.show(data[0][0], data[1][0], sizeof(data[0][0]) * ns[0], index == 254);
      strip23.show(data[2][0], data[3][0], sizeof(data[2][0]) * ns[1], index == 254);
      Serial.write('>');
      return;
    }

    uint8_t i = index % 4;
    uint8_t j = index / 4;
    if (j >= AMBILIGHT_CHUNKS_PER_STRIP || Serial.readBytes(data[i][j], sizeof(data[i][j])) != sizeof(data[i][j]))
      break;
    if (ns[i / 2] <= j)
      ns[i / 2] = j + 1;
  }
  digitalWrite(LED_BUILTIN, HIGH);
  delay(100);
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
  valid = false;
}
