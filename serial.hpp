#pragma once

#include "arduino/arduino.h"
#include "color.hpp"
#include "dxui/span.hpp"
#include "dxui/winapi.hpp"

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
            for (uint8_t d = 255, i = (r > b ? r > g ? r : g : b > g ? b : g) / 256 + 1; i < 32; i++)
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
}

struct serial {
    serial(LPCWSTR path) {
        handle.reset(CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0));
        winapi::throwOnFalse(handle);
        DCB serialParams = {};
        serialParams.DCBlength = sizeof(serialParams);
        winapi::throwOnFalse(GetCommState(handle.get(), &serialParams));
        serialParams.BaudRate = AMBILIGHT_SERIAL_BAUD_RATE;
        serialParams.ByteSize = 8;
        serialParams.StopBits = ONESTOPBIT;
        serialParams.Parity = NOPARITY;
        serialParams.fBinary = 1;
        serialParams.fErrorChar = 0;
        serialParams.fOutX = 0;
        serialParams.fInX = 0;
        serialParams.fNull = 0;
        serialParams.fDtrControl = DTR_CONTROL_ENABLE;
        serialParams.fRtsControl = RTS_CONTROL_DISABLE;
        winapi::throwOnFalse(SetCommState(handle.get(), &serialParams));
        COMMTIMEOUTS serialTimeouts = {};
        serialTimeouts.ReadIntervalTimeout = 1000;
        serialTimeouts.ReadTotalTimeoutConstant = 1000;
        serialTimeouts.WriteTotalTimeoutConstant = 1000;
        winapi::throwOnFalse(SetCommTimeouts(handle.get(), &serialTimeouts));
        winapi::throwOnFalse(PurgeComm(handle.get(), PURGE_RXCLEAR | PURGE_TXCLEAR));
    }

    template <typename F /* FLOATX4(FLOATX4) */>
    void update(uint8_t strip, util::span<const FLOATX4> data, F&& transform) {
        for (size_t i = 0; i < data.size(); i++) {
            FLOATX4 asFloat = transform(data[i]);
            size_t chunk = i / ledsPerChunk;
            size_t place = i % ledsPerChunk;
            auto old = color[strip][chunk][place];
            color[strip][chunk][place] = {
                (uint16_t)asFloat.r, (uint16_t)asFloat.g, (uint16_t)asFloat.b,
                (uint16_t)(0.299f * asFloat.r + 0.587f * asFloat.g + 0.114f * asFloat.b)};
            valid[strip][chunk] &= !memcmp(&old, &color[strip][chunk][place], sizeof(old));
        }
    }

    void submit() {
        bool force = !write({'<', 'R', 'G', 'B', 'D', 'A', 'T', 'A'});
        for (size_t strip = 0; strip < 4; strip++) {
            for (size_t chunk = 0; chunk < AMBILIGHT_CHUNKS_PER_STRIP; chunk++) if (force || !valid[strip][chunk]) {
                uint8_t tmpb[AMBILIGHT_SERIAL_CHUNK + 1];
                tmpb[0] = (uint8_t)(strip + chunk * 4);
                memcpy(tmpb + 1, color[strip][chunk], sizeof(tmpb) - 1);
                write(tmpb);
                valid[strip][chunk] = true;
            }
        }
        write({AMBILIGHT_USE_SPI ? 254 : 255});
    }

private:
    bool write(util::span<const uint8_t> data) {
        BYTE response;
        DWORD result = (DWORD)data.size();
        winapi::throwOnFalse(WriteFile(handle.get(), &data[0], result, &result, nullptr) && result == data.size());
        winapi::throwOnFalse(ReadFile(handle.get(), &response, 1, &result, nullptr) && result == 1);
        return response == '>';
    }

private:
    using LED = std::conditional_t<AMBILIGHT_USE_SPI, Y5B8G8R8, G8R8B8>;
    static_assert(AMBILIGHT_SERIAL_CHUNK % sizeof(LED) == 0, "a chunk does not fit a whole number of LEDs");
    static constexpr size_t ledsPerChunk = AMBILIGHT_SERIAL_CHUNK / sizeof(LED);

    winapi::handle handle;
    LED  color[4][AMBILIGHT_CHUNKS_PER_STRIP][ledsPerChunk] = {};
    bool valid[4][AMBILIGHT_CHUNKS_PER_STRIP] = {};
};
