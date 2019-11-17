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
// So here's the thing: consider the top left corner of the quad, for example.
// Suppose it has coordinates (0, 0). This is in *continuous* space; however,
// both the screen and the texture are composed of dots. So each dot is mapped
// to the middle of each continuous square, e.g. the top left pixel at the
// rasterizer stage will actually have screen space coordinate (.5, .5).
//
// Now, the texture coordinates are linearly interpolated, so if we set texture
// coordinate (0, 0) for the upper left corner, the result for pixel at (.5, .5)
// at scale 1 is also (.5, .5), the texture is sampled precisely at that texel,
// and all is fine. However, imagine we scale the texture up at 2x. Then linear
// interpolation would give texcoord (.25, .25) at screen coord (.5, .5). If
// the texture is in fact in the middle of a texture atlas, this will cause
// the output to have random colors from adjacent textures mixed in, which
// is obviously bad.
//
// To fix this problem, we slightly adjust the texture coordinates so that
// screen space pixel (L + .5, T + .5), i.e. the top left filled pixel,
// *always* samples the texture at (tL + .5, tT + .5) (and similarly for the
// bottom right corner). To do that, consider a 1D slice of screen space:
//      L----X-------------Y----R,   where d(L, X) = d(Y, R) = 0.5
// which we want to linearly map to the texture space interval:
//          P---Q-------V---W,  where P and W are the points we're looking for
// such that X maps to Q and Y maps to V, and locations of Q and V we know because
//           tL-Q-------V-tR    d(tL, Q) = d(V, tR) = 0.5
// Since it's all linearly interpolated, d(L, X) / d(P, Q) = d(X, Y) / d(Q, V).
// This means d(P, Q) = d(Q, V) / d(X, Y) * d(L, X); together with P = tL
// + d(tL, Q) - d(P, Q) this gives us
//     P = tL + 0.5 - (d(tL, tR) - 1) / (d(L, R) - 1) * 0.5
// Similarly for W, and then for two points in the orthogonal direction.
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
