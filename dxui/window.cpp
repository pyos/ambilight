#include "window.hpp"
#include <windowsx.h>

typedef enum {
    WCA_UNDEFINED,
    WCA_NCRENDERING_ENABLED,
    WCA_NCRENDERING_POLICY,
    WCA_TRANSITIONS_FORCEDISABLED,
    WCA_ALLOW_NCPAINT,
    WCA_CAPTION_BUTTON_BOUNDS,
    WCA_NONCLIENT_RTL_LAYOUT,
    WCA_FORCE_ICONIC_REPRESENTATION,
    WCA_EXTENDED_FRAME_BOUNDS,
    WCA_HAS_ICONIC_BITMAP,
    WCA_THEME_ATTRIBUTES,
    WCA_NCRENDERING_EXILED,
    WCA_NCADORNMENTINFO,
    WCA_EXCLUDED_FROM_LIVEPREVIEW,
    WCA_VIDEO_OVERLAY_ACTIVE,
    WCA_FORCE_ACTIVEWINDOW_APPEARANCE,
    WCA_DISALLOW_PEEK,
    WCA_CLOAK,
    WCA_CLOAKED,
    WCA_ACCENT_POLICY,
    WCA_FREEZE_REPRESENTATION,
    WCA_EVER_UNCLOAKED,
    WCA_VISUAL_OWNER,
    WCA_HOLOGRAPHIC,
    WCA_EXCLUDED_FROM_DDA,
    WCA_PASSIVEUPDATEMODE,
    WCA_USEDARKMODECOLORS, // 1903
    WCA_LAST
} WINCOMPATTR;

typedef struct {
    WINCOMPATTR Attrib;
    LPVOID pvData;
    DWORD cbData;
} WINCOMPATTRDATA;

typedef enum {
    ACCENT_DISABLED,
    ACCENT_ENABLE_GRADIENT,
    ACCENT_ENABLE_TRANSPARENTGRADIENT,
    ACCENT_ENABLE_BLURBEHIND,
    ACCENT_ENABLE_ACRYLICBLURBEHIND, // 1803
    ACCENT_ENABLE_HOSTBACKDROP, // 1809
    ACCENT_INVALID_STATE
} ACCENT_STATE;

typedef struct {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    COLORREF GradientColor;
    DWORD AnimationId;
} ACCENT_POLICY;

using WindowCompositionAttributeFn = BOOL WINAPI(HWND, WINCOMPATTRDATA*);

static HMODULE hUser32 = GetModuleHandle(L"user32.dll");
static WindowCompositionAttributeFn* GetWindowCompositionAttribute =
    (WindowCompositionAttributeFn*)GetProcAddress(hUser32, "GetWindowCompositionAttribute");
static WindowCompositionAttributeFn* SetWindowCompositionAttribute =
    (WindowCompositionAttributeFn*)GetProcAddress(hUser32, "SetWindowCompositionAttribute");

void ui::window::unregister_class(const wchar_t* name) {
    UnregisterClass(name, ui::impl::hInstance);
}

static POINT windowExtraSpace(HWND handle, POINT sz) {
    RECT client, total;
    GetClientRect(handle, &client);
    GetWindowRect(handle, &total);
    auto dw = (client.left - total.left) + (total.right - client.right);
    auto dh = (client.top - total.top) + (total.bottom - client.bottom);
    return {sz.x + dw, sz.y + dh};
}

static LRESULT windowProc(HWND handle, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto window = reinterpret_cast<ui::window*>(GetWindowLongPtr(handle, GWLP_USERDATA));
    if (window) switch (msg) {
        case WM_SHOWWINDOW:
            if (!(wParam ? window->onShow : window->onHide)(lParam))
                return TRUE;
            break;
        case WM_SIZE:
            window->draw();
            if (!window->onResize())
                return TRUE;
            break;
        case WM_CLOSE:
            if (!window->onClose())
                return TRUE;
            break;
        case WM_DESTROY:
            if (!window->onDestroy())
                return TRUE;
            break;
        case WM_GETMINMAXINFO:
            if (auto root = window->getRoot())
                reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = windowExtraSpace(handle, root->measureMin());
            break;
        case WM_SIZING:
            if (auto root = window->getRoot()) {
                auto r = reinterpret_cast<RECT*>(lParam);
                auto [a, b] = windowExtraSpace(handle, {0, 0});
                auto [w, h] = root->measure({r->right - r->left - a, r->bottom - r->top - b});
                if (w + a != r->right - r->left) switch (wParam) {
                    case WMSZ_BOTTOMLEFT:
                    case WMSZ_TOPLEFT:
                    case WMSZ_LEFT: r->left = r->right - w - a; break;
                    default: r->right = r->left + w + a; break;
                }
                if (h + b != r->bottom - r->top) switch (wParam) {
                    case WMSZ_TOPRIGHT:
                    case WMSZ_TOPLEFT:
                    case WMSZ_TOP: r->top = r->bottom - h - a; break;
                    default: r->bottom = r->top + h + b; break;
                }
            }
            break;
        case WM_MOUSEMOVE:
        case WM_LBUTTONUP:
        case WM_LBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDOWN:
            window->onMouse(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, wParam);
            break;
        // case WM_MOUSELEAVE:
        //     TODO this requires TrackMouseEvent
        case WM_PAINT: {
            RECT scheduled;
            if (GetUpdateRect(handle, &scheduled, FALSE))
                window->drawImmediate(scheduled);
            break;
        }
    }
    return DefWindowProc(handle, msg, wParam, lParam);
}

