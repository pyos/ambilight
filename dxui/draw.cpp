#include "draw.hpp"

#include "shlwapi.h"
#include "wincodec.h"

#pragma comment(lib, "shlwapi.lib")

ui::dxcontext::dxcontext() {
    D3D_FEATURE_LEVEL level;
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
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
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
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
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    blendOver = COMe(ID3D11BlendState, device->CreateBlendState, &blend);

    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
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
    context->PSSetShader(pixelId, nullptr, 0);
    context->PSSetSamplers(0, 1, &linear);
    context->RSSetState(rState);

}

void ui::dxcontext::clear(ID3D11Texture2D* target, RECT area) {
    ui::vertex quad[] = {QUAD(-1, +1, +1, -1, 0, 0, 0, 0, 0)};
    auto rt = COMe(ID3D11RenderTargetView, device->CreateRenderTargetView, target, nullptr);
    auto vb = buffer(util::span<ui::vertex>(quad).reinterpret<const uint8_t>(), D3D11_BIND_VERTEX_BUFFER);
    UINT stride = sizeof(vertex);
    UINT offset = 0;
    D3D11_TEXTURE2D_DESC targetDesc;
    target->GetDesc(&targetDesc);
    D3D11_VIEWPORT vp = {0, 0, (FLOAT)targetDesc.Width, (FLOAT)targetDesc.Height, 0, 1};
    context->RSSetViewports(1, &vp);
    context->RSSetScissorRects(1, &area);
    context->OMSetBlendState(blendClear, nullptr, 0xFFFFFFFFU);
    context->OMSetRenderTargets(1, &rt, nullptr);
    context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context->Draw(6, 0);
}

void ui::dxcontext::draw(ID3D11Texture2D* target, ID3D11Texture2D* source, util::span<ui::vertex> vertices, RECT cull) {
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
    if (sourceDesc.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS)
        context->GenerateMips(sr);
    D3D11_VIEWPORT vp = {0, 0, (FLOAT)targetDesc.Width, (FLOAT)targetDesc.Height, 0, 1};
    context->RSSetViewports(1, &vp);
    context->RSSetScissorRects(1, &cull);
    context->OMSetBlendState(blendOver, nullptr, 0xFFFFFFFFU);
    context->OMSetRenderTargets(1, &rt, nullptr);
    context->PSSetShaderResources(0, 1, &sr);
    context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context->Draw(vertices.size(), 0);
}

winapi::com_ptr<ID3D11Texture2D> ui::dxcontext::textureFromPNG(util::span<uint8_t> in) {
    winapi::com_ptr<IStream> stream;
    *&stream = SHCreateMemStream(in.data(), in.size());
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

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = tmp.data();
    initData.SysMemPitch = width * 4;
    return COMe(ID3D11Texture2D, device->CreateTexture2D, &desc, &initData);
}
