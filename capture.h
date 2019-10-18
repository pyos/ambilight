#pragma once

#include <memory>
#include <stdint.h>

#include "dxui/span.hpp"

struct IAudioCapturer {
    virtual ~IAudioCapturer() = default;
    // Capture a new sample from an audio device, blocking for up to the specified
    // number of milliseconds. Return an array of amplitudes averaged by octave,
    // or an empty span if there aren't enough samples yet.
    //
    // TODO define the range of amplitudes.
    virtual util::span<const float> next(uint32_t timeout = 200) = 0;
};

struct IVideoCapturer {
    virtual ~IVideoCapturer() = default;
    // Capture a new frame, blocking for up to the specified number of milliseconds.
    // Return the frame as a span of width * height ARGB pixels. If nothing has changed
    // since the last call, return an empty span instead.
    virtual util::span<const uint32_t> next(uint32_t timeout = 500) = 0;
};

// Create a new video capturer that takes an image of the screen, downscales it
// to the specified size, then blurs it a bit.
std::unique_ptr<IVideoCapturer> captureScreen(uint32_t id, uint32_t w, uint32_t h);

// Create a new audio capturer that uses a WASAPI loopback on a default output device.
std::unique_ptr<IAudioCapturer> captureDefaultAudioOutput();
