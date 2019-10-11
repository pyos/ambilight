//
// A general Arduino program to control up to 4 strips of up to AMBILIGHT_CHUNKS_PER_STRIP
// times AMBILIGHT_SERIAL_CHUNK (default = 120) WS2815 LEDs each over the serial interface.
//
// Serial protocol:
//    R    <RGBDATA    chunk    chunk   ...
//    S            >        >        >  ...
// `chunk` begins with a single byte:
//    * If it is 255, then the strips are refreshed (the response is only sent after
//      this is done) and the exchange ends. The next exchange should start with <RGBDATA
//      again.
//    * Otherwise, it is (chunk index * 4 + strip index) and should be followed by
//      AMBILIGHT_SERIAL_CHUNK * sizeof(LED) bytes of data. Note that 252, 253 and 254
//      are never valid start bytes regardless of AMBILIGHT_CHUNKS_PER_STRIP.
//
// Strips are driven in pairs (0+1 and 2+3), so total refresh time is 27.9us times
// [the maximum number of updated LEDs in strips 0 and 1 rounded up to chunk size,
// plus the maximum number of updated LEDs in strips 2 and 3 rounded up to chunk size].
// For example, if first 25 LEDs of strip 0, 30 LEDs of strip 1, and 10 LEDs of strip 2
// are updated during a single exhange, the refresh time with default chunk size will be
// (40 + 20) * 27.9us = 1.674ms.
//
// NOTE  If incorrect serial data is received, or any data takes more than a second to arrive,
//       the Arduino will blink its integrated LED.
//       If no data at all (not even incorrect data) is received for 4 seconds in a row, all
//       strips will be turned off and strips 2 and 3 will display a dim red light on the first LEDs.
//

#include "arduino.h"

#include "pins_arduino.h"

struct WS2815Pair {
  WS2815Pair(int pinA, int pinB)
    : port(&PORTA + digital_pin_to_port[pinA])
    , maskA(1 << digital_pin_to_bit_position[pinA])
    , maskB(1 << digital_pin_to_bit_position[pinB])
  {
    // assert(port == digital_pin_to_port[pin2]);
    port->DIRSET = maskA | maskB;
    port->OUTCLR = maskA | maskB;
  }

  void show(const byte* A, const byte* B, size_t n) {
    if (!n)
      return;
    // Truncating `micros()` is fine; at worst, this causes a redundant
    // delay once in a while.
    while ((uint16_t)micros() - endTime < 200);
    byte a = 0, b = 0, z = 0, m = 8;
    __asm__ volatile (
      "cli"               "\n" // /--------------------- cycles (1 cycle = 50 ns)
    "%=0:"                "\n" // v   nextByte:                             ---+
      "ld %[a], %a[A]+"   "\n" // 2     byte a = *A++;                         |
      "ld %[b], %a[B]+"   "\n" // 2     byte b = *B++;                         | 250ns before byte
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
    endTime = micros();
  }

private:
  PORT_struct* port;
  uint8_t maskA;
  uint8_t maskB;
  uint16_t endTime = 0;
};

static constexpr size_t CHUNK_BYTE_SIZE = AMBILIGHT_SERIAL_CHUNK * sizeof(LED);
static LED data[4][AMBILIGHT_CHUNKS_PER_STRIP][AMBILIGHT_SERIAL_CHUNK];
static WS2815Pair strip01{9, 10};
static WS2815Pair strip23{11, 12};

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(AMBILIGHT_SERIAL_BAUD_RATE);
  while (!Serial);
}

void loop() {
  for (byte i = 0; !Serial.find("<RGBDATA"); i++) {
    if (i % 4 /* seconds */ == 0) {
      memset(data, 0, sizeof(data));
      // TODO there's 256 bytes in EEPROM, maybe store a simple default pattern there?
      data[2][0][0] = data[3][0][0] = LED(10, 0, 0);
      strip01.show((byte*)data[0], (byte*)data[1], CHUNK_BYTE_SIZE * AMBILIGHT_CHUNKS_PER_STRIP);
      strip23.show((byte*)data[2], (byte*)data[3], CHUNK_BYTE_SIZE * AMBILIGHT_CHUNKS_PER_STRIP);
    }
  }
  byte index;
  byte ns[] = {0, 0};
  while (Serial.write('>') && Serial.readBytes(&index, 1) == 1) {
    if (index == 255) {
      if (ns[0]) strip01.show((byte*)data[0], (byte*)data[1], CHUNK_BYTE_SIZE * ns[0]);
      if (ns[1]) strip23.show((byte*)data[2], (byte*)data[3], CHUNK_BYTE_SIZE * ns[1]);
      Serial.write('>');
      return;
    }

    byte i = index % 4;
    byte j = index / 4;
    if (j >= AMBILIGHT_CHUNKS_PER_STRIP || Serial.readBytes((byte*)data[i][j], CHUNK_BYTE_SIZE) != CHUNK_BYTE_SIZE)
      break;
    if (ns[i / 2] <= j)
      ns[i / 2] = j + 1;
  }
  digitalWrite(LED_BUILTIN, HIGH);
  delay(100);
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
}
