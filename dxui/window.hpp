#pragma once

#include "base.hpp"
#include "draw.hpp"
#include "resource.hpp"
#include "widget.hpp"

#include <dcomp.h>

namespace ui {
    // A DirectX11 swap chain-backed window. The intended use is as an observer of external
    // data model updates:
    //
    //     auto cursor = ui::loadDefaultCursor();
    //     auto iconSm = ui::loadSmallIcon(...);
    //     auto iconLg = ui::loadNormalIcon(...);
    //     auto window = ui::window{L"unique class name", cursor, igonLg, iconSm, 800, 600, WS_OVERLAPPEDWINDOW};
    //     window.draw(someWidget);
    //
    struct window : private widget_parent {
        window(const wchar_t* name, cursor& cursor, icon& iconLg, icon& iconSm, int w, int h, int style);

        // Cast this window to a raw WinAPI handle.
        operator HWND() const { return handle.get(); }

        // Show this window. NOTE: the window is initially hidden.
        void show() { ShowWindow(*this, SW_SHOWNORMAL); }

        // Hide (not minimize!) this window.
        void hide() { ShowWindow(*this, SW_HIDE); }

        // Whether the window is visible, i.e. not hidden or minimized.
        bool isVisible() { return IsWindowVisible(*this); }

        // Make this window transparent with a given ABGR tint. Must be done while the window is hidden.
        void makeTransparent(uint32_t tint = 0x00000000u);

        // Make this window opaque. Must be done while the window is hidden.
        void makeOpaque();

        widget* getRoot() const { return root.get(); }

        // Set the root of the widget tree, return the old root.
        void setRoot(widget* newRoot) {
            root = widget_handle{newRoot, *this};
            draw();
        }

        // Schedule redrawing of the entire window.
        void draw() { lastPainted = {0, 0, 0, 0}; draw({0, 0, (LONG)w, (LONG)h}); }

        // Schedule redrawing of a region.
        void draw(RECT dirty);

        // Redraw the scheduled areas now.
        void drawScheduled();

        void onMouse(POINT abs, int keys) {
            if (mouseCapture)
                mouseCapture->onMouse(abs, keys);
            else if (root)
                root->onMouse(abs, keys);
        }

    public:
        // Fired when the window becomes visible because of a call to `show()` or because
        // the user has unminimized it.
        util::event<bool /* byUser */> onShow;

        // Fired when the window becomes hidden because of a call to `hide()` or because
        // the user has minimized it.
        util::event<bool /* byUser */> onHide;

        // Fired when the size of the window changes.
        util::event<> onResize;

        // Fired when the window is closed. Default handler destroys the window.
        util::event<> onClose;

        // Fired when the window is destroyed.
        util::event<> onDestroy;

    private:
        static void unregister_class(const wchar_t*);

        void onChildRedraw(widget&, RECT area) override { draw(area); }
        void onChildResize(widget&) override { draw(); }
        void onMouseCapture(widget* target) override {
            // assert(!!mouseCapture == !target);
            if ((mouseCapture = target)) {
                SetCapture(*this);
            } else {
                ReleaseCapture();
            }
        }

    private:
        dxcontext context;
        widget_handle root;
        widget* mouseCapture = nullptr;
        impl::holder_ex<const wchar_t*, unregister_class> wclass;
        impl::holder<HWND, CloseWindow> handle;
        winapi::com_ptr<IDXGISwapChain1> swapChain;
        winapi::com_ptr<IDCompositionDevice> device;
        winapi::com_ptr<IDCompositionTarget> target;
        winapi::com_ptr<IDCompositionVisual> visual;
        UINT w = 1, h = 1;
        RECT lastPainted = {0, 0, 0, 0};
    };
}
