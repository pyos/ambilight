#include "capture.h"
#include "kiss_fft.h"
#include "defer.hpp"
#include "dxui/winapi.hpp"

#include <Audioclient.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <atomic>
#include <vector>


// See `captureVideo.cpp`.
static HRESULT AudioDeviceExpectedErrors[] = {
    AUDCLNT_E_DEVICE_INVALIDATED,
    AUDCLNT_E_DEVICE_IN_USE,
    AUDCLNT_E_SERVICE_NOT_RUNNING,
    AUDCLNT_E_BUFFER_OPERATION_PENDING,
    S_OK
};

// The minimal frequency resolved by DFT.
#define DFT_RESOLUTION 25

// The number of DFT runs per unit of resolution (i.e. the update frequency
// is the product of this and DFT_RESOLUTION).
#define DFT_RUNS_PER_FILL 4

// EWMA coefficients for merging consecutive updates. Greater = smoother.
#define DFT_EWMA_RISE 0.50f
#define DFT_EWMA_DROP 0.96f

// When the default device for the specified flow and role is changed, set an atomic and fire an event.
// This is a COM object, meaning it should be referenced through `util::intrusive_ptr`.
struct AudioDeviceChangeListener : IMMNotificationClient {
    AudioDeviceChangeListener(winapi::com_ptr<IMMDeviceEnumerator> enumerator, EDataFlow flow, ERole role, HANDLE event)
        : enumerator(enumerator)
        , expectedFlow(flow)
        , expectedRole(role)
        , event(event)
    {
        winapi::throwOnFalse(enumerator->RegisterEndpointNotificationCallback(this));
    }

    ~AudioDeviceChangeListener() {
        enumerator->UnregisterEndpointNotificationCallback(this);
    }

    AudioDeviceChangeListener(const AudioDeviceChangeListener&) = delete;
    AudioDeviceChangeListener& operator=(const AudioDeviceChangeListener&) = delete;

    bool changed() const {
        return triggered;
    }

    HRESULT OnDeviceStateChanged(LPCWSTR device, DWORD state) override { return S_OK; }
    HRESULT OnDeviceAdded(LPCWSTR device) override { return S_OK; }
    HRESULT OnDeviceRemoved(LPCWSTR device) override { return S_OK; }
    HRESULT OnPropertyValueChanged(LPCWSTR device, const PROPERTYKEY) override { return S_OK; }
    HRESULT OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR device) override {
        if (flow == expectedFlow && role == expectedRole)
            if (!triggered.exchange(true) && event)
                SetEvent(event);
        return S_OK;
    }

    HRESULT QueryInterface(const IID& iid, void** out) override {
        return iid != __uuidof(IUnknown) && iid != __uuidof(IMMNotificationClient) ? E_NOINTERFACE
                : !out ? E_POINTER
                : (*out = this, S_OK);
    }

    ULONG AddRef() override {
        return ++refcount;
    }

    ULONG Release() override {
        auto n = --refcount;
        if (!n) delete this;
        return n;
    }

private:
    winapi::com_ptr<IMMDeviceEnumerator> enumerator;
    std::atomic<bool> triggered{false};
    std::atomic<ULONG> refcount{1};
    EDataFlow expectedFlow;
    ERole expectedRole;
    HANDLE event;
};

// Converts a binary audio sample into a floating-point number from 0 to 1
// according to the device's wave format.
using AudioSampleReader = float(BYTE*);

