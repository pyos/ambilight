#pragma once

#include <algorithm>
#include <map>

#include "draw.hpp"
#include "event.hpp"

namespace ui {
    struct widget;
    struct widget_parent {
        virtual void onChildRedraw(widget&, RECT) = 0;
        virtual void onChildResize(widget&) = 0;
        virtual void onMouseCapture(widget*) = 0;
    };

    struct widget : widget_parent {
        virtual ~widget() = default;

        // Return the minimum size for this widget. Any size passed to `measure`
        // will be made at least as big as the returned value. If a rectangle
        // smaller than this is given to `draw`, the widget is clipped.
        POINT measureMin() const {
            if (lastMeasureMin.x < 0 && lastMeasureMin.y < 0)
                lastMeasureMin = measureMinEx();
            return lastMeasureMin;
        }

        // Return the size this widget would take when drawn into a parent of a
        // given fixed size. If given a rectangle bigger than the returned value,
        // the widget will only occupy its top left corner.
        POINT measure(POINT fit) const {
            POINT min = measureMin();
            POINT rnd = {std::max(fit.x, min.x), std::max(fit.y, min.y)};
            if (lastMeasureArg.x != rnd.x || lastMeasureArg.y != rnd.y) {
                lastMeasureRes = measureEx(rnd);
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
            drawEx(ctx, target, lastDrawRect = total, dirty);
        }

        void setParent(widget_parent* p) {
            parent = p;
        }

        void onChildRedraw(widget&, RECT area) override {
            if (parent) parent->onChildRedraw(*this, area);
        }

        void onChildResize(widget&) override {
            invalidateSize();
        }

        void onMouseCapture(widget* w) override {
            if (parent) parent->onMouseCapture(w);
        }

        virtual void onMouse(POINT abs, int keys) { }
        virtual void onMouseLeave() { }

    protected:
        virtual POINT measureMinEx() const = 0;
        virtual POINT measureEx(POINT) const { return measureMin(); }
        virtual void drawEx(ui::dxcontext&, ID3D11Texture2D*, RECT total, RECT dirty) const { }

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

        // Begin or end capturing the pointer into this widget.
        //
        // NOTE: releasing the pointer while it's outside the widget's boundaries
        //       does not send an `onMouseLeave` message until the next mouse action.
        void captureMouse(bool yes) {
            if (parent) parent->onMouseCapture(yes ? this : nullptr);
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

    // Just an empty space with a fixed size.
    struct spacer : widget {
        spacer(POINT size) : size_(size) { }

        POINT size() { return size_; }

        void setSize(POINT newSize) {
            size_ = newSize;
            invalidateSize();
        }

    private:
        POINT measureMinEx() const override { return size_; }
        POINT size_;
    };

    // A grid layout with stretch factors.
    //
    // Columns with a stretch factor 0 are given exactly as much horizontal space
    // as the item with the largest minimum size requires. After this is done,
    // all remaining space is allocated to the other columns proportionally to their
    // stretch factors. If some columns do not receive enough space to satisfy their
    // minimums through this process, they behave as if they are zero-stretch.
    // The same process applies to rows. Then, if any stretchy cell is bigger than
    // its contents, alignment rules are applied.
    //
    struct grid : widget {
        grid(size_t cols /* > 0 */, size_t rows /* > 0 */)
            : cells(cols * rows)
            , align(cols * rows)
            , cols(cols)
            , rows(rows)
        {
        }

        enum alignment { align_start, align_center, align_end };

        void set(size_t x /* < cols */, size_t y /* < rows */, widget* child,
                 alignment h = align_center, alignment v = align_center) {
            cells[x * rows.size() + y] = widget_handle{child, *this};
            align[x * rows.size() + y] = {h, v};
            invalidateSize();
        }

        void setColStretch(size_t x /* < cols */, uint32_t w) {
            cols[x].weight = w;
            invalidateSize();
        }

        void setRowStretch(size_t y /* < rows */, uint32_t w) {
            rows[y].weight = w;
            invalidateSize();
        }

    private:
        // TODO don't invalidate size if a stretchable widget is resized within the allocated bounds
        struct group {
            mutable LONG start;
            mutable LONG size;
            mutable LONG min;
            mutable uint32_t stretch;
            uint32_t weight = 0;
        };

        RECT itemRect(size_t x, size_t y, size_t i, POINT origin = {0, 0}) const;
        POINT measureMinEx() const override;
        POINT measureEx(POINT fit) const override;
        void drawEx(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const override;
        void onMouse(POINT p, int keys) override;
        void onMouseLeave() override;

    private:
        std::vector<widget_handle> cells;
        std::vector<std::pair<alignment, alignment>> align;
        std::vector<group> cols;
        std::vector<group> rows;
        widget* lastMouseEvent = nullptr;
    };

    // A widget that uses a rectangle from a single texture to draw itself.
    // The rectangle is split into 9 parts, like this:
    //
    //            stretchable
    //          <------------>
    //     +----+------------+----+
    //     |    |            |    |
    //     +----+------------+----+   ^
    //     |    |    inner   |    |   | stretchable
    //     +----+------------+----+   v
    //     |    |            |    |
    //     +----+------------+----+
    //
    // A child widget is drawn into the `inner` part.
    //
    // TODO forward mouse events within the child widget's rectangle.
    struct texrect : widget {
        texrect(widget& child)
            : contents(&child, *this)
        {}

    private:
        RECT getPadding() const {
            auto o = getOuter();
            auto i = getInner();
            return {i.left - o.left, i.top - o.top, o.right - i.right, o.bottom - i.bottom};
        }

        POINT measureMinEx() const {
            auto [pl, pt, pr, pb] = getPadding();
            auto [w, h] = contents->measureMin();
            return {w + pl + pr, h + pt + pb};
        }

        POINT measureEx(POINT fit) const {
            auto [pl, pt, pr, pb] = getPadding();
            auto [w, h] = contents->measure({fit.x - pl - pr, fit.y - pt - pb});
            return {w + pl + pr, h + pt + pb};
        }

        void drawEx(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const override;

    protected:
        virtual winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext&) const = 0;
        virtual RECT getOuter() const = 0;
        virtual RECT getInner() const = 0;

    private:
        widget_handle contents;
    };

    // A convenience object for widgets that capture the mouse pointer while a button
    // is held after hovering over the widget. As an example, consider the slider
    // (aka trackbar): when you start dragging it, you can move the pointer around
    // the screen and still have it update. Very convenient for touch screens.
    struct capture_state {
        // Call this in the widget's `onMouse` handler. Return values:
        //     ignore     hovering with a button held from outside
        //     prepare    hovering with no buttons held
        //     capture    pressed a button, call `captureMouse(true)`
        //     drag       still holding a button
        //     release    released all buttons, call `captureMouse(false)`
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

    // A rectangular push button:
    //
    //     +-------------------------------+
    //     | Why did I draw a button here? |
    //     +-------------------------------+
    //
    struct button : texrect {
        using texrect::texrect;

        void onMouse(POINT abs, int keys) override {
            bool hovering = rectHit(size(), relative(abs));
            switch (cap.update(keys)) {
                case capture_state::ignore:  break;
                case capture_state::prepare: setState(hover); break;
                case capture_state::capture: setState(active); captureMouse(true); break;
                case capture_state::drag:    setState(hovering ? active : idle); break;
                // Unconditionally switching to `idle` even when hovering over the button
                // gives a little feedback that the click was actually registered.
                case capture_state::release: setState(idle); captureMouse(false); if (hovering) onClick(); break;
            }
        }

        void onMouseLeave() override {
            cap.stop();
            setState(idle);
        }

    public:
        // Emitted when the left mouse button is released over this widget.
        util::event<> onClick;

    private:
        enum state { idle, hover, active };
        void setState(state s) {
            if (currentState != s)
                currentState = s, invalidate();
        }

    protected:
        winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext&) const override;
        RECT getOuter() const override;
        RECT getInner() const override;

    private:
        state currentState = idle;
        capture_state cap;
    };

    // Also known as a "trackbar", this is just a knob on a rail:
    //
    //     ----||--------------
    //
    // Values range from 0 to 1 inclusive.
    struct slider : widget {
        enum orientation { deg0, deg90, deg180, deg270 };

        // Change the orientation of the slider, in degrees clockwise.
        //
        // For 0 and 90 degrees, 0 is top-left and 1 is bottom-right. For 180 and 270,
        // the range is inverted.
        void setOrientation(orientation o) {
            currentOrientation = o;
            invalidateSize();
        }

        // Set the current value, clamping if out of bounds. Does not emit the event.
        void setValue(double v) {
            value = std::min(std::max(v, 0.0), 1.0);
            invalidate();
        }

        void onMouse(POINT abs, int keys) override {
            switch (cap.update(keys)) {
                case capture_state::ignore:
                case capture_state::prepare: return;
                case capture_state::capture: captureMouse(true); break;
                case capture_state::release: captureMouse(false); break;
            }
            auto [w, h] = measureMin();
            setValue(vertical() ? (relative(abs).y - h / 2.) / (size().y - h)
                                : (relative(abs).x - w / 2.) / (size().x - w));
            onChange(value);
        }

        void onMouseLeave() override {
            cap.stop();
        }

    public:
        // Emitted when the current value changes.
        util::event<double> onChange;

    private:
        bool vertical() const { return currentOrientation == deg90  || currentOrientation == deg270; }
        bool inverted() const { return currentOrientation == deg180 || currentOrientation == deg270; }
        POINT measureMinEx() const override;
        POINT measureEx(POINT fit) const override;
        void drawEx(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const override;

    private:
        double value = 0.0;
        orientation currentOrientation = deg0;
        capture_state cap;
    };
}
