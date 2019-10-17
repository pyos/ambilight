#include "slider.hpp"

void ui::slider::drawImpl(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const {
    auto ts = getTrack(), gs = getGroove(), fs = getFilled();
    auto [tw, th] = measureMin();            // Horizontal:          Vertical:
    auto aw = total.right - total.left - tw; // Groove length        Track left offset
    auto ah = total.bottom - total.top - th; // Track top offset     Groove length
    bool rotate = vertical();
    auto gw = rotate ? gs.bottom - gs.top : aw; // In vertical direction, the groove texture is rotated,
    auto gh = rotate ? ah : gs.bottom - gs.top; // which is why both of these have (bottom - top) cases.
    auto fw = rotate ? fs.bottom - fs.top : (LONG)(aw * value_);
    auto fh = rotate ? (LONG)(ah * value_) : fs.bottom - fs.top;
    auto fx = total.left + (rotate ? total.right - total.left - fw : tw) / 2;
    auto fy = total.top  + (rotate ? th : total.bottom - total.top - fh) / 2;
    auto gx = total.left + (rotate ? total.right - total.left - gw : tw) / 2;
    auto gy = total.top  + (rotate ? th : total.bottom - total.top - gh) / 2;
    auto tx = total.left + (rotate ? aw / 2 : fw);
    auto ty = total.top  + (rotate ? fh : ah / 2);
    if (inverted()) {
        // Transform to the bottom-right corner + a vector towards the left-top corner.
        gx += gw, gy += gh, gw = -gw, gh = -gh;
        tx += tw, ty += th, tw = -tw, th = -th;
    }
    ui::vertex hquads[] = {
        QUADP(gx, gy, gx + gw, gy + gh, 0, gs.left, gs.top, gs.right, gs.bottom),
        QUADP(fx, fy, fx + fw, fy + fh, 0, fs.left, fs.top, fs.right, fs.bottom),
        QUADP(tx, ty, tx + tw, ty + th, 0, ts.left, ts.top, ts.right, ts.bottom),
    };
    ui::vertex vquads[] = {
        QUADPR(gx, gy, gx + gw, gy + gh, 0, gs.left, gs.top, gs.right, gs.bottom),
        QUADPR(fx, fy, fx + fw, fy + fh, 0, fs.left, fs.top, fs.right, fs.bottom),
        QUADPR(tx, ty, tx + tw, ty + th, 0, ts.left, ts.top, ts.right, ts.bottom),
    };
    ctx.draw(target, getTexture(ctx), rotate ? vquads : hquads, dirty);
}
