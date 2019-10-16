#pragma once

#include "../span.hpp"
#include <cctype>

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
}
