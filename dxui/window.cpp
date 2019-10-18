#include "window.hpp"

#include <windowsx.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shcore.lib")

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

template <typename F>
static F* GetProcAddress(HMODULE hModule, LPCSTR lpProcName) {
    return reinterpret_cast<F*>(GetProcAddress(hModule, lpProcName));
}

static HMODULE hUser32 = GetModuleHandle(L"user32.dll");
static auto GetWindowCompositionAttribute = GetProcAddress<BOOL WINAPI(HWND, WINCOMPATTRDATA*)>(hUser32, "GetWindowCompositionAttribute");
static auto SetWindowCompositionAttribute = GetProcAddress<BOOL WINAPI(HWND, WINCOMPATTRDATA*)>(hUser32, "SetWindowCompositionAttribute");

static HMODULE hUxTheme = LoadLibrary(L"uxtheme.dll");
static auto GetImmersiveUserColorSetPreference = GetProcAddress<DWORD(BOOL bForceCheckRegistry, BOOL bSkipCheckOnFail)>(hUxTheme, (LPCSTR)98);
static auto GetImmersiveColorTypeFromName = GetProcAddress<DWORD(LPCWSTR)>(hUxTheme, (LPCSTR)96);
static auto GetImmersiveColorFromColorSetEx = GetProcAddress<DWORD(
    DWORD dwImmersiveColorSet, DWORD dwImmersiveColorType, BOOL bIgnoreHighContrast,
    DWORD dwHighContrastCacheMode)>(hUxTheme, (LPCSTR)95);

static void applyGravity(ui::window::gravity g, LONG& a, LONG& b, LONG& d) {
    switch (g) {
        default:
        case ui::window::gravity_start:  b = a + d; break;
        case ui::window::gravity_center: a = (a + b - d) / 2; b = (a + b + d) / 2;
        case ui::window::gravity_end:    a = b - d; break;
    }
}

static uint32_t scaleFactorForWindow(HWND handle) {
    DEVICE_SCALE_FACTOR sf = DEVICE_SCALE_FACTOR_INVALID;
    GetScaleFactorForMonitor(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &sf);
    switch (sf) {
    default:
    case SCALE_100_PERCENT: return 100;
    case SCALE_120_PERCENT: return 120;
    case SCALE_125_PERCENT: return 125;
    case SCALE_140_PERCENT: return 140;
    case SCALE_150_PERCENT: return 150;
    case SCALE_160_PERCENT: return 160;
    case SCALE_175_PERCENT: return 175;
    case SCALE_180_PERCENT: return 180;
    case SCALE_200_PERCENT: return 200;
    case SCALE_225_PERCENT: return 225;
    case SCALE_250_PERCENT: return 250;
    case SCALE_300_PERCENT: return 300;
    case SCALE_350_PERCENT: return 350;
    case SCALE_400_PERCENT: return 400;
    case SCALE_450_PERCENT: return 450;
    case SCALE_500_PERCENT: return 500;
    }
}

