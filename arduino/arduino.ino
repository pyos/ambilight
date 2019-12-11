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
    uint8_t a, b, m, z = 0;
    __asm__ volatile (
      "cli"                 "\n" // /--------------------- cycles (1 cycle = 50 ns)
    "%=0:"                  "\n" // v   nextByte:                             ---+
      "ld %[a], %a[A]+"     "\n" // 2     uint8_t a = *A++;                      |
      "ld %[b], %a[B]+"     "\n" // 2     uint8_t b = *B++;                      |
      "ldi %[m], 8"         "\n" // 1     m = 8;                                 |
    "%=1:"                  "\n" //     nextBit:                                 +-- +250ns to first TxL in byte
      "sbrs %[a], 7"        "\n" // 1/2   if (!(a & 0x80))                       |
      "or  %[z], %[mA]"     "\n" // 1/0     z |= mA;                             |
      "lsl %[a]"            "\n" // 1     a <<= 1;                               |
      "std %a[P]+5, %[mAB]" "\n" // 2     port->OUTSET = mAB;                 ---/ T1L = 500ns; T0L = 850ns; +100ns if first bit
      "sbrs %[b], 7"        "\n" // 1/2   if (!(b & 0x80))                       |
      "or  %[z], %[mB]"     "\n" // 1/0     z |= mB;                             |
      "lsl %[b]"            "\n" // 1     b <<= 1;                               |
      "nop"                 "\n" // 1                                            |
      "std %a[P]+6, %[z]"   "\n" // 2     port->OUTCLR = z;                   ---/ T0H = 300ns
      "clr %[z]"            "\n" // 1     z = 0;                                 |
      "dec %[m]"            "\n" // 1     bool haveMoreBits = --m;               |
      "brne %=2f"           "\n" // 1/    if (!haveMoreBits) {                   |
      "sbiw %[n], 1"        "\n" // 2       bool haveMoreBytes = --n;            |
      "std %a[P]+6, %[mAB]" "\n" // 2       port->OUTCLR = mAB;               ---/ T1H = T0H + 350ns = 650ns
      "brne %=0b"           "\n" // 2       if (haveMoreBytes) goto nextByte;    \-- +100ns to next TxL
      "rjmp %=3f"           "\n" //         goto end; }
    "%=2:"                  "\n" //  /2   else {                                 |
      "nop"                 "\n" //   1                                          |
      "std %a[P]+6, %[mAB]" "\n" //   2     port->OUTCLR = mAB;               ---/
      "nop"                 "\n" //   1     // Give the next bit some room.      |
      "nop"                 "\n" //   1     // The LEDs reshape signals, so this |
      "nop"                 "\n" //   1     // allows for some variation.        |
      "rjmp %=1b"           "\n" //   2     goto nextBit; }                      \-- +250ns to next TxL
    "%=3:"                  "\n" //     end:
      "sei"                 "\n" // total 27.9us per pixel pair (100ns between bytes + 1150ns per bit) @ 20MHz
      : [a]  "=r"  (a)
      , [b]  "=r"  (b)
      , [m]  "=a"  (m)
      , [z]  "+r"  (z)
      , [A]  "+e"  (A)
      , [B]  "+e"  (B)
      , [n]  "+w"  (n)
      : [P]   "b"  (port)
      , [mA]  "la" (maskA)
      , [mB]  "la" (maskB)
      , [mAB] "la" (maskA | maskB)
    );
  }

  void showSPI(const uint8_t* A, const uint8_t* B, size_t n) {
    // `w` is current state, `z` is delta for next state. The start frame is 32 zeros.
    uint8_t a, b, m = 32, z, w = 0;
    do port->OUTSET = maskC,
       port->OUTCLR = maskC; while (--m);
    // The end frame is 32 zeros for SK9822, n/8 zeros (i.e. one toggle per LED)
    // for APA102. Redundant zeros are ignored, so just write whichever is bigger.
    size_t c = n > 256 ? n / 8 : 32;
    __asm__( // avr-gcc tends to generate garbage code
    "%=0:"                   "\n" // do {
      "ld   %[a], %a[A]+"    "\n" //   a = *A++;
      "ld   %[b], %a[B]+"    "\n" //   b = *B++;
      "ldi  %[m], 8"         "\n" //   m = 8;
    "%=1:"                   "\n" //   do {
      "mov  %[z], %[w]"      "\n" //     z = w;
      "sbrc %[a], 7"         "\n" //     if (a & 0x80)
      "eor  %[z], %[mA]"     "\n" //       z ^= maskA;
      "sbrc %[b], 7"         "\n" //     if (b & 0x80)
      "eor  %[z], %[mB]"     "\n" //       z ^= maskB;
      "std  %a[P]+7, %[z]"   "\n" //     port->OUTTGL = z;
      "eor  %[w], %[z]"      "\n" //     w ^= z;  // do this between port writes to let the data line
      "lsl  %[a]"            "\n" //     a <<= 1; // voltage stabilize before toggling the clock line
      "lsl  %[b]"            "\n" //     b <<= 1;
      "or   %[w], %[mC]"     "\n" //     w |= maskC;
      "std  %a[P]+5, %[mC]"  "\n" //     port->OUTSET = maskC;
      "dec  %[m]"            "\n" //   } while (--m);
      "brne %=1b"            "\n" //
      "sbiw %[n], 1"         "\n" // } while (--n);
      "brne %=0b"            "\n" // total 27.2us per pixel pair (400ns between bytes + 800ns per bit @ 20MHz)
      : [a]  "=r"  (a)
      , [b]  "=r"  (b)
      , [m]  "=a"  (m)
      , [z]  "=r"  (z)
      , [w]  "+r"  (w)
      , [A]  "+e"  (A)
      , [B]  "+e"  (B)
      , [n]  "+w"  (n)
      : [P]   "b"  (port)
      , [mA]  "la" (maskA)
      , [mB]  "la" (maskB)
      , [mC]  "la" (maskC)
    );
    port->OUTCLR = w;
    do port->OUTSET = maskC,
       port->OUTCLR = maskC; while (--c);
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
  data[1][0][0] /* APA102 Y */ = 0xFF;
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
