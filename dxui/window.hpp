#pragma once

#include "base.hpp"
#include "draw.hpp"
#include "resource.hpp"
#include "widget.hpp"

#include <dcomp.h>

namespace ui {
    enum class system_color { accent, background, taskbar };

    // Get a Windows 8 theme color, or 0 if unable to determine.
    uint32_t systemColor(system_color);

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

        // Make this the active window.
        void focus() { SetForegroundWindow(*this), SetFocus(*this); }

        // Whether the window is visible, i.e. not hidden or minimized.
        bool isVisible() const { return IsWindowVisible(*this); }

        // Whether this window is maximized, duh.
        bool isMaximized() const { return IsZoomed(*this); }

        // Return the current display's scale factor, in percent.
        uint32_t scale() const { return scaleFactor; }

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

        // Set the root of the widget tree, return the old root.
        void setRoot(widget* newRoot) {
            root = widget_handle{newRoot, *this};
            draw();
        }

        // Send a message to the thread that owns this window (see `onMessage`).
        // If it is the current thread, block until the message is processed.
        void post(uintptr_t data);

        void captureMouse(widget& target) {
            // assert(!mouseCapture);
            mouseCapture = &target;
            SetCapture(*this);
        }

        static LRESULT windowProc(HWND handle, UINT msg, WPARAM wParam, LPARAM lParam);

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

        // Fired when the system theme colors changes. Default action is to redraw
        // all widgets.
        util::event<> onSystemColorsChange;

        // Fired when `post` is called and the message reaches the thread that owns this window.
        util::event<uintptr_t> onMessage;

    private:
        void draw() { draw({0, 0, size.x, size.y}); }
        void draw(RECT dirty) { InvalidateRect(*this, &dirty, FALSE); }
        void drawImmediate(RECT);
        void onMouse(POINT abs, int keys);
        void onMouseLeave();
        void onChildRelease(widget& w) override { setRoot(nullptr); }
        void onChildRedraw(widget&, RECT area) override { draw(area); }
        void onChildResize(widget&) override { draw(); }
        window* parentWindow() override { return this; }

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
        winapi::holder<HWND, DestroyWindow> handle;
        winapi::com_ptr<IDXGISwapChain1> swapChain;
        winapi::com_ptr<IDCompositionDevice> device;
        winapi::com_ptr<IDCompositionTarget> target;
        winapi::com_ptr<IDCompositionVisual> visual;
        std::unique_ptr<NOTIFYICONDATA, release_notify_icon> notifyIcon;
        gravity hGravity = gravity_start;
        gravity vGravity = gravity_start;
        POINT size = {1, 1};
        POINT dragBy = {-1, -1};
        uint32_t scaleFactor = 100;
        uint32_t background = 0xFFFFFFFFu;
        bool mouseInBounds = false;
        bool dragByEmptyAreas = true;
    };
}
