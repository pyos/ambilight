#include "draw.hpp"

#include "shlwapi.h"
#include "wincodec.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include <dxui/shaders/id_vertex.h>
#include <dxui/shaders/id_pixel.h>

ui::dxcontext::dxcontext() {
    D3D_FEATURE_LEVEL level;
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
    };
    HRESULT hr = S_OK;
    for (auto driverType : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        hr = D3D11CreateDevice(nullptr, driverType, nullptr, 0, levels, ARRAYSIZE(levels),
                                D3D11_SDK_VERSION, &device, &level, &context);
        if (SUCCEEDED(hr))
            break;
    }
    winapi::throwOnFalse(hr);

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION",    0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",       0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD",    0, DXGI_FORMAT_R32G32_FLOAT,       0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    vertexId = COMe(ID3D11VertexShader, device->CreateVertexShader, g_id_vertex, ARRAYSIZE(g_id_vertex), nullptr);
    pixelId = COMe(ID3D11PixelShader, device->CreatePixelShader, g_id_pixel, ARRAYSIZE(g_id_pixel), nullptr);
    inputLayout = COMe(ID3D11InputLayout, device->CreateInputLayout, layout, ARRAYSIZE(layout), g_id_vertex, ARRAYSIZE(g_id_vertex));
    context->IASetInputLayout(inputLayout);

    D3D11_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MinLOD = 0;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    linear = COMe(ID3D11SamplerState, device->CreateSamplerState, &sampler);

    D3D11_BLEND_DESC blend;
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    blendOver = COMe(ID3D11BlendState, device->CreateBlendState, &blend);

    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendClear = COMe(ID3D11BlendState, device->CreateBlendState, &blend);

    D3D11_RASTERIZER_DESC rdesc = {};
    rdesc.FillMode = D3D11_FILL_SOLID;
    rdesc.CullMode = D3D11_CULL_BACK;
    rdesc.DepthClipEnable = true;
    rdesc.ScissorEnable = true;
    rState = COMe(ID3D11RasterizerState, device->CreateRasterizerState, &rdesc);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertexId, nullptr, 0);
    context->PSSetSamplers(0, 1, &linear);
    context->RSSetState(rState);
}

static winapi::com_ptr<ID3D11Texture2D> nullTexture(ui::dxcontext& ctx) {
    return ctx.textureFromRaw({0, 0, 0, 0}, 1);
}

void ui::dxcontext::clear(ID3D11Texture2D* target, RECT area, uint32_t color) {
    ui::vertex quad[] = {QUAD(-1, +1, +1, -1, 0, 0, 0, 0, 0)};
    for (auto& vertex : quad)
        vertex.clr = ARGB2CLR(color);
    auto tx = cached<ID3D11Texture2D, nullTexture>();
    auto sr = COMe(ID3D11ShaderResourceView, device->CreateShaderResourceView, tx, nullptr);
    auto rt = COMe(ID3D11RenderTargetView, device->CreateRenderTargetView, target, nullptr);
    auto vb = buffer(util::span<ui::vertex>(quad).reinterpret<const uint8_t>(), D3D11_BIND_VERTEX_BUFFER);
    UINT stride = sizeof(vertex);
    UINT offset = 0;
    D3D11_TEXTURE2D_DESC targetDesc;
    target->GetDesc(&targetDesc);
    D3D11_VIEWPORT vp = {0, 0, (FLOAT)targetDesc.Width, (FLOAT)targetDesc.Height, 0, 1};
    context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context->RSSetViewports(1, &vp);
    context->RSSetScissorRects(1, &area);
    context->PSSetShader(pixelId, nullptr, 0);
    context->PSSetShaderResources(0, 1, &sr);
    context->OMSetBlendState(blendClear, nullptr, 0xFFFFFFFFU);
    context->OMSetRenderTargets(1, &rt, nullptr);
    context->Draw(6, 0);
}

void ui::dxcontext::regenerateMipMaps(ID3D11Texture2D* texture) {
    context->GenerateMips(COMe(ID3D11ShaderResourceView, device->CreateShaderResourceView, texture, nullptr));
}

