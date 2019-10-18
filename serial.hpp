#pragma once

#include "arduino/arduino.h"
#include "dxui/span.hpp"
#include "dxui/winapi.hpp"

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

    template <typename F /* LED(uint32_t) */>
    void update(uint8_t strip, util::span<const UINT> data, F&& transform) {
        for (size_t i = 0; i < data.size(); i++) {
            LED n = transform(data[i]);
            size_t chunk = i / AMBILIGHT_SERIAL_CHUNK;
            size_t place = i % AMBILIGHT_SERIAL_CHUNK;
            valid[strip][chunk] &= n == color[strip][chunk][place];
            color[strip][chunk][place] = n;
        }
    }

    void submit() {
        write({'<', 'R', 'G', 'B', 'D', 'A', 'T', 'A'});
        for (size_t strip = 0; strip < 4; strip++) {
            for (size_t chunk = 0; chunk < AMBILIGHT_CHUNKS_PER_STRIP; chunk++) if (!valid[strip][chunk]) {
                uint8_t tmpb[AMBILIGHT_SERIAL_CHUNK * sizeof(LED) + 1];
                tmpb[0] = (uint8_t)(strip + chunk * 4);
                memcpy(tmpb + 1, color[strip][chunk], sizeof(tmpb) - 1);
                write(tmpb);
                valid[strip][chunk] = true;
            }
        }
        write({255});
    }

private:
    void write(util::span<const uint8_t> data) {
        BYTE response;
        DWORD result = (DWORD)data.size();
        winapi::throwOnFalse(WriteFile(handle.get(), &data[0], result, &result, nullptr) && result == data.size());
        winapi::throwOnFalse(ReadFile(handle.get(), &response, 1, &result, nullptr) && result == 1 && response == '>');
    }

private:
    ui::handle handle;
    LED  color[4][AMBILIGHT_CHUNKS_PER_STRIP][AMBILIGHT_SERIAL_CHUNK] = {};
    bool valid[4][AMBILIGHT_CHUNKS_PER_STRIP] = {};
};
