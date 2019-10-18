#include "base.hpp"
#include "resource.hpp"
#include "window.hpp"

#include <shellscalingapi.h>
#pragma comment(lib, "shcore.lib")

HINSTANCE ui::impl::hInstance = nullptr;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, INT nCmdShow) try {
    ui::impl::hInstance = hInstance;
    ui::initCOM();
    WNDCLASSEX wc = {};
    auto iconLg = ui::loadNormalIcon(ui::fromBundled(IDI_APP));
    auto iconSm = ui::loadSmallIcon(ui::fromBundled(IDI_APP));
    auto cursor = ui::loadCursor(ui::fromFile(IDC_ARROW));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpszClassName = L"__mainClass";
    wc.lpfnWndProc   = ui::window::windowProc;
    wc.hInstance     = ui::impl::hInstance;
    wc.hIcon         = iconLg.get();
    wc.hIconSm       = iconSm.get();
    wc.hCursor       = cursor.get();
    wc.hbrBackground = CreateSolidBrush(0x00000000);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    winapi::throwOnFalse(RegisterClassEx(&wc));
    return ui::main();
} catch (const std::exception& e) {
    // TODO
    return 1;
}

int ui::dispatch() {
    DWORD_PTR scalingCookie;
    ui::handle scalingEvent{CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS)};
    winapi::throwOnFalse(scalingEvent);
    winapi::throwOnFalse(RegisterScaleChangeEvent(scalingEvent.get(), &scalingCookie));
    // XXX maybe RegisterScaleChangeEvent(scalingCookie) on exit?
    MSG msg;
    while (true) {
        auto event = scalingEvent.get();
        auto ready = MsgWaitForMultipleObjectsEx(1, &event, INFINITE, QS_ALLINPUT, 0);
        winapi::throwOnFalse(ready != WAIT_FAILED);
        if (ready == WAIT_OBJECT_0) {
            EnumThreadWindows(GetCurrentThreadId(), [](HWND hWnd, LPARAM lParam) {
                RECT r = {};
                GetClientRect(hWnd, &r);
                SendMessage(hWnd, WM_MOVE /* XXX uhhhhh */, 0, r.top << 16 | r.left);
                return TRUE;
            }, 0);
        }
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                return (int)msg.wParam;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

void ui::initCOM() {
    winapi::throwOnFalse(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
}

void ui::quit(int statusCode) {
    PostQuitMessage(statusCode);
}
