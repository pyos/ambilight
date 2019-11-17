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
    {{L, B, Z}, {}, {tL, tB}}, {{L, T, Z}, {}, {tL, tT}}, {{R, B, Z}, {}, {tR, tB}}, \
    {{R, B, Z}, {}, {tR, tB}}, {{L, T, Z}, {}, {tL, tT}}, {{R, T, Z}, {}, {tR, tT}}

// Same as `QUAD`, but the texture rectangle is rotated 90 degrees clockwise.
//
// NOTE: a 180 degree rotation can be achieved by swapping tL <-> tR
//       and tT <-> tB in `QUAD`. Similar operation with `QUADR` produces
//       a 270 degree rotation.
//
#define QUADR(L, T, R, B, Z, tL, tT, tR, tB) \
    {{L, B, Z}, {}, {tR, tB}}, {{L, T, Z}, {}, {tL, tB}}, {{R, B, Z}, {}, {tR, tT}}, \
    {{R, B, Z}, {}, {tR, tT}}, {{L, T, Z}, {}, {tL, tB}}, {{R, T, Z}, {}, {tL, tT}}

// Same as `QUAD`, but all coordinates are in pixels. These are suitable
// for passing to `ui::dxcontext::draw`.
//
// Let's say we replace `tA` and `tB` with `x` and `y`. First pixel has coordinate `a + .5`.
// The texture position after linear interpolation will therefore be `x + .5 * (y - x) / (b - a)`;
// we want it to be `tA + .5`. Similarly, `b - .5` maps to `y - .5 * (y - x) / (b - a)`,
// which should become `tB - .5`.
//     tA + .5 = x + (y - x) / 2(b - a)
//     tB - .5 = y - (y - x) / 2(b - a)
//  => x = tA - (tB - tA - 1)/(b - a - 1)/2 + 1/2
//     y = tB + (tB - tA - 1)/(b - a - 1)/2 - 1/2
//
#define QUADP(L, T, R, B, Z, tL, tT, tR, tB) \
    QUAD((float)(L), (float)(T), (float)(R), (float)(B), Z, \
        (float)(tL) + .5f - .5f * (float)((tR) - (tL) - 1) / (float)((R) - (L) - 1), \
        (float)(tT) + .5f - .5f * (float)((tB) - (tT) - 1) / (float)((B) - (T) - 1), \
        (float)(tR) - .5f + .5f * (float)((tR) - (tL) - 1) / (float)((R) - (L) - 1), \
        (float)(tB) - .5f + .5f * (float)((tB) - (tT) - 1) / (float)((B) - (T) - 1))

// Same as `QUADR`, but all coordinates are in pixels. Or, same as `QUADP`,
// but the texture rectangle is rotated 90 degrees clockwise.
#define QUADPR(L, T, R, B, Z, tL, tT, tR, tB) \
    QUADR((float)(L), (float)(T), (float)(R), (float)(B), Z, \
        (float)(tL) + .5f - .5f * (float)((tR) - (tL) - 1) / (float)((B) - (T) - 1), \
        (float)(tT) + .5f - .5f * (float)((tB) - (tT) - 1) / (float)((R) - (L) - 1), \
        (float)(tR) - .5f + .5f * (float)((tR) - (tL) - 1) / (float)((B) - (T) - 1), \
        (float)(tB) - .5f + .5f * (float)((tB) - (tT) - 1) / (float)((R) - (L) - 1))

// Convert an A8R8G8B8 color into R32G32B32A32 used by the shaders.
#define ARGB2CLR(u) \
    (DirectX::XMFLOAT4{ \
        (uint8_t)((u) >> 16) / 255.f, (uint8_t)((u) >> 8)  / 255.f, \
        (uint8_t)((u))       / 255.f, (uint8_t)((u) >> 24) / 255.f, \
    })

