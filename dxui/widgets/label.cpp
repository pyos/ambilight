#include "data.hpp"
#include "label.hpp"

ui::font::font(util::span<const uint8_t> texture, util::span<const char> charmap)
    : texture(texture)
{
    ascii.resize(256);
    readPseudoCSV<8>(charmap, [&](size_t lineno, util::span<const char> columns[8]) {
        if (lineno == 0) {
            nativeSize_ = atol(columns[1].data());
            baseline_ = atol(columns[3].data());
            return;
        }
        uint32_t cp = atol(columns[0].data());
        auto& c = cp < 256 ? ascii[cp] : [&]() -> ui::font_symbol& {
            unicode.emplace_back(cp, ui::font_symbol{});
            return unicode.back().second;
        }();
        c.x = atoi(columns[1].data());
        c.y = atoi(columns[2].data());
        c.w = atoi(columns[3].data());
        c.h = atoi(columns[4].data());
        c.originX = atoi(columns[5].data());
        c.originY = atoi(columns[6].data());
        c.advance = atoi(columns[7].data());
    });
    std::sort(unicode.begin(), unicode.end(), [&](auto& a, auto& b) { return a.first < b.first; });
}

const ui::font_symbol& ui::font::operator[](wchar_t point) const {
    static const ui::font_symbol empty = {};
    if (point < 256)
        return ascii[point];
    auto it = std::lower_bound(unicode.begin(), unicode.end(), point, [](auto& p, wchar_t c) { return p.first < c; });
    return it == unicode.end() || it->first != point ? empty : it->second;
}

POINT ui::label::measureMinImpl() const {
    return measureImpl({0, 0});
}

POINT ui::label::measureImpl(POINT fit) const {
    double tx = 0, ty = 0, sx = 0;
    split<const ui::text_part>(data, [](auto& c) { return c.breakAfter; }, 0, [&](size_t, util::span<const ui::text_part> parts) {
        double lx = 0, ly = 0, ex = 0;
        for (const ui::text_part& part : parts) {
            double scale = (double)part.fontSize / part.font.get().nativeSize();
            for (wchar_t c : part.data) {
                auto& sym = part.font.get()[c];
                lx += sym.advance * scale;
                // Reserve some space for the character's edges.
                sx = std::min(sx, lx - scale * (sym.originX + sym.advance));
                ex = std::max(ex, lx + scale * (sym.w - sym.advance - sym.originX));
            }
            ly = std::max(ly, part.fontSize * lineHeight);
        }
        tx = std::max(ex, tx), ty += ly;
    });
    originX = -sx;
    return {hideOverflow ? std::min((LONG)(tx - sx) + 1, fit.x) : (LONG)(tx - sx) + 1, (LONG)ty + 1};
}

void ui::label::drawImpl(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const {
    measureMin(); // Make sure the origin X is set to the correct value.
    std::vector<ui::vertex> quads;
    if (!data.empty())
        quads.reserve(std::max_element(data.begin(), data.end(), [](auto& a, auto& b) {
            return a.data.size() < b.data.size(); })->data.size());
    // TODO use DirectWrite or something.
    double ty = 0;
    split<const ui::text_part>(data, [](auto& c) { return c.breakAfter; }, 0, [&](size_t, util::span<const ui::text_part> parts) {
        double ly = 0;
        double bs = 0;
        for (const ui::text_part& part : parts) {
            ly = std::max(ly, part.fontSize * lineHeight);
            bs = std::max(bs, part.fontSize * lineHeight * part.font.get().baseline() / (double)part.font.get().nativeSize());
        }
        ty += ly;
        double x = originX;
        double y = ty - bs;
        for (const ui::text_part& part : parts) {
            quads.clear();
            double scale = (double)part.fontSize / part.font.get().nativeSize();
            for (wchar_t c : part.data) {
                auto& info = part.font.get()[c];
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
            ctx.draw(target, part.font.get().loadTexture(ctx), quads, dirty, ui::dxcontext::distanceCoded);
        }
    });
}