static AudioSampleReader* makeAudioSampleReader(WAVEFORMATEXTENSIBLE* pwfx) {
    if (pwfx->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) switch (pwfx->Format.wBitsPerSample) {
        case 32: return [](BYTE* at) -> float { return *(float*)at; };
        case 64: return [](BYTE* at) -> float { return *(double*)at; };
    }
    if (pwfx->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) switch (pwfx->Format.wBitsPerSample) {
        // TODO there's `pwfx->Samples.wValidBitsPerSample` for scaling
        case 8:  return [](BYTE* at) -> float { return *(INT8*)at / 256.; };
        case 16: return [](BYTE* at) -> float { return *(INT16*)at / (256. * 256.); };
        case 24: return [](BYTE* at) -> float { return (*(INT32*)at & 0xFFFFFF) / (256. * 256. * 256.); };
        case 32: return [](BYTE* at) -> float { return *(INT32*)at / (256. * 256. * 256. * 256.); };
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

struct close_handle {
    void operator()(HANDLE h) const {
        CloseHandle(h);
    }
};

struct audio_client_stop {
    void operator()(IAudioClient* client) {
        client->Stop();
    }
};

struct AudioOutputCapturer : IAudioCapturer {
    AudioOutputCapturer()
        : readyEvent(CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS))
    {
        winapi::throwOnFalse(readyEvent);

        auto enumerator = COMv(IMMDeviceEnumerator, CoCreateInstance, __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL);
        // TODO AudioDeviceExpectedErrors
        auto device = COMe(IMMDevice, enumerator->GetDefaultAudioEndpoint, eRender, eConsole);
        audioClient = COM(IAudioClient, void, device->Activate, __uuidof(IAudioClient), CLSCTX_ALL, nullptr);
        *&deviceChangeListener = new AudioDeviceChangeListener(enumerator, eRender, eConsole, readyEvent.get());

        WAVEFORMATEX* formatPtr = nullptr;
        // TODO AudioDeviceExpectedErrors
        winapi::throwOnFalse(audioClient->GetMixFormat(&formatPtr));
        DEFER { CoTaskMemFree(formatPtr); };
        format = *formatPtr;
        reader = makeAudioSampleReader((WAVEFORMATEXTENSIBLE*)formatPtr);

        // FFT works fastest when the sample count is a product of powers of 2, 3, and 5.
        size_t n = kiss_fftr_next_fast_size_real(format.nSamplesPerSec / DFT_RESOLUTION);
        samplesL.resize(n);
        samplesR.resize(n);
        // Output range: [0, DFT_RESOLUTION, DFT_RESOLUTION*2, ..., Nyquist frequency].
        fft.reset(kiss_fftr_alloc(n, 0, nullptr, nullptr));
        fftBuffer.resize(n / 2 + 1);
        // Discard 0 Hz, group the rest into octaves. First half is left channel, second half is right channel.
        mapped.resize(log2fp1(n / 2) * 2);

        // TODO AudioDeviceExpectedErrors
        winapi::throwOnFalse(audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, formatPtr, NULL));
        winapi::throwOnFalse(audioClient->SetEventHandle(readyEvent.get()));
        winapi::throwOnFalse(audioClient->Start());
        audioClientStopper.reset(audioClient);
        captureClient = COMi(IAudioCaptureClient, audioClient->GetService);
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
            if (deviceChangeListener->changed())
                throw std::exception();
            // TODO AudioDeviceExpectedErrors
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
        auto shift = samplesL.size() / DFT_RUNS_PER_FILL;
        for (UINT i = 0; frames--; i += format.nBlockAlign) {
            samplesL[nextSample] = data ? reader(&data[i]) : 0;
            samplesR[nextSample] = data ? reader(&data[i + second]) : 0;
            if (++nextSample != samplesL.size())
                continue; // Not enough for a DFT run yet.
            if (data) {
                size_t half = mapped.size() / 2;
                mapTimeToLogFreq(samplesL, {mapped.data(), half});
                mapTimeToLogFreq(samplesR, {mapped.data() + half, half});
                haveUpdates = true;
            } else if (std::any_of(mapped.begin(), mapped.end(), [](float c) { return c > 1e-5; })) {
                for (auto& c : mapped) c *= DFT_EWMA_DROP;
                haveUpdates = true;
            }
            if (nextSample -= shift) {
                std::copy(samplesL.begin() + shift, samplesL.end(), samplesL.begin());
                std::copy(samplesR.begin() + shift, samplesR.end(), samplesR.begin());
            }
        }
        return haveUpdates;
    }

    void mapTimeToLogFreq(util::span<const kiss_fft_scalar> in, util::span<float> out) {
        // assert(out.size() < in.size());
        kiss_fftr(fft.get(), in.data(), fftBuffer.data());
        for (size_t i = 1, j = 0; j < out.size(); j++) {
            double m = 0;
            for (size_t q = 1 << j, d = 0; q-- && i < fftBuffer.size(); i++)
                m += (sqrt(fftBuffer[i].r * fftBuffer[i].r + fftBuffer[i].i * fftBuffer[i].i) - m) / ++d;
            out[j] = (out[j] - m) * (m > out[j] ? DFT_EWMA_RISE : DFT_EWMA_DROP) + m;
        }
    }

private:
    std::unique_ptr<std::remove_pointer_t<HANDLE>, close_handle> readyEvent;
    winapi::com_ptr<IAudioClient> audioClient;
    winapi::com_ptr<IAudioCaptureClient> captureClient;
    winapi::com_ptr<AudioDeviceChangeListener> deviceChangeListener;
    std::unique_ptr<IAudioClient, audio_client_stop> audioClientStopper;
    std::unique_ptr<kiss_fftr_state, fft_release> fft;
    std::vector<kiss_fft_scalar> samplesL;
    std::vector<kiss_fft_scalar> samplesR;
    std::vector<kiss_fft_cpx> fftBuffer;
    std::vector<float> mapped;
    AudioSampleReader* reader = nullptr;
    WAVEFORMATEX format;
    UINT nextSample = 0;
};

std::unique_ptr<IAudioCapturer> captureDefaultAudioOutput() {
    return std::make_unique<AudioOutputCapturer>();
}