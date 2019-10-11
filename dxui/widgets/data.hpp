#pragma once

#include "../draw.hpp"
#include "../span.hpp"
#include "../resource.hpp"

template <typename T, typename F>
static util::span<T> strip(util::span<T> in, F&& discard) {
    auto a = in.begin();
    auto b = in.end();
    while (a < b && discard(a[0])) a++;
    while (a < b && discard(b[-1])) b--;
    return util::span<T>(a, b - a);
}

template <typename T, typename S /* = bool(T&) */, typename F /* = void(size_t, util::span<T>) */>
static void split(util::span<T> string, S&& sep, size_t limit, F&& f) {
    size_t i = 0;
    for (auto it = string.begin(), next = it; it != string.end(); it = next) {
        next = i + 1 == limit ? string.end() : std::find_if(it, string.end(), sep);
        if (next != string.end())
            next++;
        f(i++, util::span<T>(it, next - it));
    }
}

template <size_t columns, typename F /* = void(size_t, util::span<const char>[columns]) */>
static void readPseudoCSV(util::span<const char> resource, F&& f) {
    size_t skipped = 0;
    split<const char>(resource, [](char c) { return c == '\n'; }, 0, [&](size_t lineno, util::span<const char> line) {
        // Ignore empty lines and comments (starting with whitespace and then `#`).
        if (std::all_of(line.begin(), line.end(), [](char c) { return isspace(c); }) || line[0] == '#') {
            skipped++;
            return;
        }
        util::span<const char> stripped[columns] = {};
        split<const char>(line, [](char c) { return c == ','; }, columns, [&](size_t i, util::span<const char> part) {
            stripped[i] = strip(part, [](char c) { return isspace(c); });
        });
        f(lineno - skipped, stripped);
    });
}

static winapi::com_ptr<ID3D11Texture2D> builtinTexture(ui::dxcontext& ctx) {
    auto resource = ui::read(ui::fromBundled(IDI_WIDGETS), L"PNG");
    if (!resource)
        throw std::runtime_error("IDI_WIDGETS PNG-type resource could not be loaded");
    return ctx.textureFromPNG(resource);
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
