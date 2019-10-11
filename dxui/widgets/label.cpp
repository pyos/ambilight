#include "data.hpp"
#include "label.hpp"

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
    double tx = 0, ty = 0;
    double sx = 0, ex = 0;
    split<const ui::text_part>(data, [](auto& c) { return c.breakAfter; }, 0, [&](size_t, util::span<const ui::text_part> parts) {
        bool firstInLine = true;
        double lx = 0, ly = 0;
        for (const ui::text_part& part : parts) {
            if (part.data) {
                double scale = (double)part.fontSize / part.font.nativeSize();
                for (wchar_t c : part.data)
                    lx += part.font[c].advance * scale;
                auto& last = part.font[part.data[part.data.size() - 1]];
                // Horizontally align lines so that there is a straight vertical line
                // going through the origins of the first character of each.
                sx = std::max(sx, scale * part.font[part.data[0]].originX * firstInLine);
                // Reserve some space for the last character's right edge.
                ex = scale * (last.w - last.advance - last.originX);
                firstInLine = false;
            }
            ly = std::max(ly, part.fontSize * lineHeight);
        }
        tx = std::max(lx + ex, tx), ty += ly;
    });
    originX = sx;
    return {(LONG)(sx + tx) + 1, (LONG)ty + 1};
}

void ui::label::drawEx(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const {
    measureMin(); // Make sure the origin X and the character count are set to the correct values.
    std::vector<ui::vertex> quads;
    if (!data.empty())
        quads.reserve(std::max_element(data.begin(), data.end(), [](auto& a, auto& b) {
            return a.data.size() < b.data.size(); })->data.size());
    // TODO use DirectWrite or something.
    double y = 0;
    split<const ui::text_part>(data, [](auto& c) { return c.breakAfter; }, 0, [&](size_t, util::span<const ui::text_part> parts) {
        double x = originX;
        double ly = std::max_element(parts.begin(), parts.end(), [](auto& a, auto& b) {
            return a.fontSize < b.fontSize; })->fontSize * lineHeight;
        y += ly * 0.75;
        for (const ui::text_part& part : parts) {
            quads.clear();
            double scale = (double)part.fontSize / part.font.nativeSize();
            for (wchar_t c : part.data) {
                auto& info = part.font[c];
                double tl = x - (info.originX) * scale + total.left
                     , tt = y - (info.originY) * scale + total.top
                     , tr = x - (info.originX - info.w) * scale + total.left
                     , tb = y - (info.originY - info.h) * scale + total.top;
                ui::vertex quad[] = {QUADP(tl, tt, tr, tb, 0, info.x, info.y, info.x + info.w, info.y + info.h)};
                quads.insert(quads.end(), std::begin(quad), std::end(quad));
                x += scale * info.advance;
            }
            for (auto& vertex : quads)
                vertex.clr = ARGB2CLR(part.fontColor);
            ctx.draw(target, part.font.loadTexture(ctx), quads, dirty, true);
        }
        y += ly * 0.25;
    });
}
