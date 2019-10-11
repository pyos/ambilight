#pragma once

#include <algorithm>
#include <unordered_map>

#include "winapi.hpp"
#include "span.hpp"

#include <d3d11.h>
#include <directxmath.h>
#include <dxgi1_2.h>

// Construct a quad, i.e. a rectangle composed of two triangles.
// Whether the coordinates are in UV or pixels depends on what
// you're going to do with it. Usage:
//
//     ui::vertex vs[] = {QUAD(...)};
//
#define QUAD(L, T, R, B, Z, tL, tT, tR, tB) \
    {{L, B, Z}, {}, {tL, tB}, 1.f}, {{L, T, Z}, {}, {tL, tT}, 1.f}, {{R, B, Z}, {}, {tR, tB}, 1.f}, \
    {{R, B, Z}, {}, {tR, tB}, 1.f}, {{L, T, Z}, {}, {tL, tT}, 1.f}, {{R, T, Z}, {}, {tR, tT}, 1.f}

// Same as `QUAD`, but the texture rectangle is rotated 90 degrees clockwise.
//
// NOTE: a 180 degree rotation can be achieved by swapping tL <-> tR
//       and tT <-> tB in `QUAD`. Similar operation with `QUADR` produces
//       a 270 degree rotation.
//
#define QUADR(L, T, R, B, Z, tL, tT, tR, tB) \
    {{L, B, Z}, {}, {tR, tB}, 1.f}, {{L, T, Z}, {}, {tL, tB}, 1.f}, {{R, B, Z}, {}, {tR, tT}, 1.f}, \
    {{R, B, Z}, {}, {tR, tT}, 1.f}, {{L, T, Z}, {}, {tL, tB}, 1.f}, {{R, T, Z}, {}, {tL, tT}, 1.f}

// Same as `QUAD`, but all coordinates are in pixels. These are suitable
// for passing to `ui::dxcontext::draw`.
#define QUADP(L, T, R, B, Z, tL, tT, tR, tB) \
    QUAD((float)(L), (float)(T), (float)(R), (float)(B), Z, (float)(tL)+.5f, (float)(tT)+.5f, (float)(tR)-.5f, (float)(tB)-.5f)

// Same as `QUADR`, but all coordinates are in pixels. Or, same as `QUADP`,
// but the texture rectangle is rotated 90 degrees clockwise.
#define QUADPR(L, T, R, B, Z, tL, tT, tR, tB) \
    QUADR((float)(L), (float)(T), (float)(R), (float)(B), Z, (float)(tL)+.5f, (float)(tT)+.5f, (float)(tR)-.5f, (float)(tB)-.5f)

// Convert an A8R8G8B8 color into R32G32B32A32 used by the shaders.
#define ARGB2CLR(u) \
    (DirectX::XMFLOAT4{ \
        (uint8_t)((u) >> 16) / 255.f, (uint8_t)((u) >> 8)  / 255.f, \
        (uint8_t)((u))       / 255.f, (uint8_t)((u) >> 24) / 255.f, \
    })

namespace ui {
    // The identity rectangle:
    //     rectUnion(RECT_ID, x) == x
    //     rectIntersection(RECT_ID, x) == x
    static constexpr RECT RECT_ID = {LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN};

    // Test if a rectangle is non-empty.
    static constexpr bool rectValid(RECT r) {
        return r.left < r.right && r.top < r.bottom;
    }

    // Test if a point falls into a rectangle.
    static constexpr bool rectHit(RECT r, POINT p) {
        return r.left <= p.x && p.x < r.right && r.top <= p.y && p.y < r.bottom;
    }

    // Test if a point falls into a rectangle bounded by (0, 0) and another point.
    static constexpr bool rectHit(POINT r, POINT p) {
        return rectHit({0, 0, r.x, r.y}, p);
    }

