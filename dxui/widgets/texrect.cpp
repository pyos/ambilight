#include "texrect.hpp"

void ui::texrect::drawEx(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const {
    auto [ol, ot, or, ob] = getOuter();
    auto [il, it, ir, ib] = getInner();
    auto pl = il - ol, pt = it - ot, pr = or - ir, pb = ob - ib;
    auto [cw, ch] = contents->measure({total.right - total.left - pl - pr, total.bottom - total.top - pt - pb});
    POINT p0 = {total.left, total.top};  //  0--+--+--+
    POINT p1 = {p0.x + pl, p0.y + pt};   //  +--1--+--+
    POINT p2 = {p1.x + cw, p1.y + ch};   //  +--+--2--+
    POINT p3 = {p2.x + pr, p2.y + pb};   //  +--+--+--3
    ui::vertex vs[] = {
        QUADP(p0.x, p0.y, p1.x, p1.y, 0, ol, ot, il, it), // top-left
        QUADP(p1.x, p0.y, p2.x, p1.y, 0, il, ot, ir, it), // top
        QUADP(p2.x, p0.y, p3.x, p1.y, 0, ir, ot, or, it), // top-right
        QUADP(p0.x, p1.y, p1.x, p2.y, 0, ol, it, il, ib), // left
        QUADP(p1.x, p1.y, p2.x, p2.y, 0, il, it, ir, ib), // center
        QUADP(p2.x, p1.y, p3.x, p2.y, 0, ir, it, or, ib), // right
        QUADP(p0.x, p2.y, p1.x, p3.y, 0, ol, ib, il, ob), // bottom-left
        QUADP(p1.x, p2.y, p2.x, p3.y, 0, il, ib, ir, ob), // bottom
        QUADP(p2.x, p2.y, p3.x, p3.y, 0, ir, ib, or, ob), // bottom-right
    };
    ctx.draw(target, getTexture(ctx), vs, dirty);
    contents->draw(ctx, target, {p1.x, p1.y, p2.x, p2.y}, rectIntersection(dirty, {p1.x, p1.y, p2.x, p2.y}));
}
