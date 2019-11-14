#pragma once

#include <stdint.h>
#include <math.h>
#include <utility>

struct FLOATX4 {
    union { float r, h, x, _1; };
    union { float g, s, y, _2; };
    union { float b, v, z, _3; };
    union { float a,    w, _4; };

    template <typename... Ts, typename F>
    FLOATX4 apply(F&& f, Ts&&... other) const {
        return {f(r, other.r...), f(g, other.g...), f(b, other.b...), f(a, other.a...)};
    }
};

static FLOATX4 u2qd(uint32_t q) {
    return {(uint8_t)(q >> 16) / 255.f, (uint8_t)(q >> 8) / 255.f, (uint8_t)q / 255.f, (uint8_t)(q >> 24) / 255.f};
}

static uint32_t qd2u(FLOATX4 q) {
    return (uint8_t)round(q.a * 255) << 24
         | (uint8_t)round(q.r * 255) << 16
         | (uint8_t)round(q.g * 255) << 8
         | (uint8_t)round(q.b * 255);
}

static FLOATX4 rgba2hsva(FLOATX4 x) {
    float min = x.r < x.b ? x.r < x.g ? x.r : x.g : x.b < x.g ? x.b : x.g;
    float max = x.r > x.b ? x.r > x.g ? x.r : x.g : x.b > x.g ? x.b : x.g;
    float h = 0, s = 0, v = max >= 0.00001 ? max : 0;
    if (max - min >= 0.00001) {
        s = 1 - min / max;
        h = x.r == max ? 0.f + (x.g - x.b) / (max - min)
          : x.g == max ? 2.f + (x.b - x.r) / (max - min)
          : /* x.b */    4.f + (x.r - x.g) / (max - min);
        h = h / 6 + (h < 0);
    }
    return {h, s, v, x.a};
}

static FLOATX4 hsva2rgba(FLOATX4 x) {
    int i = (int)(x.h * 6);
    float f = x.h * 6 - i;
    float m = x.v * (1.f - x.s);
    float p = x.v * (1.f - x.s * f);
    float q = x.v * (1.f - x.s * (1.f - f));
    return i % 6 == 0 ? FLOATX4{x.v, q, m, x.a}
         : i % 6 == 1 ? FLOATX4{p, x.v, m, x.a}
         : i % 6 == 2 ? FLOATX4{m, x.v, q, x.a}
         : i % 6 == 3 ? FLOATX4{m, p, x.v, x.a}
         : i % 6 == 4 ? FLOATX4{q, m, x.v, x.a}
         : /* 5 */  FLOATX4{x.v, m, p, x.a};
}

static FLOATX4 k2rgba(float t) {
    t /= 100; // http://www.zombieprototypes.com/?p=210
    auto clamp = [](float x) { return x < 0.f ? 0.f : x > 1.f ? 1.f : x; };
#define LC(a, b, c, d) (a) + (b) * (t - d) + (c) * logf(t - d)
    auto r = clamp(t < 66 ? 1.f : LC(1.38030159086f, 0.0004478684462f, -0.15785750233f, 55));
    auto g = clamp(t < 66 ? LC(-0.6088425711f, -0.001748900018f,  0.4097731843f,   2)
                          : LC( 1.2762722062f, 0.0003115080995f, -0.11013841706f, 50));
    auto b = clamp(t < 66 ? LC(-0.9990954974f, 0.0032447435545f,  0.453646839f,   10) : 1.f);
#undef LC
    return {r, g, b, 1};
}
