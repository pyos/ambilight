#pragma once

#include "../widget.hpp"

namespace ui {
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
    struct texrect : widget {
        texrect() = default;
        texrect(widget& child)
            : contents(&child, *this)
        {}

        void setContents(widget* w) {
            contents = widget_handle(w, *this);
            invalidateSize();
        }

        void onChildRelease(widget& w) override {
            // assert(contents.get() == &w);
            setContents(nullptr);
        }

        bool onMouse(POINT abs, int keys) override {
            if (!contents)
                return false;
            auto [w, h] = size();
            auto [pl, pt, pr, pb] = getPadding();
            if (!rectHit(RECT{pl, pt, w - pr, h - pb}, relative(abs)))
                return false;
            forwardMouse = true;
            return contents->onMouse(abs, keys);
        }

        void onMouseLeave() override {
            if (contents && forwardMouse)
                contents->onMouseLeave();
            forwardMouse = false;
        }

    private:
        RECT getPadding() const {
            auto o = getOuter();
            auto i = getInner();
            return {i.left - o.left, i.top - o.top, o.right - i.right, o.bottom - i.bottom};
        }

        POINT measureMinImpl() const {
            auto [pl, pt, pr, pb] = getPadding();
            auto [w, h] = contents ? contents->measureMin() : POINT{0, 0};
            return {w + pl + pr, h + pt + pb};
        }

        POINT measureImpl(POINT fit) const {
            auto [pl, pt, pr, pb] = getPadding();
            auto [w, h] = contents ? contents->measure({fit.x - pl - pr, fit.y - pt - pb}) : POINT{0, 0};
            return {w + pl + pr, h + pt + pb};
        }

        void drawImpl(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const override;

    protected:
        static RECT middleOf(RECT r) {
            POINT p = {(r.left + r.right) / 2, (r.top + r.bottom) / 2};
            return {p.x, p.y, p.x, p.y};
        }

        virtual winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext&) const = 0;
        virtual RECT getOuter() const = 0;
        virtual RECT getInner() const { return middleOf(getOuter()); }

    private:
        widget_handle contents;
        bool forwardMouse = false;
    };

    // Like a texrect, but with a constant texture.
    template <winapi::com_ptr<ID3D11Texture2D> (*F)(ui::dxcontext&)>
    struct image : texrect {
        image(RECT outer) : outer(outer), inner(middleOf(outer)) {}
        image(RECT outer, RECT inner) : outer(outer), inner(inner) {}
        image(RECT outer, RECT inner, widget& contents) : texrect(contents), outer(outer), inner(inner) {}

    private:
        winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext& ctx) const override {
            return ctx.cachedTexture<F>(); }
        RECT getOuter() const override { return outer; }
        RECT getInner() const override { return inner; }
        RECT outer, inner;
    };
}
