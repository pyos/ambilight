#include "capture.h"
#include "kiss_fft.h"
#include "defer.hpp"
#include "dxui/winapi.hpp"

#include <Audioclient.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <atomic>
#include <vector>

// The minimal frequency resolved by DFT.
#define DFT_RESOLUTION 25

// The number of DFT runs per unit of resolution (i.e. the update frequency
// is the product of this and DFT_RESOLUTION).
#define DFT_RUNS_PER_FILL 4

// EWMA coefficients for merging consecutive updates. Greater = smoother.
#define DFT_EWMA_RISE 0.50f
#define DFT_EWMA_DROP 0.96f

// Converts a binary audio sample into a floating-point number from 0 to 1
// according to the device's wave format.
using AudioSampleReader = float(BYTE*);

static AudioSampleReader* makeAudioSampleReader(WAVEFORMATEXTENSIBLE* pwfx) {
    if (pwfx->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) switch (pwfx->Format.wBitsPerSample) {
        case 32: return [](BYTE* at) -> float { return *(float*)at; };
        case 64: return [](BYTE* at) -> float { return (float)*(double*)at; };
    }
    if (pwfx->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) switch (pwfx->Format.wBitsPerSample) {
        // TODO there's `pwfx->Samples.wValidBitsPerSample` for scaling
        case 8:  return [](BYTE* at) -> float { return *(INT8*)at / 256.f; };
        case 16: return [](BYTE* at) -> float { return *(INT16*)at / (256.f * 256.f); };
        case 24: return [](BYTE* at) -> float { return (*(INT32*)at & 0xFFFFFF) / (256.f * 256.f * 256.f); };
        case 32: return [](BYTE* at) -> float { return *(INT32*)at / (256.f * 256.f * 256.f * 256.f); };
    }
    // TODO A-law or mu-law format, probably.
    return [](BYTE*) { return 0.0f; };
}

// Compute floor(log2(x)) + 1, i.e. the number of octaves in a range of x uniformly
// distributed frequencies.
static size_t log2fp1(DWORD i) {
    return _BitScanReverse(&i, i) ? i + 1 : 0;
}

struct fft_release {
    void operator()(kiss_fftr_state* p) const {
        kiss_fftr_free(p);
    }
};

struct AudioOutputCapturer : IAudioCapturer, private IMMNotificationClient {
    AudioOutputCapturer() {
        enumerator = COMv(IMMDeviceEnumerator, CoCreateInstance, __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL);
        auto device = COMe(IMMDevice, enumerator->GetDefaultAudioEndpoint, eRender, eConsole);
        audioClient = COM(IAudioClient, void, device->Activate, __uuidof(IAudioClient), CLSCTX_ALL, nullptr);

        WAVEFORMATEX* formatPtr = nullptr;
        winapi::throwOnFalse(audioClient->GetMixFormat(&formatPtr));
        DEFER { CoTaskMemFree(formatPtr); };
        format = *formatPtr;
        reader = makeAudioSampleReader((WAVEFORMATEXTENSIBLE*)formatPtr);

        // FFT works fastest when the sample count is a product of powers of 2, 3, and 5.
        size_t n = kiss_fftr_next_fast_size_real(format.nSamplesPerSec / DFT_RESOLUTION);
        for (auto& sv : samples)
            sv.resize(n);
        // Output range: [0, DFT_RESOLUTION, DFT_RESOLUTION*2, ..., Nyquist frequency].
        fft.reset(kiss_fftr_alloc(n, 0, nullptr, nullptr));
        fftBuffer.resize(n / 2 + 1);
        // Discard 0 Hz, group the rest into octaves. First half is left channel, second half is right channel.
        mapped.resize(log2fp1(n / 2) * ARRAYSIZE(samples));

        winapi::throwOnFalse(audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, formatPtr, NULL));
        winapi::throwOnFalse(audioClient->SetEventHandle(readyEvent.get()));
        winapi::throwOnFalse(audioClient->Start());
        captureClient = COMi(IAudioCaptureClient, audioClient->GetService);
        winapi::throwOnFalse(enumerator->RegisterEndpointNotificationCallback(this));
    }

    ~AudioOutputCapturer() {
        enumerator->UnregisterEndpointNotificationCallback(this);
        audioClient->Stop();
    }

    util::span<const float> next(uint32_t timeout) override {
        BYTE* data;
        UINT32 frames;
        DWORD flags;
        bool haveUpdates = false;
        if (WaitForSingleObject(readyEvent.get(), timeout) == WAIT_TIMEOUT) {
            // Nothing is rendering to the stream, so insert an appropriate amount of silence.
            haveUpdates |= handleSound(nullptr, format.nSamplesPerSec * timeout / 1000);
        } else do {
            winapi::throwOnFalse(deviceChanged ? AUDCLNT_E_DEVICE_INVALIDATED : S_OK);
            winapi::throwOnFalse(captureClient->GetBuffer(&data, &frames, &flags, NULL, NULL));
            DEFER { captureClient->ReleaseBuffer(frames); };
            haveUpdates |= handleSound(flags & AUDCLNT_BUFFERFLAGS_SILENT ? nullptr : data, frames);
        } while (frames != 0);
        return haveUpdates ? mapped : util::span<const float>{};
    }

