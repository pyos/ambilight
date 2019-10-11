#pragma once

#include "../widget.hpp"

namespace ui {
    // An empty space around a widget or by itself.
    //
    // NOTE: when wrapping another widget, the padding is split in half.
    //       If it is not an even number, the extra pixel is added to
    //       bottom and right.
    //
    struct spacer : widget {
        spacer(POINT size) : size_(size) { }
        spacer(POINT size, widget& widget) : size_(size), child(&widget, *this) {
        }

        POINT size() { return size_; }

        void setSize(POINT newSize) {
            size_ = newSize;
            invalidateSize();
        }

        void setChild(widget* w) {
            child = widget_handle(w, *this);
        }

        bool onMouse(POINT abs, int keys) override {
            if (!child)
                return false;
            auto [x, y] = relative(abs);
            auto [w, h] = widget::size();
            if (!rectHit(POINT{w - size_.x, h - size_.y}, {x - size_.x / 2, y - size_.y / 2}))
                return false;
            mouseInChild = true;
            return child->onMouse(abs, keys);
        }

        void onMouseLeave() override {
            if (child && mouseInChild)
                child->onMouseLeave();
            mouseInChild = false;
        }

        void onChildRelease(widget& w) override {
            // assert(child.get() == &w);
            setChild(nullptr);
        }

    private:
        POINT measureMinEx() const override {
            auto [w, h] = child ? child->measureMin() : POINT{0, 0};
            return {w + size_.x, h + size_.y};
        }

        POINT measureEx(POINT fit) const override {
            auto [w, h] = child ? child->measure({fit.x - size_.x, fit.y - size_.y}) : POINT{0, 0};
            return {w + size_.x, h + size_.y};
        }

        void drawEx(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const override {
            if (child) {
                POINT h = {size_.x / 2, size_.y / 2};
                RECT r = {total.left + h.x, total.top + h.y,
                          total.right - size_.x + h.x, total.bottom - size_.y + h.y};
                child->draw(ctx, target, r, rectIntersection(dirty, r));
            }
        }

        POINT size_;
        widget_handle child;
        bool mouseInChild = false;
    };
}
