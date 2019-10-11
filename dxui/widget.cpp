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

bool ui::grid::onMouse(POINT abs, int keys) {
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
    return lastMouseEvent && lastMouseEvent->onMouse(abs, keys);
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

template <typename T, typename F>
static util::span<T> strip(util::span<T> in, F&& discard) {
    auto a = in.begin();
    auto b = in.end();
    while (a < b && discard(a[0])) a++;
    while (a < b && discard(b[-1])) b--;
    return util::span<T>(a, b - a);
}

template <typename T, typename F /* = void(size_t, util::span<T>) */>
static void splitString(util::span<T> string, T sep, size_t limit, F&& f) {
    auto it = string.begin();
    for (size_t i = 0;; i++) {
        auto next = i + 1 == limit ? string.end() : std::find(it, string.end(), sep);
        f(i, util::span<T>(it, next - it));
        if (next == string.end())
            break;
        it = next + 1;
    }
}

template <size_t columns, typename F /* = void(size_t, util::span<const char>[columns]) */>
static void readPseudoCSV(util::span<const char> resource, F&& f) {
    size_t skipped = 0;
    splitString<const char>(resource, '\n', 0, [&](size_t lineno, util::span<const char> line) {
        // Ignore empty lines and comments (starting with whitespace and then `#`).
        if (std::all_of(line.begin(), line.end(), [](char c) { return isspace(c); }) || line[0] == '#') {
            skipped++;
            return;
        }
        util::span<const char> stripped[columns] = {};
        splitString<const char>(line, ',', columns, [&](size_t i, util::span<const char> part) {
            stripped[i] = strip(part, [](char c) { return isspace(c); });
        });
        f(lineno - skipped, stripped);
    });
}

enum builtin_rect {
    BUTTON_OUTER,
    BUTTON_OUTER_HOVER,
    BUTTON_OUTER_ACTIVE,
    BUTTON_INNER,
    BUTTON_INNER_HOVER,
    BUTTON_INNER_ACTIVE,
    SLIDER_TRACK,
    SLIDER_GROOVE,
    BUILTIN_RECT_COUNT
};

static RECT builtinRect(builtin_rect r) {
    static RECT rects[BUILTIN_RECT_COUNT];
    static int initialized = [&]{
        auto resource = ui::read(ui::fromBundled(IDD_WIDGETS_RECTS), L"TEXT");
        if (!resource)
            throw std::runtime_error("IDD_WIDGETS_RECTS TEXT-type resource could nont be loaded");
        std::unordered_map<std::string_view, RECT> map;
        // x, y, width, height, name with optional whitespace
        readPseudoCSV<5>(resource.reinterpret<const char>(), [&](size_t, util::span<const char> columns[5]) {
            POINT p = {atol(columns[0].data()), atol(columns[1].data())};
            POINT q = {atol(columns[2].data()), atol(columns[3].data())};
            std::string_view name{columns[4].data(), columns[4].size()};
            map.emplace(name, RECT{p.x, p.y, p.x + q.x, p.y + q.y});
        });
        rects[BUTTON_OUTER]        = map.at("button outer");
        rects[BUTTON_OUTER_HOVER]  = map.at("button outer hover");
        rects[BUTTON_OUTER_ACTIVE] = map.at("button outer active");
        rects[BUTTON_INNER]        = map.at("button inner");
        rects[BUTTON_INNER_HOVER]  = map.at("button inner hover");
        rects[BUTTON_INNER_ACTIVE] = map.at("button inner active");
        rects[SLIDER_TRACK]        = map.at("slider track");
        rects[SLIDER_GROOVE]       = map.at("slider groove");
        return 0;
    }();
    return rects[r];
};

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
        default:     return builtinRect(BUTTON_OUTER);
        case hover:  return builtinRect(BUTTON_OUTER_HOVER);
        case active: return builtinRect(BUTTON_OUTER_ACTIVE);
    }
}

RECT ui::button::getInner() const {
    switch (currentState) {
        default:     return builtinRect(BUTTON_INNER);
        case hover:  return builtinRect(BUTTON_INNER_HOVER);
        case active: return builtinRect(BUTTON_INNER_ACTIVE);
    }
}