    // Compute a union of bounding rectangles.
    static constexpr RECT rectUnion(RECT a, RECT b) {
        return {std::min(a.left, b.left), std::min(a.top, b.top),
                std::max(a.right, b.right), std::max(a.bottom, b.bottom)};
    }

    // Compute an intersection of bounding rectangles.
    static constexpr RECT rectIntersection(RECT a, RECT b) {
        return {std::max(a.left, b.left), std::max(a.top, b.top),
                std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
    }

    static constexpr bool isSubRect(RECT a, RECT b) {
        return a.left >= b.left && a.top >= b.top && a.right <= b.right && a.bottom <= b.bottom;
    }

    struct vertex {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT4 clr;
        DirectX::XMFLOAT2 tex;
        float             blw;
    };

    struct dxcontext {
        dxcontext();

        ID3D11Device* raw() {
            return device.get();
        }

        // Copy a rectangle from one texture to another without any processing.
        void copy(ID3D11Texture2D* target, ID3D11Texture2D* source, RECT from, POINT to = {0, 0}) {
            D3D11_BOX box = {(UINT)from.left, (UINT)from.top, 0, (UINT)from.right, (UINT)from.bottom, 1};
            context->CopySubresourceRegion(target, to.x, to.y, 0, 0, source, 0, &box);
        }

        // Clear a region of a texture, setting all pixels in it to the specified ARGB color.
        void clear(ID3D11Texture2D* target, RECT, uint32_t color = 0);

        // Draw a textured object onto a surface. Vertex coordinates must be given in pixels;
        // calling this function will transform them into UV.
        void draw(ID3D11Texture2D* target, ID3D11Texture2D* source, util::span<vertex> vertices, RECT cull,
                  bool distanceCoded = false);

        // Convert a PNG image into a BGRA 32-bit texture.
        winapi::com_ptr<ID3D11Texture2D> textureFromPNG(util::span<const uint8_t>);

        template <typename F /* = winapi::com_ptr<ID3D11Texture2D>() */>
        winapi::com_ptr<ID3D11Texture2D> cachedTexture(uintptr_t key, F&& gen) {
            auto [it, added] = texCache.emplace(key, winapi::com_ptr<ID3D11Texture2D>{});
            return added ? (it->second = gen()) : it->second;
        }

        winapi::com_ptr<ID3D11Texture2D> invalidateTexture(uintptr_t key) {
            texCache.erase(key);
        }

        template <winapi::com_ptr<ID3D11Texture2D> (*F)(dxcontext&)>
        winapi::com_ptr<ID3D11Texture2D> cachedTexture() {
            return cachedTexture((uintptr_t)F, [this]{ return F(*this); });
        }

    private:
        winapi::com_ptr<ID3D11Buffer> buffer(util::span<const uint8_t> contents, int bindFlags) {
            D3D11_BUFFER_DESC bufferDesc = {};
            bufferDesc.Usage = D3D11_USAGE_DEFAULT;
            bufferDesc.ByteWidth = contents.size();
            bufferDesc.BindFlags = bindFlags;
            D3D11_SUBRESOURCE_DATA initData = {contents.data(), 0, 0};
            return COMe(ID3D11Buffer, device->CreateBuffer, &bufferDesc, &initData);
        }

    private:
        winapi::com_ptr<ID3D11Device> device;
        winapi::com_ptr<ID3D11DeviceContext> context;
        winapi::com_ptr<ID3D11VertexShader> vertexId;
        winapi::com_ptr<ID3D11PixelShader> pixelId;
        winapi::com_ptr<ID3D11PixelShader> colorDistance;
        winapi::com_ptr<ID3D11InputLayout> inputLayout;
        winapi::com_ptr<ID3D11SamplerState> linear;
        winapi::com_ptr<ID3D11BlendState> blendOver;
        winapi::com_ptr<ID3D11BlendState> blendClear;
        winapi::com_ptr<ID3D11RasterizerState> rState;
        std::unordered_map<uintptr_t, winapi::com_ptr<ID3D11Texture2D>> texCache;
    };
}
