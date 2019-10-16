#pragma once

#include "../draw.hpp"
#include "../span.hpp"
#include "../resource.hpp"

namespace ui {
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

    enum builtin_rect {
        BUTTON_OUTER,
        BUTTON_OUTER_HOVER,
        BUTTON_OUTER_ACTIVE,
        BUTTON_INNER,
        BUTTON_INNER_HOVER,
        BUTTON_INNER_ACTIVE,
        BUTTON_BORDERLESS_OUTER,
        BUTTON_BORDERLESS_OUTER_HOVER,
        BUTTON_BORDERLESS_OUTER_ACTIVE,
        BUTTON_BORDERLESS_INNER,
        BUTTON_BORDERLESS_INNER_HOVER,
        BUTTON_BORDERLESS_INNER_ACTIVE,
        SLIDER_TRACK,
        SLIDER_GROOVE,
        WIN_CLOSE_OUTER,
        WIN_CLOSE_OUTER_HOVER,
        WIN_CLOSE_OUTER_ACTIVE,
        WIN_CLOSE_INNER,
        WIN_CLOSE_INNER_HOVER,
        WIN_CLOSE_INNER_ACTIVE,
        WIN_FRAME_OUTER,
        WIN_FRAME_OUTER_HOVER,
        WIN_FRAME_OUTER_ACTIVE,
        WIN_FRAME_INNER,
        WIN_FRAME_INNER_HOVER,
        WIN_FRAME_INNER_ACTIVE,
        WIN_ICON_CLOSE,
        WIN_ICON_MAXIMIZE,
        WIN_ICON_UNMAXIMIZE,
        WIN_ICON_MINIMIZE,
        BUILTIN_RECT_COUNT
    };

    winapi::com_ptr<ID3D11Texture2D> builtinTexture(ui::dxcontext&);
    RECT builtinRect(builtin_rect);
}
