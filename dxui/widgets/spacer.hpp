#pragma once

#include "texrect.hpp"

namespace ui {
    // An empty space around a widget or by itself.
    struct spacer : texrect {
        spacer(POINT size) : pad(split(size)) { }
        spacer(POINT size, widget& widget) : texrect(widget), pad(split(size)) {}
        spacer(RECT padding, widget& widget) : texrect(widget), pad(padding) {}

        RECT padding() { return pad; }

        void setPadding(RECT padding) { pad = padding; invalidateSize(); }

        // Set padding to half the provided size from opposite sides.
        void setTotalPadding(POINT total) { setPadding(split(total)); }

        // Set padding to half the provided size from each side.
        void setTotalPadding(LONG value) { setTotalPadding({value, value}); }

    private:
        static RECT split(POINT p) { return {p.x / 2, p.y / 2, (p.x + 1) / 2, (p.y + 1) / 2}; }

    private:
        winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext&) const override { return {}; }
        RECT getOuter() const override { return {0, 0, pad.left + pad.right, pad.top + pad.bottom}; }
        RECT getInner() const override { return {pad.left, pad.top, pad.left, pad.top}; }
        RECT pad;
    };
}
