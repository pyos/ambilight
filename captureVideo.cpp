#include "capture.h"
#include "defer.hpp"
#include "dxui/draw.hpp"

#include <numeric>
#include <vector>

struct ScreenCapturer : IVideoCapturer {
    ScreenCapturer(UINT output, UINT w, UINT h)
        : collected(w * h)
    {
        auto dxgiDevice = COMi(IDXGIDevice, res.raw()->QueryInterface);
        auto dxgiAdapter = COMi(IDXGIAdapter, dxgiDevice->GetParent);
        auto dxgiOutput = COMe(IDXGIOutput, dxgiAdapter->EnumOutputs, output);
        auto dxgiOutput1 = COMi(IDXGIOutput1, dxgiOutput->QueryInterface);
        display = COMe(IDXGIOutputDuplication, dxgiOutput1->DuplicateOutput, res.raw());
        DXGI_OUTPUT_DESC displayDesc;
        dxgiOutput->GetDesc(&displayDesc);
        // All four directions are decomposable into a combination of a 90 degree clockwise
        // rotation and a flip of both axes.
        mirror = displayDesc.Rotation == DXGI_MODE_ROTATION_ROTATE180
              || displayDesc.Rotation == DXGI_MODE_ROTATION_ROTATE270;
        rotate = displayDesc.Rotation == DXGI_MODE_ROTATION_ROTATE90
              || displayDesc.Rotation == DXGI_MODE_ROTATION_ROTATE270;

        D3D11_TEXTURE2D_DESC targetDesc = {};
        targetDesc.Width = displayDesc.DesktopCoordinates.right - displayDesc.DesktopCoordinates.left;
        targetDesc.Height = displayDesc.DesktopCoordinates.bottom - displayDesc.DesktopCoordinates.top;
        if (rotate)
            // DesktopCoordinates refer to the place in the entire multi-display grid, so they
            // are already rotated. We want the original texture dimensions here.
            std::swap(targetDesc.Width, targetDesc.Height);
        targetDesc.MipLevels = 0;
        targetDesc.ArraySize = 1;
        targetDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        targetDesc.SampleDesc.Count = 1;
        targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        targetDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        complete = COMe(ID3D11Texture2D, res.raw()->CreateTexture2D, &targetDesc, nullptr);

        targetDesc.Width = w;
        targetDesc.Height = h;
        targetDesc.MipLevels = 1;
        targetDesc.MiscFlags = 0;
        rescaled1 = COMe(ID3D11Texture2D, res.raw()->CreateTexture2D, &targetDesc, nullptr);
        rescaled2 = COMe(ID3D11Texture2D, res.raw()->CreateTexture2D, &targetDesc, nullptr);

        targetDesc.Usage = D3D11_USAGE_STAGING;
        targetDesc.BindFlags = 0;
        targetDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        cpuReadTarget = COMe(ID3D11Texture2D, res.raw()->CreateTexture2D, &targetDesc, nullptr);
    }