POINT ui::slider::measureMinEx() const {
    auto ts = builtinRect(SLIDER_TRACK);
    return {vertical() ? ts.bottom - ts.top : ts.right - ts.left,
            vertical() ? ts.right - ts.left : ts.bottom - ts.top};
}

POINT ui::slider::measureEx(POINT fit) const {
    auto ts = builtinRect(SLIDER_TRACK);
    return {vertical() ? ts.bottom - ts.top : fit.x,
            vertical() ? fit.y : ts.bottom - ts.top};
}

void ui::slider::drawEx(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const {
    auto gs = builtinRect(SLIDER_GROOVE);
    auto ts = builtinRect(SLIDER_TRACK);
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

int ui::font::nativeSize() const {
    (*this)[0];
    return nativeSize_;
}

const ui::font_symbol& ui::font::operator[](wchar_t point) const {
    static const ui::font_symbol empty = {};
    if (ascii.empty()) {
        ascii.resize(256);
        readPseudoCSV<8>(charmap, [&](size_t lineno, util::span<const char> columns[8]) {
            if (lineno == 0) {
                nativeSize_ = atol(columns[1].data());
            } else if (uint32_t cp = atol(columns[0].data()); cp < 256) {
                auto& c = ascii[cp];
                c.x = atoi(columns[1].data());
                c.y = atoi(columns[2].data());
                c.w = atoi(columns[3].data());
                c.h = atoi(columns[4].data());
                c.originX = atoi(columns[5].data());
                c.originY = atoi(columns[6].data());
                c.advance = atoi(columns[7].data());
            }
        });
    }
    return point < 256 ? ascii[point] : empty;
}

POINT ui::label::measureMinEx() const {
    POINT ret = {0, 0};
    long x = 0;
    long y = 0;
    long originX = 0;
    splitString<const wchar_t>(text, L'\n', 0, [&](size_t, util::span<const wchar_t> line) {
        long w = 0;
        if (line) {
            // Align the text so that there is a straight vertical line going through
            // each text line's first character's origin.
            originX = std::max<long>(originX, (*font)[line[0]].originX);
            for (wchar_t c : line) w += (*font)[c].advance;
            // Reserve some space on the right for the last character's horizontal overflow.
            auto& b = (*font)[line[line.size() - 1]];
            w += b.w - b.advance - b.originX;
        }
        x  = std::max(x, w);
        y += font->nativeSize() * lineHeight;
    });
    origin = {originX, (LONG)(font->nativeSize() * lineHeight / 4)};
    double scale = (double)fontSize / font->nativeSize();
    return {(LONG)((x + originX) * scale + 0.5), (LONG)(y * scale + 0.5)};
}

void ui::label::drawEx(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const {
    measureMin(); // Make sure the baseline is set to the correct value.
    std::vector<ui::vertex> quads;
    quads.reserve(text.size() * 6);
    // TODO use DirectWrite or something.
    double scale = (double)fontSize / font->nativeSize();
    long y = -origin.y;
    splitString<const wchar_t>(text, L'\n', 0, [&](size_t, util::span<const wchar_t> line) {
        y += font->nativeSize() * lineHeight;
        if (auto cs = strip(line, [](wchar_t c) { return c >= 256; })) {
            long x = origin.x;
            for (wchar_t c : cs) {
                auto& info = (*font)[c];
                RECT s = {info.x, info.y, info.x + info.w, info.y + info.h};
                double tl = (x - info.originX) * scale + total.left
                     , tt = (y - info.originY) * scale + total.top
                     , tr = (x - info.originX + info.w) * scale + total.left
                     , tb = (y - info.originY + info.h) * scale + total.top;
                ui::vertex quad[] = {QUADP(tl, tt, tr, tb, 0, s.left, s.top, s.right, s.bottom)};
                quads.insert(quads.end(), std::begin(quad), std::end(quad));
                x += info.advance;
            }
        }
    });
    // TODO textured color
    // TODO multiple fonts, colors, and sizes within a single text
    for (auto& vertex : quads)
        vertex.clr = ARGB2CLR(fontColor);
    ctx.draw(target, font->loadTexture(ctx), quads, dirty, true);
}
