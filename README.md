Screen bias lighting. Real original, I know, I know.

Requirements
------------

 * Windows 10
 * Arduino Nano Every (yes, that one specifically due to its different I/O scheme)
 * At least 4 WS2812B, WS2813, WS2815, APA102, or SK9822 LEDs.

Features
--------

 * Video capture using the DirectX Desktop Duplication API - works in fullscreen games, super fast thanks
   to GPU acceleration.
 * An additional strip that pulses in rhythm to music.
 * Supports WS281x and APA102/SK9822 strips.
 * Nice UI, I guess?

How to
------

 1. Configure the Arduino IDE to flash the Nano Every to 20MHz mode:
    in boards.txt, set `nona4809.build.f_cpu=20000000L` and `nona4809.bootloader.OSCCFG=0x02`.
 2. Connect strips to the Arduino:
   * pin 9 = control for bottom and left edges, starting from rightmost LED on the bottom;
   * pin 10 = control for right and top edges, starting from lowest LED on the right;
   * pin 11 = control for left half of the extra strip, starting from center;
   * pin 12 = control for right half of the extra strip, starting from center;
   * (APA102/SK9822 only) pin 5 = clock for 9 and 10;
   * (APA102/SK9822 only) pin 13 = clock for 11 and 12.
 3. Build (`msbuild /p:Configuration=Release`) and run the software, follow the initial setup.

Troubleshooting
---------------

**No lights**: check the wiring, especially that there is in fact enough power for LEDs.

**Weird lights**: check the wiring, especially that the ground is shared and that the voltage is stable. If using WS2812 or similar, make sure the Arduino is in 20MHz mode (16MHz would probably break the timings).

**Red lights in the middle of the extra strip, nothing otherwise**: check connection to PC; try changing the serial port number.
