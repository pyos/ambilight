#include "grid.hpp"
#include <algorithm>

static LONG alignAs(LONG outer, LONG inner, LONG globalCenter, ui::grid::alignment a) {
    switch (a) {
        default:
        case ui::grid::align_start:         return 0;
        case ui::grid::align_center:        return (outer - inner) / 2;
        case ui::grid::align_global_center: return std::max(0L, std::min(outer - inner, globalCenter - inner / 2));
        case ui::grid::align_end:           return (outer - inner);
    }
}

RECT ui::grid::itemRect(size_t x, size_t y, size_t i, POINT origin) const {
    auto [tw, th] = size();
    auto [w, h] = cells[i]->measure({cols[x].size, rows[y].size});
    origin.x += cols[x].start + alignAs(cols[x].size, w, tw / 2 - cols[x].start, align[i].first);
    origin.y += rows[y].start + alignAs(rows[y].size, h, th / 2 - cols[x].start, align[i].second);
    return {origin.x, origin.y, origin.x + w, origin.y + h};
}

POINT ui::grid::measureMinImpl() const {
    for (auto& c : cols) c.min = 0;
    for (auto& r : rows) r.min = 0;
    for (size_t y = 0, i = 0; y < rows.size(); y++) {
        for (size_t x = 0; x < cols.size(); x++, i++) if (cells[i]) {
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

POINT ui::grid::measureImpl(POINT fit) const {
    auto allocateRemaining = [this](const std::vector<group>& m, LONG space) {
        LONG total = 0;
        for (auto& it : m) if ((it.stretch = it.weight))
            total += it.stretch;
        for (auto& it : m) if (!it.stretch)
            space -= it.size = it.min;
        for (bool changed = true; changed; ) {
            changed = false;
            for (auto& it : m) if (it.stretch) {
                if (space < 0 || (it.size = space * it.stretch / total) <= it.min) {
                    total -= it.stretch;
                    space -= it.size = it.min;
                    it.stretch = 0;
                    changed = true;
                }
            }
        }
        for (auto& it : m) if (it.stretch)
            space -= it.size;
        return std::max(0L, space);
    };
    // Assume `min` in both `c` and `r` is valid from a previous call to `measureMin`.
    POINT remaining = {allocateRemaining(cols, fit.x), allocateRemaining(rows, fit.y)};
    if (remaining.x || remaining.y) {
        if (primary.first < cols.size() && primary.second < rows.size()) {
            auto& c = cols[primary.first];
            auto& r = rows[primary.second];
            POINT extended = {remaining.x + c.size, remaining.y + r.size};
            if (auto& item = cells[primary.first + primary.second * cols.size()])
                extended = item->measure(extended);
            c.size = std::max(c.size, extended.x);
            r.size = std::max(r.size, extended.y);
        } else {
            // Try to find a stretchy row & column then. (This is still possible even if
            // there is unallocated space because of rounding errors during allocation.)
            auto cmp = [](auto& a, auto& b) { return a.stretch < b.stretch; };
            if (auto c = std::max_element(cols.begin(), cols.end(), cmp); c != cols.end() && c->stretch)
                c->size += remaining.x;
            if (auto r = std::max_element(rows.begin(), rows.end(), cmp); r != rows.end() && r->stretch)
                r->size += remaining.y;
        }
    }
    // Now that all sizes are finally set, we can compute the start offsets.
    LONG cs = 0, rs = 0;
    for (auto& c : cols) c.start = cs, cs += c.size;
    for (auto& r : rows) r.start = rs, rs += r.size;
    return {cols.back().start + cols.back().size, rows.back().start + rows.back().size};
}

void ui::grid::drawImpl(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const {
    // Fill `c` and `r` with the correct values for `start` and `end`.
    measure({total.right - total.left, total.bottom - total.top});
    for (size_t y = 0, i = 0; y < rows.size(); y++) {
        for (size_t x = 0; x < cols.size(); x++, i++) if (cells[i]) {
            auto itemArea = itemRect(x, y, i, {total.left, total.top});
            auto itemDirty = rectIntersection(itemArea, dirty);
            // Due to the row/column sizing rules, repainting one item never makes it necessary
            // to repaint another, so the total dirty area never changes.
            if (itemDirty.left < itemDirty.right && itemDirty.top < itemDirty.bottom)
                cells[i]->draw(ctx, target, itemArea, itemDirty);
        }
    }
}

bool ui::grid::onMouse(POINT abs, int keys) {
    widget* target = nullptr;
    auto rel = relative(abs);
    if (cols[0].start <= rel.x && rows[0].start <= rel.y) {
        auto ci = std::upper_bound(cols.begin(), cols.end(), rel.x, [](LONG x, auto& c) { return x < c.start; });
        auto ri = std::upper_bound(rows.begin(), rows.end(), rel.y, [](LONG y, auto& r) { return y < r.start; });
        size_t x = ci - cols.begin() - 1, y = ri - rows.begin() - 1, i = y * cols.size() + x;
        if ((target = cells[i].get()) && !rectHit(itemRect(x, y, i), rel))
            target = nullptr; // Hit the cell, but not the widget.
    }
    if (lastMouseEvent && lastMouseEvent != target)
        lastMouseEvent->onMouseLeave();
    lastMouseEvent = target;
    return lastMouseEvent && lastMouseEvent->onMouse(abs, keys);
}

void ui::grid::onMouseLeave() {
    if (lastMouseEvent)
        lastMouseEvent->onMouseLeave();
    lastMouseEvent = nullptr;
}