LRESULT ui::window::windowProc(HWND handle, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto window = reinterpret_cast<ui::window*>(GetWindowLongPtr(handle, GWLP_USERDATA));
    if (window) switch (msg) {
        case WM_SHOWWINDOW:
            (wParam ? window->onShow : window->onHide)(lParam);
            break; // NOTE: returning 0 cancels
        case WM_SIZE:
            window->draw();
            window->onResize();
            // fallthrough
        case WM_MOVE:
            if (auto sf = scaleFactorForWindow(handle); sf != window->scaleFactor)
                window->scaleFactor = sf, window->draw();
            break;
        case WM_CLOSE:
            if (window->onClose())
                return 0;
            break;
        case WM_DESTROY:
            window->onDestroy();
            break;
        case WM_GETMINMAXINFO:
            if (window->root)
                reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = window->root->measureMin();
            break;
        case WM_SIZING:
            if (window->root) {
                auto r = reinterpret_cast<RECT*>(lParam);
                auto [w, h] = window->root->measure({r->right - r->left, r->bottom - r->top});
                auto left = wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_LEFT;
                auto top  = wParam == WMSZ_TOPRIGHT   || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOP;
                applyGravity(left ? ui::window::gravity_end : ui::window::gravity_start, r->left, r->right, w);
                applyGravity(top  ? ui::window::gravity_end : ui::window::gravity_start, r->top, r->bottom, w);
            }
            break;
        case WM_ACTIVATE:
        case WM_ACTIVATEAPP:
            (wParam ? window->onFocus : window->onBlur)();
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
            window->onMouse(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, (int)wParam);
            break;
        case WM_MOUSELEAVE:
            window->onMouseLeave();
            break;
        case WM_PAINT: {
            RECT scheduled;
            if (GetUpdateRect(handle, &scheduled, FALSE))
                window->drawImmediate(scheduled);
            break;
        }
        case WM_SETTINGCHANGE:
            if (lParam && wcscmp((LPCWSTR)lParam, L"ImmersiveColorSet") == 0 && !window->onSystemColorsChange())
                window->draw();
            break;
        case WM_NCCALCSIZE:
            return 0;
        case WM_USER: switch (LOWORD(lParam)) {
            case NIN_SELECT:
            case NIN_KEYSELECT:
                window->onNotificationIcon(POINT{GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)}, true);
                break;
            case WM_CONTEXTMENU:
                window->onNotificationIcon(POINT{GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)}, false);
                break;
        }
    }
    return DefWindowProc(handle, msg, wParam, lParam);
}

ui::window::window(int w, int h, int x, int y, window* parent) {
    handle.reset(CreateWindowEx(0, L"__mainClass", L"", WS_OVERLAPPEDWINDOW, x, y,
        w, h, parent ? parent->handle.get() : nullptr, nullptr, hInstance, nullptr));
    winapi::throwOnFalse(handle);
    SetWindowLongPtr(*this, GWLP_USERDATA, (LONG_PTR)this);
    // Request a WM_NCCALCSIZE so that Windows knows we don't want a frame.
    SetWindowPos(*this, HWND_NOTOPMOST, 0, 0, w, h, SWP_DRAWFRAME|SWP_NOMOVE);

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
    target = COMe(IDCompositionTarget, device->CreateTargetForHwnd, *this, FALSE);
    winapi::throwOnFalse(device->CreateVisual(&visual));
    winapi::throwOnFalse(visual->SetContent(swapChain.get()));
    winapi::throwOnFalse(target->SetRoot(visual));
    winapi::throwOnFalse(device->Commit());
}

void ui::window::setShadow(bool value) {
    MARGINS m = {value, value, value, value};
    DwmExtendFrameIntoClientArea(*this, &m);
}

void ui::window::drawImmediate(RECT scheduled) {
    RECT rect;
    GetWindowRect(*this, &rect);
    if (root) {
        // Fit the window size to contents.
        POINT actualSize = {rect.right - rect.left, rect.bottom - rect.top};
        POINT neededSize = root->measure(actualSize);
        if (neededSize.x != actualSize.x || neededSize.y != actualSize.y) {
            applyGravity(hGravity, rect.left, rect.right, neededSize.x);
            applyGravity(vGravity, rect.top, rect.bottom, neededSize.y);
            move(rect);
        }
    }

    if (size.x != rect.right - rect.left || size.y != rect.bottom - rect.top) {
        // Fit the swapchain buffer to window size.
        auto [w, h] = size = {std::max(rect.right - rect.left, 1L), std::max(rect.bottom - rect.top, 1L)};
        DXGI_SWAP_CHAIN_DESC swapChainDesc;
        swapChain->GetDesc(&swapChainDesc);
        winapi::throwOnFalse(swapChain->ResizeBuffers(swapChainDesc.BufferCount, w, h, swapChainDesc.BufferDesc.Format, swapChainDesc.Flags));
        scheduled = {0, 0, w, h};
    }

    auto target = COMv(ID3D11Texture2D, swapChain->GetBuffer, 0);
    if (!isSubRect(lastPainted, scheduled)) {
        // We're in flip mode, so the current back buffer contains outdated state,
        // which we also need to repaint.
        //
        // TODO copy from the other buffer directly somehow
        context.clear(target, lastPainted, background);
        if (root) root->draw(context, target, {0, 0, size.x, size.y}, lastPainted);
    }
    context.clear(target, scheduled, background);
    if (root) root->draw(context, target, {0, 0, size.x, size.y}, scheduled);
    lastPainted = scheduled;
    winapi::throwOnFalse(swapChain->Present(1, 0));
}