namespace ui {
    // The zero rectangle:
    //     rectUnion(EMPTY_RECT, x) == x
    //     rectIntersection(EMPTY_RECT, x) == EMPTY_RECT
    static constexpr RECT EMPTY_RECT = {LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN};

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

    static constexpr RECT moveRectTo(RECT r, POINT p) {
        return {p.x, p.y, p.x + (r.right - r.left), p.y + (r.bottom - r.top)};
    }

    struct vertex {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT4 clr;
        DirectX::XMFLOAT2 tex;
    };

    struct dxcontext {
        dxcontext();

        winapi::com_ptr<ID3D11Device> raw() { return device; }

        // Copy a rectangle from one texture to another without any processing.
        void copy(ID3D11Texture2D* target, ID3D11Texture2D* source, RECT from, POINT to) {
            D3D11_BOX box = {(UINT)from.left, (UINT)from.top, 0, (UINT)from.right, (UINT)from.bottom, 1};
            context->CopySubresourceRegion(target, 0, to.x, to.y, 0, source, 0, &box);
        }

        void copy(ID3D11Texture2D* target, ID3D11Texture2D* source, RECT from) {
            copy(target, source, from, {from.left, from.top});
        }

        // Clear a region of a texture, setting all pixels in it to the specified ARGB color.
        void clear(ID3D11Texture2D* target, RECT, uint32_t color = 0);

        // Draw a textured object onto a surface. Vertex coordinates must be given in pixels;
        // calling this function will transform them into UV.
        void draw(ID3D11Texture2D* target, ID3D11Texture2D* source, util::span<vertex> vertices, RECT cull,
                  ID3D11PixelShader* shader = nullptr);

        // Convert raw BGRA data into a texture. Size must be a multiple of `4w`.
        winapi::com_ptr<ID3D11Texture2D> textureFromRaw(util::span<const uint8_t>, size_t w, bool mipmaps = false);

        // Convert a PNG image into a BGRA 32-bit texture.
        winapi::com_ptr<ID3D11Texture2D> textureFromPNG(util::span<const uint8_t>, bool mipmaps = false);

        void regenerateMipMaps(ID3D11Texture2D* texture);

        template <typename T, typename F /* = winapi::com_ptr<T>() */>
        winapi::com_ptr<T> cached(uintptr_t key, F&& f) {
            auto [it, added] = cache.emplace(key, winapi::com_ptr<IUnknown>());
            return (added ? (it->second = f().reinterpret<IUnknown>()) : it->second).reinterpret<T>();
        }

        template <typename T, winapi::com_ptr<T> (*F)(dxcontext&)>
        winapi::com_ptr<T> cached() {
            return cached<T>((uintptr_t)F, [this]{ return F(*this); });
        }

    private:
        winapi::com_ptr<ID3D11Buffer> buffer(util::span<const uint8_t> contents, int bindFlags) {
            D3D11_BUFFER_DESC bufferDesc = {};
            bufferDesc.Usage = D3D11_USAGE_DEFAULT;
            bufferDesc.ByteWidth = (UINT)contents.size();
            bufferDesc.BindFlags = bindFlags;
            D3D11_SUBRESOURCE_DATA initData = {contents.data(), 0, 0};
            return COMe(ID3D11Buffer, device->CreateBuffer, &bufferDesc, &initData);
        }

    private:
        winapi::com_ptr<ID3D11Device> device;
        winapi::com_ptr<ID3D11DeviceContext> context;
        winapi::com_ptr<ID3D11VertexShader> vertexId;
        winapi::com_ptr<ID3D11PixelShader> pixelId;
        winapi::com_ptr<ID3D11InputLayout> inputLayout;
        winapi::com_ptr<ID3D11SamplerState> linear;
        winapi::com_ptr<ID3D11BlendState> blendOver;
        winapi::com_ptr<ID3D11BlendState> blendClear;
        winapi::com_ptr<ID3D11RasterizerState> rState;
        std::unordered_map<uintptr_t, winapi::com_ptr<IUnknown>> cache;
    };
}
