#pragma once

#include "base.hpp"
#include "draw.hpp"
#include "resource.hpp"
#include "widget.hpp"

#include <dcomp.h>

namespace ui {
    // Get the current Windows 8 theme accent color, or 0 if unable to determine.
    uint32_t systemAccentColor();

    // Get the current Windows 8 theme background color, or 0 if unable to determine.
    uint32_t systemBackgroundColor();

    // Get the current Windows 8 theme taskbar color, or 0 if unable to determine.
    uint32_t systemTaskBarColor();

    // A DirectX11 swap chain-backed window.
    //
    // NOTE: the window is initially hidden.
    //
    struct window : private widget_parent {
        // Where the size of the content changes, extend or shrink the area
        //    gravity_start     downward and rightward
        //    gravity_center    uniformly in all directions
        //    gravity_end       upward and leftward
        enum gravity { gravity_start, gravity_center, gravity_end };

        window(int w, int h, int x = CW_USEDEFAULT, int y = CW_USEDEFAULT, window* parent = nullptr);

        // Cast this window to a raw WinAPI handle.
        operator HWND() const { return handle.get(); }

        // Show this window. If it is visible and maximized, restore it to the original size.
        void show() { ShowWindow(*this, SW_SHOWNORMAL); }

        // Enable or disable shadow and the accent border under this window.
        void setShadow(bool value = true);

        // Make it float on top of everything else.
        void setTopmost(bool permanently = false) {
            SetWindowPos(*this, permanently ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE|SWP_NOMOVE);
        }

        // Maximize this window.
        void maximize() { ShowWindow(*this, SW_SHOWMAXIMIZED); }

        // Minimize this window.
        void minimize() { ShowWindow(*this, SW_SHOWMINIMIZED); }

        // Minimize this window and hide it from the task bar.
        void hide() { ShowWindow(*this, SW_HIDE); }

        // Close this window.
        void close() { SendMessage(*this, WM_CLOSE, 0, 0); }

        // Move this window to a specified position on screen.
        void move(RECT p) { MoveWindow(*this, p.left, p.top, p.right - p.left, p.bottom - p.top, TRUE); }

        // Whether the window is visible, i.e. not hidden or minimized.
        bool isVisible() { return IsWindowVisible(*this); }

        // Whether this window is maximized, duh.
        bool isMaximized() { return IsZoomed(*this); }

        // Set the window title.
        void setTitle(const wchar_t* value) { winapi::throwOnFalse(SetWindowText(*this, value)); }

        // Set the direction in which the window will change size to accomodate the root widget.
        void setGravity(gravity h, gravity v) { hGravity = h, vGravity = v; }

        // Set the background of the window, in ARGB format. Default is white.
        // Transparent colors use the Windows 10 blur effect if possible, else
        // are made non-transparent.
        void setBackground(uint32_t tint, bool acrylic = false);

        // Enable or disable the ability to drag the window by any area that does not
        // handle mouse clicks otherwise.
        void setDragByEmptyAreas(bool value) { dragByEmptyAreas = value; }

        // Create or replace a notification icon in the system tray area.
        void setNotificationIcon(const ui::icon& icon, util::span<const wchar_t> tip = {});

        // Remove the notification icon.
        void clearNotificationIcon() { notifyIcon.reset(); }

        widget* getRoot() const { return root.get(); }

        // Set the root of the widget tree, return the old root.
        void setRoot(widget* newRoot) {
            root = widget_handle{newRoot, *this};
            draw();
        }

        // Schedule redrawing of the entire window.
        void draw() { draw({0, 0, size.x, size.y}); }

        // Schedule redrawing of a region.
        void draw(RECT);

        // Redraw the specified area right now.
        void drawImmediate(RECT);

        void onMouse(POINT abs, int keys);
        void onMouseLeave();

    public:
        // Fired when the window becomes visible because of a call to `show()` or because
        // the user has unminimized it.
        util::event<bool /* byUser */> onShow;

        // Fired when the window becomes hidden because of a call to `hide()` or because
        // the user has minimized it.
        util::event<bool /* byUser */> onHide;

        // Fired when the size of the window changes.
        util::event<> onResize;

        // Fired when the window gains focus.
        util::event<> onFocus;

        // Fired when the window loses focus.
        util::event<> onBlur;

        // Fired when the window is closed. Default handler destroys the window.
        util::event<> onClose;

        // Fired when the window is destroyed.
        util::event<> onDestroy;

        // Fired when a notification icon is activated with a left click or a space bar
        // for the primary action, or a right click or keyboard selection for the secondary.
        util::event<POINT, bool /* primary */> onNotificationIcon;

    private:
        void onChildRelease(widget& w) override { setRoot(nullptr); }
        void onChildRedraw(widget&, RECT area) override { draw(area); }
        void onChildResize(widget&) override { draw(); }
        void onMouseCapture(widget& target) override {
            // assert(!mouseCapture);
            mouseCapture = &target;
            SetCapture(*this);
        }

        struct release_notify_icon {
            void operator()(NOTIFYICONDATA* nid) {
                Shell_NotifyIcon(NIM_DELETE, nid);
                delete nid;
            }
        };

    private:
        dxcontext context;
        widget_handle root;
        widget* mouseCapture = nullptr;
        impl::holder<HWND, DestroyWindow> handle;
        winapi::com_ptr<IDXGISwapChain1> swapChain;
        winapi::com_ptr<IDCompositionDevice> device;
        winapi::com_ptr<IDCompositionTarget> target;
        winapi::com_ptr<IDCompositionVisual> visual;
        std::unique_ptr<NOTIFYICONDATA, release_notify_icon> notifyIcon;
        gravity hGravity = gravity_start;
        gravity vGravity = gravity_start;
        POINT size = {1, 1};
        POINT dragBy = {-1, -1};
        RECT lastPainted = {0, 0, 0, 0};
        uint32_t background = 0xFFFFFFFFu;
        bool mouseInBounds = false;
        bool dragByEmptyAreas = true;
    };

    namespace impl {
        LRESULT windowProc(HWND handle, UINT msg, WPARAM wParam, LPARAM lParam);
    }
}
