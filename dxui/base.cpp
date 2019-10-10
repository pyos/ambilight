#include "base.hpp"

HINSTANCE ui::impl::hInstance = nullptr;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, INT nCmdShow) {
    ui::impl::hInstance = hInstance;
    ui::initCOM();
    return ui::main();
}

int ui::dispatch() {
    MSG msg;
    while (true) {
        winapi::throwOnFalse(GetMessage(&msg, nullptr, 0, 0) >= 0);
        if (msg.message == WM_QUIT)
            return msg.wParam;
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