private:
    bool handleSound(BYTE* data, UINT32 frames) {
        bool haveUpdates = false;
        auto second = format.nChannels > 1 ? format.wBitsPerSample / 8 : 0;
        auto shift = samples[0].size() / DFT_RUNS_PER_FILL;
        for (size_t i = 0; frames--; i += format.nBlockAlign) {
            samples[0][nextSample] = data ? reader(&data[i]) : 0;
            samples[1][nextSample] = data ? reader(&data[i + second]) : 0;
            if (++nextSample != samples[0].size())
                continue; // Not enough for a DFT run yet.
            if (data) {
                size_t part = mapped.size() / ARRAYSIZE(samples);
                for (size_t j = 0; j < ARRAYSIZE(samples); j++)
                    mapTimeToLogFreq(samples[j], {&mapped[j * part], part});
                haveUpdates = true;
            } else if (std::any_of(mapped.begin(), mapped.end(), [](float c) { return c > 1e-5; })) {
                for (auto& c : mapped) c *= DFT_EWMA_DROP;
                haveUpdates = true;
            }
            if (nextSample -= shift)
                for (auto& sv : samples)
                    std::copy(sv.begin() + shift, sv.end(), sv.begin());
        }
        return haveUpdates;
    }

    void mapTimeToLogFreq(util::span<const kiss_fft_scalar> in, util::span<float> out) {
        // assert(out.size() < in.size());
        kiss_fftr(fft.get(), in.data(), fftBuffer.data());
        for (size_t i = 1, j = 0; j < out.size(); j++) {
            float m = 0;
            for (size_t q = 1 << j, d = 0; q-- && i < fftBuffer.size(); i++)
                m += (sqrtf(fftBuffer[i].r * fftBuffer[i].r + fftBuffer[i].i * fftBuffer[i].i) - m) / ++d;
            out[j] = (out[j] - m) * (m > out[j] ? DFT_EWMA_RISE : DFT_EWMA_DROP) + m;
        }
    }

    HRESULT OnDeviceStateChanged(LPCWSTR device, DWORD state) override { return S_OK; }
    HRESULT OnDeviceAdded(LPCWSTR device) override { return S_OK; }
    HRESULT OnDeviceRemoved(LPCWSTR device) override { return S_OK; }
    HRESULT OnPropertyValueChanged(LPCWSTR device, const PROPERTYKEY) override { return S_OK; }
    HRESULT OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR device) override {
        if (flow == eRender && role == eConsole)
            if (!deviceChanged.exchange(true))
                SetEvent(readyEvent.get());
        return S_OK;
    }

    HRESULT QueryInterface(const IID& iid, void** out) override { return E_NOINTERFACE; }
    ULONG AddRef() override { return 1; }
    ULONG Release() override { return 1; }

private:
    winapi::handle readyEvent{winapi::throwOnFalse(CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS))};
    winapi::com_ptr<IMMDeviceEnumerator> enumerator;
    winapi::com_ptr<IAudioClient> audioClient;
    winapi::com_ptr<IAudioCaptureClient> captureClient;
    std::unique_ptr<kiss_fftr_state, fft_release> fft;
    std::vector<kiss_fft_scalar> samples[2];
    std::vector<kiss_fft_cpx> fftBuffer;
    std::vector<float> mapped;
    std::atomic<bool> deviceChanged{false};
    AudioSampleReader* reader = nullptr;
    WAVEFORMATEX format;
    UINT nextSample = 0;
};

std::unique_ptr<IAudioCapturer> captureDefaultAudioOutput() {
    return std::make_unique<AudioOutputCapturer>();
}
