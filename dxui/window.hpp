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
    // NOTE: the window is initially hidden.
    //
    struct window : private widget_parent {
        window(const wchar_t* name, cursor& cursor, icon& iconLg, icon& iconSm, int w, int h, int style);

        // Cast this window to a raw WinAPI handle.
        operator HWND() const { return handle.get(); }

        // Show this window.
        void show() {
            drawImmediate({0, 0, (LONG)w, (LONG)h});
            ShowWindow(*this, SW_SHOWNORMAL);
        }

        // Hide (not minimize) this window.
        void hide() { ShowWindow(*this, SW_HIDE); }

        // Whether the window is visible, i.e. not hidden or minimized.
        bool isVisible() { return IsWindowVisible(*this); }

        // Set the background of the window, in ARGB format. Default is white.
        // Transparent colors use the Windows 10 acrylic blur effect.
        void setBackground(uint32_t tint);

        // Enable or disable the ability to drag the window by any area that does not
        // handle mouse clicks otherwise.
        void setDragByEmptyAreas(bool value) {
            dragByEmptyAreas = value;
        }

        widget* getRoot() const { return root.get(); }

        // Set the root of the widget tree, return the old root.
        void setRoot(widget* newRoot) {
            root = widget_handle{newRoot, *this};
            draw();
        }

        // Schedule redrawing of the entire window.
        void draw() { draw({0, 0, (LONG)w, (LONG)h}); }

        // Schedule redrawing of a region.
        void draw(RECT);

        // Redraw the specified area right now.
        void drawImmediate(RECT);

        void onMouse(POINT abs, int keys);

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
        void onMouseCapture(widget& target) override {
            // assert(!mouseCapture);
            mouseCapture = &target;
            SetCapture(*this);
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
        uint32_t background = 0xFFFFFFFFu;
        bool dragByEmptyAreas = true;
        POINT dragBy = {-1, -1};
    };
}
