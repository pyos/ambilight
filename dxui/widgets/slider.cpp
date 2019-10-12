#include "slider.hpp"
#include "data.hpp"

POINT ui::slider::measureMinImpl() const {
    auto ts = builtinRect(SLIDER_TRACK);
    return {vertical() ? ts.bottom - ts.top : ts.right - ts.left,
            vertical() ? ts.right - ts.left : ts.bottom - ts.top};
}

POINT ui::slider::measureImpl(POINT fit) const {
    auto ts = builtinRect(SLIDER_TRACK);
    return {vertical() ? ts.bottom - ts.top : fit.x,
            vertical() ? fit.y : ts.bottom - ts.top};
}

winapi::com_ptr<ID3D11Texture2D> ui::slider::getTexture(ui::dxcontext& ctx) const {
    return ctx.cachedTexture<builtinTexture>();
}

RECT ui::slider::getTrack() const {
    return builtinRect(SLIDER_TRACK);
}

RECT ui::slider::getGroove() const {
    return builtinRect(SLIDER_GROOVE);
}

void ui::slider::drawImpl(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const {
    auto gs = getGroove();
    auto ts = getTrack();
    auto [tw, th] = measureMin();            // Horizontal:          Vertical:
    auto aw = total.right - total.left - tw; // Groove length        Track left offset
    auto ah = total.bottom - total.top - th; // Track top offset     Groove length
    bool rotate = vertical();
    auto gw = rotate ? gs.bottom - gs.top : aw; // In vertical direction, the groove texture is rotated,
    auto gh = rotate ? ah : gs.bottom - gs.top; // which is why both of these have (bottom - top) cases.
    auto gx = total.left + (rotate ? total.right - total.left - gw : tw) / 2;
    auto gy = total.top  + (rotate ? th : total.bottom - total.top - gh) / 2;
    auto tx = total.left + (rotate ? aw / 2 : (LONG)(aw * value));
    auto ty = total.top  + (rotate ? (LONG)(ah * value) : ah / 2);
    if (inverted()) {
        // Transform to the bottom-right corner + a vector towards the left-top corner.
        gx += gw, gy += gh, gw = -gw, gh = -gh;
        tx += tw, ty += th, tw = -tw, th = -th;
    }
    ui::vertex hquads[] = {
        QUADP(gx, gy, gx + gw, gy + gh, 0, gs.left, gs.top, gs.right, gs.bottom),
        QUADP(tx, ty, tx + tw, ty + th, 0, ts.left, ts.top, ts.right, ts.bottom),
    };
    ui::vertex vquads[] = {
        QUADPR(gx, gy, gx + gw, gy + gh, 0, gs.left, gs.top, gs.right, gs.bottom),
        QUADPR(tx, ty, tx + tw, ty + th, 0, ts.left, ts.top, ts.right, ts.bottom),
    };
    ctx.draw(target, getTexture(ctx), rotate ? vquads : hquads, dirty);
}