ui::window::window(const wchar_t* name, cursor& cursor, icon& iconLg, icon& iconSm, int w, int h, int style) {
    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpszClassName = name;
    wc.lpfnWndProc   = windowProc;
    wc.hInstance     = ui::impl::hInstance;
    wc.hCursor       = cursor.get();
    wc.hIcon         = iconLg.get();
    wc.hIconSm       = iconSm.get();
    wc.hbrBackground = CreateSolidBrush(0x00000000);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    winapi::throwOnFalse(RegisterClassEx(&wc));
    wclass.reset(name);
    RECT r = {0, 0, w, h};
    winapi::throwOnFalse(AdjustWindowRect(&r, style, FALSE));
    handle.reset(CreateWindowEx(WS_EX_COMPOSITED, name, L"", style, CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top, nullptr, nullptr, impl::hInstance, nullptr));
    winapi::throwOnFalse(handle);
    SetWindowLongPtr(*this, GWLP_USERDATA, (LONG_PTR)this);
    SetWindowLongPtr(*this, GWL_STYLE, style);

    auto dxgiDevice = COMi(IDXGIDevice, context.raw()->QueryInterface);
    auto dxgiAdapter = COMi(IDXGIAdapter, dxgiDevice->GetParent);
    auto dxgiFactory = COMi(IDXGIFactory2, dxgiAdapter->GetParent);
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Width = 1;
    swapChainDesc.Height = 1;
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    swapChain = COMe(IDXGISwapChain1, dxgiFactory->CreateSwapChainForComposition, context.raw(), &swapChainDesc, nullptr);
    // Apparently the easiest way to render a transparent texture onto a window,
    // as non-undefined AlphaMode is only allowed for CreateSwapChainForComposition.
    device = COMv(IDCompositionDevice, DCompositionCreateDevice, dxgiDevice);
    target = COMe(IDCompositionTarget, device->CreateTargetForHwnd, *this, TRUE);
    winapi::throwOnFalse(device->CreateVisual(&visual));
    winapi::throwOnFalse(visual->SetContent(swapChain.get()));
    winapi::throwOnFalse(target->SetRoot(visual));
    winapi::throwOnFalse(device->Commit());
}

void ui::window::draw(RECT dirty) {
    InvalidateRect(*this, &dirty, FALSE);
}

void ui::window::drawImmediate(RECT scheduled) {
    RECT rect, total;
    GetClientRect(*this, &rect);
    GetWindowRect(*this, &total);
    POINT extra = {(rect.left - total.left) + (total.right - rect.right),
                   (rect.top - total.top) + (total.bottom - rect.bottom)};
    if (root) {
        POINT c = {rect.right - rect.left, rect.bottom - rect.top};
        POINT w = root->measure(c);
        if (w.x != c.x || w.y != c.y) {
            rect.right = rect.left + w.x;
            rect.bottom = rect.top + w.y;
            total.right = total.left + w.x + extra.x;
            total.bottom = total.top + w.y + extra.y;
            MoveWindow(*this, total.left, total.top, total.right - total.left, total.bottom - total.top, TRUE);
        }
    }

    if (w != rect.right - rect.left || h != rect.bottom - rect.top) {
        w = std::max(rect.right - rect.left, 1L);
        h = std::max(rect.bottom - rect.top, 1L);
        DXGI_SWAP_CHAIN_DESC swapChainDesc;
        swapChain->GetDesc(&swapChainDesc);
        winapi::throwOnFalse(swapChain->ResizeBuffers(swapChainDesc.BufferCount, w, h, swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));
        scheduled = {0, 0, (LONG)w, (LONG)h};
    }

    auto target = COMv(ID3D11Texture2D, swapChain->GetBuffer, 0);
    // We're in flip mode, so the current back buffer contains outdated state.
    // TODO don't draw the intersection twice at all
    if (!isSubRect(lastPainted, scheduled)) {
        context.clear(target, lastPainted, background);
        if (root) root->draw(context, target, {0, 0, (LONG)w, (LONG)h}, lastPainted);
    }
    context.clear(target, scheduled, background);
    if (root) root->draw(context, target, {0, 0, (LONG)w, (LONG)h}, scheduled);
    lastPainted = scheduled;
    winapi::throwOnFalse(swapChain->Present(1, 0));
}

void ui::window::setBackground(uint32_t tint) {
    ACCENT_POLICY ap = {};
    if ((tint >> 24) != 0xFF) {
        ap.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
        ap.AccentFlags = 2;
        // This value should be ABGR, not ARGB.
        ap.GradientColor = tint & 0xFF00FF00u | (tint & 0xFF) << 16 | (tint & 0xFF0000) >> 16;
        background = 0;
    } else {
        background = tint;
    }
    WINCOMPATTRDATA ca;
    ca.Attrib = WCA_ACCENT_POLICY;
    ca.pvData = &ap;
    ca.cbData = sizeof(ap);
    winapi::throwOnFalse(SetWindowCompositionAttribute(*this, &ca));
}

void ui::window::onMouse(POINT abs, int keys) {
    if (mouseCapture) {
        if (!mouseCapture->onMouse(abs, keys)) {
            mouseCapture = nullptr;
            ReleaseCapture();
        }
    } else if ((!root || !root->onMouse(abs, keys))) {
        if (!(keys & MK_LBUTTON)) {
            dragBy = {-1, -1};
        } else if (dragBy.x >= 0 && dragBy.y >= 0) {
            RECT rect;
            GetWindowRect(*this, &rect);
            // `abs` is actually relative to the window, so moving the window also moves
            // the coordinate frame. That's why we *don't* update `dragBy` here; effectively,
            // this code is keeping `abs` at a constant value.
            MoveWindow(*this, rect.left + abs.x - dragBy.x, rect.top + abs.y - dragBy.y,
                       rect.right - rect.left, rect.bottom - rect.top, TRUE);
        } else if (dragByEmptyAreas) {
            dragBy = abs;
        }
    }
}