uint32_t ui::systemColor(ui::system_color c) {
    auto name = c == system_color::accent     ? L"ImmersiveSystemAccent"
              : c == system_color::background ? L"ImmersiveApplicationBackground"
              : c == system_color::taskbar    ? L"ImmersiveSystemBackground"
              : nullptr;
    if (!GetImmersiveUserColorSetPreference || !GetImmersiveColorTypeFromName || !GetImmersiveColorFromColorSetEx || !name)
        return 0;
    auto colorSet = GetImmersiveUserColorSetPreference(FALSE, FALSE);
    auto colorType = GetImmersiveColorTypeFromName(name);
    auto abgr = GetImmersiveColorFromColorSetEx(colorSet, colorType, FALSE, 0);
    return abgr & 0xFF00FF00u | (abgr & 0xFF0000u) >> 16 | (abgr & 0xFFu) << 16;
}

void ui::window::setBackground(uint32_t tint, bool acrylic) {
    if (!SetWindowCompositionAttribute) {
        background = tint | 0xFF000000u;
        return;
    }
    ACCENT_POLICY ap = {};
    if ((tint >> 24) != 0xFF) {
        ap.AccentState = acrylic ? ACCENT_ENABLE_ACRYLICBLURBEHIND : ACCENT_ENABLE_BLURBEHIND;
        ap.AccentFlags = 2;
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

void ui::window::setNotificationIcon(const ui::icon& icon, util::span<const wchar_t> tip) {
    bool adding = !notifyIcon;
    if (adding)
        notifyIcon.reset(new NOTIFYICONDATA{});
    notifyIcon->cbSize   = sizeof(NOTIFYICONDATA);
    notifyIcon->hWnd     = *this;
    notifyIcon->hIcon    = icon.get();
    notifyIcon->uFlags   = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    notifyIcon->uTimeout = 0;
    notifyIcon->uCallbackMessage = WM_USER;
    memset(notifyIcon->szTip, 0, sizeof(notifyIcon->szTip));
    if (tip)
        memcpy(notifyIcon->szTip, tip.data(), std::min(tip.size(), ARRAYSIZE(notifyIcon->szTip)) * sizeof(wchar_t));
    winapi::throwOnFalse(Shell_NotifyIcon(adding ? NIM_ADD : NIM_MODIFY, notifyIcon.get()));
    if (adding) {
        notifyIcon->uVersion = NOTIFYICON_VERSION_4;
        winapi::throwOnFalse(Shell_NotifyIcon(NIM_SETVERSION, notifyIcon.get()));
    }
}

void ui::window::onMouse(POINT abs, int keys) {
    if (!mouseInBounds) {
        mouseInBounds = true;
        TRACKMOUSEEVENT tme = {};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = *this;
        winapi::throwOnFalse(TrackMouseEvent(&tme));
    }
    if (mouseCapture) {
        if (!mouseCapture->onMouse(abs, keys)) {
            mouseCapture = nullptr;
            ReleaseCapture();
        }
    } else if ((!root || !root->onMouse(abs, keys))) {
        if (dragBy.x >= 0 && dragBy.y >= 0) {
            RECT rect;
            GetWindowRect(*this, &rect);
            // `abs` is actually relative to the window, so moving the window also moves
            // the coordinate frame. That's why we *don't* update `dragBy` here; effectively,
            // this code is keeping `abs` at a constant value.
            MoveWindow(*this, rect.left + abs.x - dragBy.x, rect.top + abs.y - dragBy.y,
                       rect.right - rect.left, rect.bottom - rect.top, TRUE);
            if (!(keys & MK_LBUTTON)) {
                dragBy = {-1, -1};
                ReleaseCapture();
            }
        } else if (dragByEmptyAreas && (keys & MK_LBUTTON)) {
            dragBy = abs;
            SetCapture(*this);
        }
    }
}

void ui::window::onMouseLeave() {
    mouseInBounds = false;
    if (root)
        root->onMouseLeave();
}
