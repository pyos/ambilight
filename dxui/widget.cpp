#include "resource.hpp"
#include "widget.hpp"

#include "../resource.h"

static LONG alignAs(LONG outer, LONG inner, ui::grid::alignment a) {
    switch (a) {
        default:
        case ui::grid::align_start:  return 0;
        case ui::grid::align_center: return (outer - inner) / 2;
        case ui::grid::align_end:    return (outer - inner);
    }
}

RECT ui::grid::itemRect(size_t x, size_t y, size_t i, POINT origin) const {
    auto [w, h] = cells[i]->measure({cols[x].size, rows[y].size});
    origin.x += cols[x].start + alignAs(cols[x].size, w, align[i].first);
    origin.y += rows[y].start + alignAs(rows[y].size, h, align[i].second);
    return {origin.x, origin.y, origin.x + w, origin.y + h};
}

POINT ui::grid::measureMinEx() const {
    for (auto& c : cols) c.min = 0;
    for (auto& r : rows) r.min = 0;
    for (size_t x = 0, i = 0; x < cols.size(); x++) {
        for (size_t y = 0; y < rows.size(); y++, i++) if (cells[i]) {
            auto [w, h] = cells[i]->measureMin();
            cols[x].min = std::max(cols[x].min, w);
            rows[y].min = std::max(rows[y].min, h);
        }
    }
    POINT res = {0, 0};
    for (auto& c : cols) res.x += c.min;
    for (auto& r : rows) res.y += r.min;
    return res;
}

POINT ui::grid::measureEx(POINT fit) const {
    auto allocateRemaining = [this](const std::vector<group>& m, LONG space) {
        uint64_t total = 0;
        for (auto& it : m) {
            if ((it.stretch = it.weight))
                total += it.stretch;
            else
                space -= it.size = it.min;
        }
        for (bool changed = true; total != 0 && changed; ) {
            LONG consumed = 0;
            changed = false;
            for (auto& it : m) {
                it.start = consumed;
                if (it.stretch && (it.size = space < 0 ? 0 : space * it.stretch / total) < it.min) {
                    total -= it.stretch;
                    space -= it.size = it.min;
                    it.stretch = 0;
                    changed = true;
                }
                consumed += it.size;
            }
        }
    };
    // Assume `min` in both `c` and `r` is valid from a previous call to `measureMin`.
    allocateRemaining(cols, fit.x);
    allocateRemaining(rows, fit.y);
    return {cols.back().start + cols.back().size, rows.back().start + rows.back().size};
}

void ui::grid::drawEx(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const {
    // Fill `c` and `r` with the correct values for `start` and `end`.
    measure({total.right - total.left, total.bottom - total.top});
    for (size_t x = 0, i = 0; x < cols.size(); x++) {
        for (size_t y = 0; y < rows.size(); y++, i++) if (cells[i]) {
            auto itemArea = itemRect(x, y, i, {total.left, total.top});
            auto itemDirty = rectIntersection(itemArea, dirty);
            // Due to the row/column sizing rules, repainting one item never makes it necessary
            // to repaint another, so the total dirty area never changes.
            if (itemDirty.left < itemDirty.right && itemDirty.top < itemDirty.bottom)
                cells[i]->draw(ctx, target, itemArea, itemDirty);
        }
    }
}

void ui::grid::onMouse(POINT abs, int keys) {
    widget* target = nullptr;
    auto rel = relative(abs);
    if (cols[0].start <= rel.x || rows[0].start <= rel.y) {
        auto ci = std::upper_bound(cols.begin(), cols.end(), rel.x, [](LONG x, auto& c) { return x < c.start; });
        auto ri = std::upper_bound(rows.begin(), rows.end(), rel.y, [](LONG y, auto& r) { return y < r.start; });
        auto x = std::distance(cols.begin(), ci) - 1;
        auto y = std::distance(rows.begin(), ri) - 1;
        if ((target = cells[x * rows.size() + y].get()))
            if (!rectHit(itemRect(x, y, x * rows.size() + y), rel))
                target = nullptr; // Hit the cell, but not the widget.
    }
    if (lastMouseEvent && lastMouseEvent != target)
        lastMouseEvent->onMouseLeave();
    lastMouseEvent = target;
    if (lastMouseEvent)
        lastMouseEvent->onMouse(abs, keys);
}

void ui::grid::onMouseLeave() {
    if (lastMouseEvent)
        lastMouseEvent->onMouseLeave();
    lastMouseEvent = nullptr;
}

static winapi::com_ptr<ID3D11Texture2D> builtinTexture(ui::dxcontext& ctx) {
    auto resource = ui::read(ui::fromBundled(IDI_WIDGETS), L"PNG");
    if (!resource)
        throw std::runtime_error("IDI_WIDGETS PNG-type resource could not be loaded");
    return ctx.textureFromPNG(resource);
}

static constexpr RECT
    BUTTON_OUTER_NORMAL = { 0,  0, 23, 23},
    BUTTON_INNER_NORMAL = {11, 11, 12, 12},
    BUTTON_OUTER_HOVER  = { 0, 23, 23, 46},
    BUTTON_INNER_HOVER  = {11, 34, 12, 35},
    BUTTON_OUTER_ACTIVE = { 0, 46, 23, 69},
    BUTTON_INNER_ACTIVE = {11, 57, 12, 58},
    SLIDER_TRACK  = {23,  0, 38, 32},
    SLIDER_GROOVE = {23, 32, 38, 35};

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

winapi::com_ptr<ID3D11Texture2D> ui::button::getTexture(ui::dxcontext& ctx) const {
    return ctx.cachedTexture<builtinTexture>();
}

RECT ui::button::getOuter() const {
    switch (currentState) {
        default: return BUTTON_OUTER_NORMAL;
        case hover: return BUTTON_OUTER_HOVER;
        case active: return BUTTON_OUTER_ACTIVE;
    }
}

RECT ui::button::getInner() const {
    switch (currentState) {
        default: return BUTTON_INNER_NORMAL;
        case hover: return BUTTON_INNER_HOVER;
        case active: return BUTTON_INNER_ACTIVE;
    }
}

POINT ui::slider::measureMinEx() const {
    auto w = SLIDER_TRACK.right - SLIDER_TRACK.left;
    auto h = SLIDER_TRACK.bottom - SLIDER_TRACK.top;
    return vertical() ? POINT{h, w} : POINT{w, h};
}

POINT ui::slider::measureEx(POINT fit) const {
    auto h = SLIDER_TRACK.bottom - SLIDER_TRACK.top;
    return vertical() ? POINT{h, fit.y} : POINT{fit.x, h};
}

void ui::slider::drawEx(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const {
    RECT gs = SLIDER_GROOVE;
    RECT ts = SLIDER_TRACK;
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
    ctx.draw(target, ctx.cachedTexture<builtinTexture>(), rotate ? vquads : hquads, dirty);
}