    util::span<const UINT> next(UINT timeout) override {
        bool needUpdate = !needRelease;
        if (needRelease) {
            needRelease = false;
            winapi::throwOnFalse(display->ReleaseFrame());
        }

        DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
        winapi::com_ptr<IDXGIResource> resource;
        auto hr = display->AcquireNextFrame(timeout, &frameInfo, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT)
            return {};
        winapi::throwOnFalse(hr);
        needRelease = true;
        if (!frameInfo.TotalMetadataBufferSize)
            return {}; // No changes from last frame. (Probably moved the pointer.)
        auto frame = COMi(ID3D11Texture2D, resource->QueryInterface);
        metadata.resize(frameInfo.TotalMetadataBufferSize);

        D3D11_TEXTURE2D_DESC originalDesc;
        D3D11_TEXTURE2D_DESC rescaledDesc;
        frame->GetDesc(&originalDesc);
        rescaled1->GetDesc(&rescaledDesc);
        const RECT src = {0, 0, (LONG)originalDesc.Width, (LONG)originalDesc.Height};
        const RECT dst = {0, 0, (LONG)rescaledDesc.Width, (LONG)rescaledDesc.Height};
        // These two values should normally be the same, but may differ if the display aspect ratio
        // is not the same as the LED rectangle aspect ratio.
        const LONG rx = 10 * src.right / (rotate ? dst.bottom : dst.right);
        const LONG ry = 10 * src.bottom / (rotate ? dst.right : dst.bottom);
        // The most expensive operation is `GenerateMips`, which cannot be clipped to update
        // a specific region only. At least we can discard the update entirely if it is
        // in the middle of the screen.
        for (const auto& move : meta<false>())
            needUpdate |= rx >= move.DestinationRect.left || rx + move.DestinationRect.right  >= src.right
                       || ry >= move.DestinationRect.top  || ry + move.DestinationRect.bottom >= src.bottom;
        for (const auto& dirty : meta<true>())
            needUpdate |= rx >= dirty.left || rx + dirty.right  >= src.right
                       || ry >= dirty.top  || ry + dirty.bottom >= src.bottom;
        if (!needUpdate)
            return {};

        // Can't subsample from `frame` because it probably doesn't have `D3D11_RESOURCE_MISC_GENERATE_MIPS`.
        res.copy(complete, frame, src);
        ui::vertex vs[] = {
            QUADP(0, 0, dst.right, dst.bottom, 0, 0, 0, src.right, src.bottom),
            QUADP(dst.right, dst.bottom, 0, 0, 0, 0, 0, src.right, src.bottom),
            QUADPR(0, 0, dst.right, dst.bottom, 0, 0, 0, src.right, src.bottom),
            QUADPR(dst.right, dst.bottom, 0, 0, 0, 0, 0, src.right, src.bottom),
        };
        ui::vertex vs2[] = {QUADP(0, 0, dst.right, dst.bottom, 0, 0, 0, dst.right, dst.bottom)};
        ui::vertex vs3[] = {QUADP(0, 0, dst.right, dst.bottom, 0, 0, 0, dst.right, dst.bottom)};
        for (auto& w : vs3) w.blw = 1;
        res.regenerateMipMaps(complete);
        res.draw(rescaled1, complete, {&vs[6 * (rotate * 2 + mirror)], 6}, dst);
        res.draw(rescaled2, rescaled1, vs2, dst, ui::dxcontext::blur);
        res.draw(rescaled1, rescaled2, vs3, dst, ui::dxcontext::blur);
        res.copy(cpuReadTarget, rescaled1, dst);

        auto readSurface = COMi(IDXGISurface, cpuReadTarget->QueryInterface);
        DXGI_MAPPED_RECT mapped;
        winapi::throwOnFalse(readSurface->Map(&mapped, DXGI_MAP_READ));
        DEFER { readSurface->Unmap(); };
        for (UINT y = 0; y < rescaledDesc.Height; y++)
            memcpy(&collected[y * rescaledDesc.Width], &mapped.pBits[y * mapped.Pitch], rescaledDesc.Width * sizeof(UINT));
        return collected;
    }

private:
    template <bool dirty>
    util::span<std::conditional_t<dirty, RECT, DXGI_OUTDUPL_MOVE_RECT>> meta() {
        auto data = reinterpret_cast<std::conditional_t<dirty, RECT, DXGI_OUTDUPL_MOVE_RECT>*>(metadata.data());
        UINT size = (UINT)metadata.size();
        if constexpr (dirty)
            winapi::throwOnFalse(display->GetFrameDirtyRects(size, data, &size));
        else
            winapi::throwOnFalse(display->GetFrameMoveRects(size, data, &size));
        return {data, size / sizeof(data[0])};
    }

private:
    ui::dxcontext res;
    winapi::com_ptr<ID3D11Texture2D> moveTemporary;
    winapi::com_ptr<ID3D11Texture2D> complete;
    winapi::com_ptr<ID3D11Texture2D> rescaled1;
    winapi::com_ptr<ID3D11Texture2D> rescaled2;
    winapi::com_ptr<ID3D11Texture2D> cpuReadTarget;
    winapi::com_ptr<IDXGIOutputDuplication> display;
    std::vector<BYTE> metadata;
    std::vector<UINT> collected;
    bool mirror = false;
    bool rotate = false;
    bool needRelease = false;
};

std::unique_ptr<IVideoCapturer> captureScreen(uint32_t id, uint32_t w, uint32_t h) {
    return std::make_unique<ScreenCapturer>(id, w, h);
}
