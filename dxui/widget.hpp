#pragma once

#include <algorithm>
#include <map>

#include "draw.hpp"
#include "event.hpp"

namespace ui {
    struct window;
    struct widget;
    struct widget_parent {
        virtual void onChildRelease(widget&) = 0;
        virtual void onChildRedraw(widget&, RECT) = 0;
        virtual void onChildResize(widget&) = 0;
        virtual window* parentWindow() = 0;
    };

    struct widget : widget_parent {
        widget() = default;
        // Since there are pointers everywhere, copying and moving is forbidden.
        // Use `unique_ptr` and `shared_ptr` if you have to.
        widget(const widget&) = delete;
        widget& operator=(const widget&) = delete;

        virtual ~widget() {
            if (parent) parent->onChildRelease(*this);
            // assert(!parent);
        }

        // Return the minimum size for this widget. Any size passed to `measure`
        // will be made at least as big as the returned value. If a rectangle
        // smaller than this is given to `draw`, the widget is clipped.
        POINT measureMin() const {
            if (lastMeasureMin.x < 0 && lastMeasureMin.y < 0)
                lastMeasureMin = measureMinImpl();
            return lastMeasureMin;
        }

        // Return the size this widget would take when drawn into a parent of a
        // given fixed size. If given a rectangle bigger than the returned value,
        // the widget will only occupy its top left corner.
        POINT measure(POINT fit) const {
            POINT min = measureMin();
            POINT rnd = {std::max(fit.x, min.x), std::max(fit.y, min.y)};
            if (lastMeasureArg.x != rnd.x || lastMeasureArg.y != rnd.y) {
                lastMeasureRes = measureImpl(rnd);
                lastMeasureArg = rnd;
            }
            return lastMeasureRes;
        }

        // Draw this widget into a given texture. Precisely the `dirty` part of `area`
        // will be drawn to; anything outside of it is untouched.
        //
        // WARNING: for any given total area width and height, if `measure({width, height})`
        //          does not return `{width, height}`, the result of drawing into that area
        //          is undefined; behavior of mouse events especially. Meaning, the widget
        //          will do something that will probably look weird, e.g. draw a button
        //          that is smaller than the requested area but reacts to mouse events
        //          in the seemingly empty space around it. So don't do that.
        //
        void draw(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const {
            drawImpl(ctx, target, lastDrawRect = total, dirty);
        }

        void setParent(widget_parent* p) {
            parent = p;
        }

        void onChildRelease(widget& w) override {
            throw std::runtime_error("parent ignored child widget release request; use-after-free imminent");
        }

        void onChildRedraw(widget&, RECT area) override {
            if (parent) parent->onChildRedraw(*this, area);
        }

        void onChildResize(widget&) override {
            invalidateSize();
        }

        window* parentWindow() override {
            return parent ? parent->parentWindow() : nullptr;
        }

        virtual bool onMouse(POINT abs, int keys) { return false; }
        virtual void onMouseLeave() { }

    protected:
        virtual POINT measureMinImpl() const = 0;
        virtual POINT measureImpl(POINT) const { return measureMin(); }
        virtual void drawImpl(ui::dxcontext&, ID3D11Texture2D*, RECT total, RECT dirty) const { }

        // Mark the contents of this widget as updated.
        void invalidate() {
            if (parent) parent->onChildRedraw(*this, lastDrawRect);
        }

        // Mark the size and the contents of this widget as updated.
        //
        // NOTE: this is a heavyweight operation that is likely to cause invalidation
        //       of a lot of widgets. Prefer to use `invalidate` when possible.
        void invalidateSize() {
            lastMeasureMin = {-1, -1};
            lastMeasureArg = {-1, -1};
            if (parent) parent->onChildResize(*this);
        }

        // Map a point from window coordinates to widget coordinates, which are just
        // window coordinates offset by the last draw position.
        POINT relative(POINT abs) const {
            return {abs.x - lastDrawRect.left, abs.y - lastDrawRect.top};
        }

        // Inverse of `relative`.
        POINT absolute(POINT rel) const {
            return {rel.x + lastDrawRect.left, rel.y + lastDrawRect.top};
        }

        // The last draw size of this widget, i.e. the bottom-right corner in widget coordinates.
        POINT size() const {
            return relative({lastDrawRect.right, lastDrawRect.bottom});
        }

    private:
        widget_parent* parent = nullptr;
        mutable RECT lastDrawRect = RECT_ID;
        mutable POINT lastMeasureMin = {-1, -1};
        mutable POINT lastMeasureArg = {-1, -1};
        mutable POINT lastMeasureRes;
    };

    struct widget_release {
        void operator()(widget* w) {
            w->setParent(nullptr);
        }
    };

    struct widget_handle : std::unique_ptr<widget, widget_release> {
        widget_handle() = default;
        widget_handle(widget* child, widget_parent& parent)
            : std::unique_ptr<widget, widget_release>(child)
        {
            if (child) child->setParent(&parent);
        }
    };

    // A convenience object for widgets that capture the mouse pointer while a button
    // is held after hovering over the widget. As an example, consider the slider
    // (aka trackbar): when you start dragging it, you can move the pointer around
    // the screen and still have it update. Very convenient for touch screens.
    struct capture_state {
        // Call this in the widget's `onMouse` handler. Return values:
        //     ignore     hovering with a button held from outside
        //     prepare    hovering with no buttons held
        //     capture    pressed a button, call `parentWindow()->captureMouse()` and return `true`
        //     drag       still holding a button, keep returning `true`
        //     release    released all buttons, return `false`
        enum { ignore, prepare, capture, drag, release } update(int keys) {
            static const int MK_ANY = MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2;
            bool a = capturing;
            bool b = capturing = (capturing || capturable) && (keys & MK_ANY);
            capturable = !(keys & MK_ANY);
            return a ? (b ? drag : release) : (b ? capture : capturable ? prepare : ignore);
        }

        // Call this in the widget's `onMouseLeave` handler.
        void stop() {
            capturable = false;
        }

    private:
        bool capturable = false;
        bool capturing = false;
    };
}
