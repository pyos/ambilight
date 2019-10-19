#pragma once

#include <stdint.h>
#include <math.h>
#include <utility>

struct DOUBLEX4 {
    double _1, _2, _3, _4;
};

static DOUBLEX4 u2qd(uint32_t q) {
    return {(uint8_t)(q >> 24) / 255., (uint8_t)(q >> 16) / 255., (uint8_t)(q >> 8) / 255., (uint8_t)q / 255.};
}

static uint32_t qd2u(DOUBLEX4 q) {
    return (uint8_t)round(q._1 * 255) << 24
         | (uint8_t)round(q._2 * 255) << 16
         | (uint8_t)round(q._3 * 255) << 8
         | (uint8_t)round(q._4 * 255);
}

static DOUBLEX4 argb2ahsv(DOUBLEX4 x) {
    auto [a, r, g, b] = x;
    double min = r < b ? r < g ? r : g : b < g ? b : g;
    double max = r > b ? r > g ? r : g : b > g ? b : g;
    double h = 0, s = 0, v = max >= 0.00001 ? max : 0;
    if (max - min >= 0.00001) {
        s = 1 - min / max;
        h = r == max ? 0.0 + (g - b) / (max - min)
          : g == max ? 2.0 + (b - r) / (max - min)
          : /* c.B */  4.0 + (r - g) / (max - min);
        h = h / 6 + (h < 0);
    }
    return {a, h, s, v};
}

static DOUBLEX4 ahsv2argb(DOUBLEX4 x) {
    auto [a, h, s, v] = x;
    int i = (int)(h * 6);
    double f = h * 6 - i;
    double m = v * (1.0 - s);
    double p = v * (1.0 - s * f);
    double q = v * (1.0 - s * (1.0 - f));
    return i == 0 ? DOUBLEX4{a, v, q, m}
         : i == 1 ? DOUBLEX4{a, p, v, m}
         : i == 2 ? DOUBLEX4{a, m, v, q}
         : i == 3 ? DOUBLEX4{a, m, p, v}
         : i == 4 ? DOUBLEX4{a, q, m, v}
         : /* 6 */  DOUBLEX4{a, v, m, p};
}

static DOUBLEX4 k2argb(double t) {
    t /= 100; // http://www.zombieprototypes.com/?p=210
#define LC(a, b, c, d) (a) + (b) * (t - d) + (c) * log(t - d)
    auto r = std::max(0., std::min(1., t < 66 ? 1. : LC(1.3803015908551253, 0.0004478684462124118, -0.15785750232675008, 55)));
    auto g = std::max(0., std::min(1., t < 66 ? LC(-0.6088425710866344, -0.001748900018414868, 0.4097731842899564, 2)
                                              : LC(1.2762722061615583, 0.0003115080994769546, -0.11013841706194392, 50)));
    auto b = std::max(0., std::min(1., t < 66 ? LC(-0.9990954974165059, 0.0032447435545127036, 0.453646839257496, 10) : 1));
#undef LC
    return {1, r, g, b};
}
