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
    // TODO forward mouse events within the child widget's rectangle.
    //
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
}