void ui::dxcontext::draw(ID3D11Texture2D* target, ID3D11Texture2D* source, util::span<ui::vertex> vertices, RECT cull,
                         ID3D11PixelShader* shader) {
    D3D11_TEXTURE2D_DESC sourceDesc;
    D3D11_TEXTURE2D_DESC targetDesc;
    source->GetDesc(&sourceDesc);
    target->GetDesc(&targetDesc);
    for (auto& v : vertices) {
        v.pos.x = 2 * v.pos.x / targetDesc.Width - 1;
        v.pos.y = 1 - 2 * v.pos.y / targetDesc.Height;
        v.tex.x = v.tex.x / sourceDesc.Width;
        v.tex.y = v.tex.y / sourceDesc.Height;
    }
    auto sr = COMe(ID3D11ShaderResourceView, device->CreateShaderResourceView, source, nullptr);
    auto rt = COMe(ID3D11RenderTargetView, device->CreateRenderTargetView, target, nullptr);
    auto vb = buffer(vertices.reinterpret<const uint8_t>(), D3D11_BIND_VERTEX_BUFFER);
    UINT stride = sizeof(vertex);
    UINT offset = 0;
    D3D11_VIEWPORT vp = {0, 0, (FLOAT)targetDesc.Width, (FLOAT)targetDesc.Height, 0, 1};
    context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context->RSSetViewports(1, &vp);
    context->RSSetScissorRects(1, &cull);
    context->PSSetShader(shader ? shader : pixelId.get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, &sr);
    context->OMSetBlendState(blendOver, nullptr, 0xFFFFFFFFU);
    context->OMSetRenderTargets(1, &rt, nullptr);
    context->Draw((UINT)vertices.size(), 0);
}

winapi::com_ptr<ID3D11Texture2D> ui::dxcontext::textureFromPNG(util::span<const uint8_t> in, bool mipmaps) {
    winapi::com_ptr<IStream> stream;
    *&stream = SHCreateMemStream(in.data(), (UINT)in.size());
    auto dec = COMv(IWICBitmapDecoder, CoCreateInstance, CLSID_WICPngDecoder, nullptr, CLSCTX_INPROC_SERVER);
    winapi::throwOnFalse(dec->Initialize(stream, WICDecodeMetadataCacheOnLoad));
    auto raw = COMe(IWICBitmapFrameDecode, dec->GetFrame, 0);
    auto cnv = COMv(IWICFormatConverter, CoCreateInstance, CLSID_WICDefaultFormatConverter, nullptr, CLSCTX_INPROC_SERVER);
    winapi::throwOnFalse(cnv->Initialize(raw, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                         nullptr, 0, WICBitmapPaletteTypeFixedBW));
    UINT width = 0, height = 0;
    winapi::throwOnFalse(cnv->GetSize(&width, &height));
    std::vector<uint8_t> tmp(width * height * 4);
    winapi::throwOnFalse(cnv->CopyPixels(nullptr, width * 4, width * height * 4, tmp.data()));
    return textureFromRaw(tmp, width, mipmaps);
}

winapi::com_ptr<ID3D11Texture2D> ui::dxcontext::textureFromRaw(util::span<const uint8_t> in, size_t w, bool mipmaps) {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = (UINT)w;
    desc.Height = (UINT)(in.size() / w / 4);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = in.data();
    initData.SysMemPitch = (UINT)(w * 4);
    auto initial = COMe(ID3D11Texture2D, device->CreateTexture2D, &desc, &initData);
    if (!mipmaps)
        return initial;
    desc.MipLevels = 0;
    desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
    auto mipmapped = COMe(ID3D11Texture2D, device->CreateTexture2D, &desc, nullptr);
    copy(mipmapped, initial, {0, 0, (LONG)desc.Width, (LONG)desc.Height});
    context->GenerateMips(COMe(ID3D11ShaderResourceView, device->CreateShaderResourceView, mipmapped, nullptr));
    return mipmapped;
}
