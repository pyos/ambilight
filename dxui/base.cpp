#include "base.hpp"
#include "resource.hpp"
#include "window.hpp"

HINSTANCE ui::impl::hInstance = nullptr;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, INT nCmdShow) {
    ui::impl::hInstance = hInstance;
    ui::initCOM();
    WNDCLASSEX wc = {};
    auto iconLg = ui::loadNormalIcon(ui::fromBundled(IDI_APP));
    auto iconSm = ui::loadSmallIcon(ui::fromBundled(IDI_APP));
    auto cursor = ui::loadCursor(ui::fromFile(IDC_ARROW));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpszClassName = L"__mainClass";
    wc.lpfnWndProc   = ui::impl::windowProc;
    wc.hInstance     = ui::impl::hInstance;
    wc.hIcon         = iconLg.get();
    wc.hIconSm       = iconSm.get();
    wc.hCursor       = cursor.get();
    wc.hbrBackground = CreateSolidBrush(0x00000000);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    winapi::throwOnFalse(RegisterClassEx(&wc));
    return ui::main();
}

int ui::dispatch() {
    MSG msg;
    while (true) {
        winapi::throwOnFalse(GetMessage(&msg, nullptr, 0, 0) >= 0);
        if (msg.message == WM_QUIT)
            return (int)msg.wParam;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void ui::initCOM() {
    winapi::throwOnFalse(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
}

void ui::quit(int statusCode) {
    PostQuitMessage(statusCode);
}
