#include "data.hpp"
#include "texrect.hpp"

winapi::com_ptr<ID3D11Texture2D> ui::texrect::getTexture(ui::dxcontext& ctx) const {
    return ctx.cachedTexture<builtinTexture>();
}

void ui::texrect::drawImpl(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const {
    auto [al, at, ar, ab] = getOuter();
    auto [il, it, ir, ib] = getInner();
    auto pl = il - al, pt = it - at, pr = ar - ir, pb = ab - ib;
    auto [cw, ch] = contents
        ? contents->measure({total.right - total.left - pl - pr, total.bottom - total.top - pt - pb})
        : POINT{0, 0};
    POINT p0 = {total.left, total.top};  //  0--+--+--+
    POINT p1 = {p0.x + pl, p0.y + pt};   //  +--1--+--+
    POINT p2 = {p1.x + cw, p1.y + ch};   //  +--+--2--+
    POINT p3 = {p2.x + pr, p2.y + pb};   //  +--+--+--3
    ui::vertex vs[] = {
        QUADP(p0.x, p0.y, p1.x, p1.y, 0, al, at, il, it), // top-left
        QUADP(p1.x, p0.y, p2.x, p1.y, 0, il, at, ir, it), // top
        QUADP(p2.x, p0.y, p3.x, p1.y, 0, ir, at, ar, it), // top-right
        QUADP(p0.x, p1.y, p1.x, p2.y, 0, al, it, il, ib), // left
        QUADP(p1.x, p1.y, p2.x, p2.y, 0, il, it, ir, ib), // center
        QUADP(p2.x, p1.y, p3.x, p2.y, 0, ir, it, ar, ib), // right
        QUADP(p0.x, p2.y, p1.x, p3.y, 0, al, ib, il, ab), // bottom-left
        QUADP(p1.x, p2.y, p2.x, p3.y, 0, il, ib, ir, ab), // bottom
        QUADP(p2.x, p2.y, p3.x, p3.y, 0, ir, ib, ar, ab), // bottom-right
    };
    if (auto texture = getTexture(ctx))
        ctx.draw(target, texture, vs, dirty);
    if (contents)
        contents->draw(ctx, target, {p1.x, p1.y, p2.x, p2.y}, rectIntersection(dirty, {p1.x, p1.y, p2.x, p2.y}));
}
