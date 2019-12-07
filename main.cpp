#include "dxui/base.hpp"
#include "dxui/resource.hpp"
#include "dxui/window.hpp"
#include "dxui/widgets/button.hpp"
#include "dxui/widgets/grid.hpp"
#include "dxui/widgets/label.hpp"
#include "dxui/widgets/slider.hpp"
#include "dxui/widgets/spacer.hpp"
#include "dxui/widgets/wincontrol.hpp"

#include "arduino/arduino.h"
#include "capture.h"
#include "color.hpp"
#include "defer.hpp"
#include "serial.hpp"

#include <atomic>
#include <string>
#include <fstream>
#include <condition_variable>
#include <mutex>

namespace appui {
    #define CONFIG_NOP(x) x
    #define CONFIG_MAP(f, ...) \
        CONFIG_NOP(f(uint32_t, width,       16         , __VA_ARGS__)); \
        CONFIG_NOP(f(uint32_t, height,      9          , __VA_ARGS__)); \
        CONFIG_NOP(f(uint32_t, musicLeds,   20         , __VA_ARGS__)); \
        CONFIG_NOP(f(uint32_t, serial,      3          , __VA_ARGS__)); \
        CONFIG_NOP(f(uint32_t, color,       0x00FFFFFFu, __VA_ARGS__)); \
        CONFIG_NOP(f(double,   brightnessV, .7         , __VA_ARGS__)); \
        CONFIG_NOP(f(double,   brightnessA, .4         , __VA_ARGS__)); \
        CONFIG_NOP(f(double,   gamma,       2.         , __VA_ARGS__)); \
        CONFIG_NOP(f(double,   temperature, 6600.      , __VA_ARGS__)); \
        CONFIG_NOP(f(double,   minLevel,    0.         , __VA_ARGS__));
    #define CONFIG_DECLARE(T, name, default, wrapper) wrapper<T> name{default}

    struct state { CONFIG_MAP(CONFIG_DECLARE, std::atomic) };

    template <typename T>
    struct padded : T {
        template <typename... Args>
        padded(RECT padding, Args&&... args)
            : T(std::forward<Args>(args)...)
            , pad(padding, *this)
        {}

        ui::spacer pad;
    };

    struct padded_label : padded<ui::label> {
        padded_label(RECT padding, const ui::text_part& text)
            : padded<ui::label>{padding, std::vector<ui::text_part>{text}}
        {}
    };

    // name [-] N [+]
    // name [-] ---|----------- [+] N
    struct controlled_number {
        controlled_number(ui::grid& grid, size_t row, util::span<const wchar_t> name,
                          uint32_t min, uint32_t max, uint32_t step, bool useSlider = false)
            : min(min), max(max), step(step)
        {
            label.modText([&](auto& chunks) { chunks[0].data = name; });
            grid.set(0, row, &label, ui::grid::align_end);
            grid.set(1, row, &decButton.pad);
            if (useSlider) {
                grid.set(2, row, &slider);
                grid.set(4, row, &numLabel);
            } else {
                grid.set(2, row, &numLabel);
            }
            grid.set(3, row, &incButton.pad);
            decButton.onClick.addForever([=]{ setValue(value() - step); return onChange(value()); });
            incButton.onClick.addForever([=]{ setValue(value() + step); return onChange(value()); });
            slider.onChange.addForever([=](double) { setValue(value()); return onChange(value()); });
        }

        uint32_t value() const {
            return slider.mapValue(min, max, step);
        }

        void setValue(uint32_t v) {
            slider.setValue(v, min, max, step);
            numBuffer = std::to_wstring(v);
            numLabel.modText([&](auto& chunks) { chunks[0].data = numBuffer; });
        }

    public:
        util::event<uint32_t> onChange;

    private:
        uint32_t min, max, step;
        ui::label label{{{L"", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}}};
        ui::label numLabel{{{L"", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}}};
        ui::label decLabel{{{L"\uf068", ui::font::loadPermanently<IDI_FONT_ICONS>()}}};
        ui::label incLabel{{{L"\uf067", ui::font::loadPermanently<IDI_FONT_ICONS>()}}};
        padded<ui::borderless_button> decButton{{5, 0, 5, 0}, decLabel};
        padded<ui::borderless_button> incButton{{5, 0, 5, 0}, incLabel};
        ui::slider slider;
        std::wstring numBuffer;
    };

    static winapi::com_ptr<ID3D11Texture2D> extraWidgets(ui::dxcontext& ctx) {
        return ctx.textureFromPNG(ui::read(ui::fromBundled(IDI_SCREENSETUP), L"PNG"));
    }

    struct screensetup : ui::grid {
        screensetup()
            : ui::grid(3, 3)
        {
            set(1, 1, &screen);
            set(1, 2, &strip);
            setColStretch(0, 1); setColStretch(1, 6); setColStretch(2, 1);
            setRowStretch(0, 1); setRowStretch(1, 6); setRowStretch(2, 0);
            strip.set(1, 0, &stripL, ui::grid::align_end);
            strip.set(2, 0, &stripR, ui::grid::align_start);
            strip.setColStretch(0, 1);
            strip.setColStretch(1, 3);
            strip.setColStretch(2, 3);
            strip.setColStretch(3, 1);
        }

    private:
        struct fixedAspectRatio : ui::widget {
            POINT measureMinImpl() const override { return {60, 1}; }
            POINT measureImpl(POINT fit) const override {
                return {std::min(fit.x, fit.y * 16 / 9 + 60), std::min(fit.x * 9 / 16 - 33, fit.y)}; }
            void drawImpl(ui::dxcontext&, ID3D11Texture2D*, RECT, RECT) const override {}
        } stretch16To9;

        struct hstretch : ui::widget {
            POINT measureMinImpl() const override { return {0, 0}; }
            POINT measureImpl(POINT fit) const override { return {fit.x, 0}; }
            void drawImpl(ui::dxcontext&, ID3D11Texture2D*, RECT, RECT) const override {}
        } stretchAnyTo0x1, stretchAnyTo0x2;

        ui::image<extraWidgets> screen{{  0,   0, 128, 128}, { 64,  64,  64,  64}, stretch16To9};
        ui::image<extraWidgets> stripL{{ 22, 128,  64, 154}, { 44, 142,  44, 142}, stretchAnyTo0x1};
        ui::image<extraWidgets> stripR{{ 64, 128, 106, 154}, { 84, 142,  84, 142}, stretchAnyTo0x2};
        ui::grid strip{4, 1};
    };

    // +---------------------------------+
    // |                                 |
    // |       [image of a screen]       |
    // |       [with CMYW corners]       |
    // |                                 |
    // |  Width [-] ---|---------- [+] 1 |
    // | Height [-] ---|---------- [+] 2 |
    // |  Extra [-] ---|---------- [+] 3 |
    // | Serial [-] 3 [+]        [apply] |
    // +---------------------------------+
    struct sizing_config : ui::grid {
        sizing_config(const state& init)
            : ui::grid(1, 4)
        {
            set({&image, &sliderGrid.pad, &helpLabel.pad, &bottomRow.pad});
            setPrimaryCell(0, 0);
            sliderGrid.set(4, 3, &constNumWidth);
            sliderGrid.setColStretch(2, 1);
            bottomRow.set(5, 0, &done);
            bottomRow.setColStretch(4, 1);
            w.setValue(init.width);
            h.setValue(init.height);
            e.setValue(init.musicLeds);
            s.setValue(init.serial);
            w.onChange.addForever([this](uint32_t v) {
                w.setValue(v = std::min(v, MAX_LEDS - h.value()));
                return onChange(0, v); });
            h.onChange.addForever([this](uint32_t v) {
                h.setValue(v = std::min(v, MAX_LEDS - w.value()));
                return onChange(1, v); });
            e.onChange.addForever([this](uint32_t v) { return onChange(2, v); });
            s.onChange.addForever([this](uint32_t v) { return onChange(3, v); });
            done.onClick.addForever([this]{ if (auto w = parentWindow()) w->close(); });
        }

    public:
        // 0 = width, 1 = height, 2 = music leds, 3 = serial port
        util::event<int /* parameter */, uint32_t /* new value */> onChange;

    private:
        screensetup image;
        padded_label helpLabel{{40, 0, 40, 0}, {L"Tweak the values until you get the pattern shown above.",
                                                ui::font::loadPermanently<IDI_FONT_SEGOE_UI>(), 22}};
        padded_label doneLabel{{20, 0, 20, 0}, {L"Done", ui::font::loadPermanently<IDI_FONT_SEGOE_UI>()}};
        padded<ui::grid> sliderGrid{{40, 40, 40, 40}, 5, 4};
        padded<ui::grid> bottomRow{{40, 40, 40, 40}, 6, 1};
        ui::button done{doneLabel.pad};
        controlled_number w{sliderGrid, 0, L"Screen width",  1, MAX_LEDS * 4 / 5, 1, true};
        controlled_number h{sliderGrid, 1, L"Screen height", 1, MAX_LEDS * 4 / 5, 1, true};
        controlled_number e{sliderGrid, 2, L"Music LEDs",    2, MAX_LEDS, 2, true};
        controlled_number s{bottomRow,  0, L"Serial port",   1, 16, 1};
        // A hacky way to set a constant size for the number labels:
        ui::spacer constNumWidth{{60, 1}};
    };

    template <int i>
    struct texslider : ui::slider {
    protected:
        winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext& ctx) const override {
            return ctx.cached<ID3D11Texture2D, extraWidgets>(); }
        RECT getTrack() const override { return {0, 154, 15, 186}; }
        RECT getGroove() const override { return {15, 154 + 6 * i, 128, 157 + 6 * i}; }
        RECT getFilled() const override { return {15, 157 + 6 * i, 128, 160 + 6 * i}; }
    };

    struct gray_bg : ui::texrect {
        using ui::texrect::texrect;

        bool toggle() {
            enabled = !enabled;
            invalidate();
            return enabled;
        }

    protected:
        winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext& ctx) const override {
            return enabled ? ctx.cached<ID3D11Texture2D, extraWidgets>() : winapi::com_ptr<ID3D11Texture2D>{}; }
        RECT getOuter() const override { return {15, 163, 16, 164}; }
        RECT getInner() const override { return getOuter(); }
        bool enabled = true;
    };

    enum setting { Y, T, Lv, La, Lm };

    struct tooltip_config : ui::grid {
        tooltip_config(const state& init)
            : ui::grid(1, 4)
        {
            set(0, 1, init.color >> 24 ? &colorTab : nullptr);
            set(0, 2, &buttons);
            set(0, 3, &brightnessGrid.pad);
            setColStretch(0, 1);

            buttons.set({&title.pad, nullptr, &gammaBg, &colorBg});
            buttons.setColStretch(1, 1);
            if (!(init.color >> 24))
                colorBg.toggle();
            gammaBg.toggle();
            gammaButton.onClick.addForever([this] { set(0, 0, gammaBg.toggle() ? &gammaTab : nullptr); });
            colorButton.onClick.addForever([this] {
                bool show = colorBg.toggle();
                set(0, 1, show ? &colorTab : nullptr);
                return onColor(color() & (show ? 0xFFFFFFFFu : 0x00FFFFFFu));
            });

            brightnessGrid.set({&bvLabel.pad, &bvSlider.pad,
                                &baLabel.pad, &baSlider.pad});
            brightnessGrid.setColStretch(1, 1);
            bvSlider.setValue(init.brightnessV);
            baSlider.setValue(init.brightnessA);
            bvSlider.onChange.addForever([this](double v) { return onChange(Lv, v); });
            baSlider.onChange.addForever([this](double v) { return onChange(La, v); });

            gammaGrid.set({&yLabel.pad, &ySlider.pad,
                           &tLabel.pad, &tSlider.pad,
                           &mLabel.pad, &mSlider.pad});
            gammaGrid.setColStretch(1, 1);
            ySlider.setValue((init.gamma - 1.2) / 2.4); // gamma <- [1.2..3.6]
            tSlider.setValue(pow((init.temperature - 1000) / 19000, 0.5673756656029532)); // temperature <- [1000..20000]
            mSlider.setValue(init.minLevel * 32); // minLevel <- [0..32]
            ySlider.onChange.addForever([&](double value) { return onChange(Y, value * 2.4 + 1.2); });
            tSlider.onChange.addForever([&](double value) { return onChange(T, pow(value, 1.7625006862733437) * 19000 + 1000); });
            mSlider.onChange.addForever([&](double value) { return onChange(Lm, value / 32); });

            colorGrid.set({&hueSlider.pad, &satSlider.pad});
            colorGrid.setColStretch(0, 1);
            auto c = rgba2hsva(u2qd(init.color));
            hueSlider.setValue(c.h);
            satSlider.setValue(c.s);
            hueSlider.onChange.addForever([&](double) { return onColor(color()); });
            satSlider.onChange.addForever([&](double) { return onColor(color()); });
        }

    public:
        util::event<setting, double> onChange;
        util::event<uint32_t> onColor;

    private:
        uint32_t color() const {
            return qd2u(hsva2rgba({(float)hueSlider.value(), (float)satSlider.value(), 1, 1}));
        }

    private:
        ui::grid buttons{4, 1};
        padded_label title{{20, 5, 0, 0}, {L"Ambilight", ui::font::loadPermanently<IDI_FONT_SEGOE_UI>()}};
        padded_label gammaLabel{{10, 5, 10, 5}, {L"\uf0d0", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        padded_label colorLabel{{12, 5, 12, 5}, {L"\uf043", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        ui::borderless_button gammaButton{gammaLabel.pad};
        ui::borderless_button colorButton{colorLabel.pad};
        gray_bg gammaBg{gammaButton};
        gray_bg colorBg{colorButton};

        padded<ui::grid> brightnessGrid{{10, 0, 10, 10}, 2, 2};
        padded_label bvLabel{{10, 10, 10, 10}, {L"\uf185", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22}};
        padded_label baLabel{{10, 10, 10, 10}, {L"\uf001", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22}};
        padded<texslider<2>> bvSlider{{10, 10, 10, 10}};
        padded<texslider<2>> baSlider{{10, 10, 10, 10}};

        padded<ui::grid> gammaGrid{{10, 10, 10, 10}, 2, 3};
        padded_label yLabel{{10, 10, 10, 10}, {L"\uf042", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22}};
        padded_label tLabel{{10, 10, 10, 10}, {L"\uf2c9", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22}};
        padded_label mLabel{{10, 10, 10, 10}, {L"0", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>(), 22}};
        padded<ui::slider> ySlider{{10, 10, 10, 10}};
        padded<texslider<3>> tSlider{{10, 10, 10, 10}};
        padded<texslider<2>> mSlider{{10, 10, 10, 10}};
        gray_bg gammaTab{gammaGrid.pad};

        padded<ui::grid> colorGrid{{10, 10, 10, 10}, 1, 2};
        padded<texslider<0>> hueSlider{{10, 10, 10, 10}};
        padded<texslider<1>> satSlider{{10, 10, 10, 10}};
        gray_bg colorTab{colorGrid.pad};
    };

    struct preview_render : ui::widget {
        preview_render(size_t w, size_t h, size_t musicLeds)
            : colors((w + h) * 2 + musicLeds)
            , w_(w)
            , h_(h)
            , m_(musicLeds)
        {}

        template <typename F /* = FLOATX4(FLOATX4) */>
        void setColors(const FLOATX4* bl /* w+h */, const FLOATX4* rt /* w+h */,
                       const FLOATX4* ml /* m/2 */, const FLOATX4* mr /* m/2 */, F&& transform) {
            // Order in `colors`: clockwise from top left, then music LEDs left to right.
            auto out = colors.data();
            auto t0 = transform(0), t1 = transform(1), t2 = transform(2), t3 = transform(3);
            for (size_t x = w_; x--; ) *out++ = t1(rt[x + h_]);
            for (size_t y = h_; y--; ) *out++ = t1(rt[y]);
            for (size_t x = w_; x--; ) *out++ = t0(*bl++);
            for (size_t y = h_; y--; ) *out++ = t0(*bl++);
            for (size_t i = m_ / 2; i--; ) *out++ = t2(ml[i]);
            for (size_t i = m_ / 2; i--; ) *out++ = t3(*mr++);
            invalidate();
        }

    private:
        POINT measureMinImpl() const override {
            auto r = std::max(5.f, std::min(w_, h_) / (10.f - 2));
            return {(LONG)(w_ + r * 2), (LONG)(h_ + r * 3)};
        }

        POINT measureImpl(POINT fit) const override {
            auto r = std::max(5.f, std::min(fit.x, fit.y) / 10.f);
            return {std::min(fit.x, (LONG)round(r*2 + (fit.y - r*3) * w_ / h_)),
                    std::min(fit.y, (LONG)round(r*3 + (fit.x - r*2) * h_ / w_))};
        }

        static DirectX::XMFLOAT4 c2v(FLOATX4 c) {
            c = c.apply<false>([](float x) { return powf(x / 65535, 1 / 2.4f); }); // Unapply gamma and scaling.
            // LEDs do not actually emit black, so move brightness to alpha.
            auto v = c.a ? (c.r > c.b ? c.r > c.g ? c.r : c.g : c.g > c.b ? c.g : c.b) / c.a : 0.f;
            return v ? DirectX::XMFLOAT4{c.r / v, c.g / v, c.b / v, c.a * v}
                     : DirectX::XMFLOAT4{0, 0, 0, 0};
        }

        //    ---       -2-   -+ ... +-   -+-
        //     |      1- | -3- | ... | -+- | -+  Total N*9 + 3 vertices.
        // r = |     / \ | / \ | ... | / \ | / \
        //     |    /   \|/   \| ... |/   \|/   \
        //    ---  4-----0-----+-...-+-----+-----+
        //            |--| = d
        static void addStrip(ui::vertex*& out, float x0, float y0, float d, float r, int i,
                             const FLOATX4*& colors, size_t n, bool inv = false) {
            float q = r > d ? sqrtf(r * r - d * d) : 0;
            float x1[] = {-d, +q, +d, -q};
            float y1[] = {-q, -d, +q, +d};
            float x2[] = { 0, +r,  0, -r};
            float y2[] = {-r,  0, +r, 0 };
            float x3[] = {+d, +q, -d, -q};
            float y3[] = {-q, +d, +q, -d};
            float ux[] = {-d,  0, +d,  0};
            float uy[] = { 0, -d,  0, +d};
            ui::vertex cv = {{x0 + ux[i], y0 + uy[i], 0}, {}, {}};
            while (n--) {
                out++->pos = {x0,         y0,         0};
                out++->pos = {x0 + x1[i], y0 + y1[i], 0};
                out++->pos = {x0 + x2[i], y0 + y2[i], 0};
                out++->pos = {x0 + x2[i], y0 + y2[i], 0};
                out++->pos = {x0 + x3[i], y0 + y3[i], 0};
                out++->pos = {x0,         y0,         0};
                out[-6].clr = out[-1].clr = c2v(inv ? *--colors : *colors++);
                auto z = out[-5], w = out[-6];
                *out++ = cv;
                *out++ = z;
                *out++ = w;
                cv = out[-9];
                (i % 2 ? y0 : x0) += i < 2 ? d * 2 : d * -2;
            }
            auto z = out[-5];
            *out++ = cv;
            *out++ = z;
            out++->pos = {x0 + ux[i], y0 + uy[i], 0};
        }

        void drawImpl(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const override {
            std::vector<ui::vertex> vs((w_ + h_) * 18 + m_ * 18 + 24);
            auto r = std::max(5.f, std::min(total.right - total.left, total.bottom - total.top) / 10.f);
            auto dw = (float)(total.right - total.left - r*2) / w_;
            auto dh = (float)(total.bottom - total.top - r*3) / h_;
            auto dm = (float)(total.right - total.left) / (m_ + 1);
            ui::vertex center[] = {QUADP(total.left + r, total.top + r, total.right - r, total.bottom - r * 2, 0, 0, 0, 0, 0)};
            ui::vertex* out = vs.data();
            const FLOATX4* in = colors.data();
            addStrip(out, total.left  + r + dw / 2, total.top    + r,            dw / 2, r,     0, in, w_);
            addStrip(out, total.right - r,          total.top    + r + dh / 2,   dh / 2, r,     1, in, h_);
            addStrip(out, total.right - r - dw / 2, total.bottom - r*2,          dw / 2, r,     2, in, w_);
            addStrip(out, total.left  + r,          total.bottom - r*2 - dh / 2, dh / 2, r,     3, in, h_);
            addStrip(out, total.left  + dm,         total.bottom - r/2,          dm / 2, r / 2, 0, in, m_);
            addStrip(out, total.right - dm,         total.bottom - r/2,          dm / 2, r / 2, 2, in, m_, true);
            // Fill the middle of the screen.
            for (size_t i = 0; i < 6; i++)
                *out++ = {center[i].pos, {0, 0, 0, .8f}, {}};
            ctx.draw(target, ctx.cached<ID3D11Texture2D, extraWidgets>(), vs, dirty);
        }

    private:
        std::vector<FLOATX4> colors;
        size_t w_;
        size_t h_;
        size_t m_;
    };

    struct preview : ui::grid {
        preview(size_t w, size_t h, size_t musicLeds)
            : ui::grid{1, 2}
            , render({20, 20, 20, 20}, w, h, musicLeds)
        {
            titlebar.set({nullptr, &min, &close});
            titlebar.set(0, 0, &title, ui::grid::align_global_center);
            titlebar.setColStretch(0, 1);
            set({&titlebar, &render.pad});
            setPrimaryCell(0, 1);
        }

        template <typename F /* = FLOATX4(FLOATX4) */>
        void setColors(const FLOATX4 *bl, const FLOATX4 *rt, const FLOATX4 *ml, const FLOATX4 *mr, F&& transform) {
            render.setColors(bl, rt, ml, mr, transform);
        }

    private:
        ui::grid titlebar{3, 1};
        ui::label title{{{L"Ambilight Preview", ui::font::loadPermanently<IDI_FONT_SEGOE_UI>(), 16}}};
        ui::win_minimize min;
        ui::win_close close;
        padded<preview_render> render;
    };
}

#define CONFIG_WRITE(T, name, default, out, s) out << #name << " " << s.name << "\n"
#define CONFIG_READ(T, name, default, key, in, s) if (T value; key == #name && in >> value) s.name = value

int ui::main() {
    bool initialized = true;
    appui::state config;
    wchar_t configPath[MAX_PATH];
    size_t i = winapi::throwOnFalse(GetModuleFileName(nullptr, configPath, MAX_PATH));
    configPath[--i] = 'g';
    configPath[--i] = 'f';
    configPath[--i] = 'c';
    try {
        std::ifstream in{configPath};
        for (std::string key; in >> key; ) { CONFIG_MAP(CONFIG_READ, key, in, config) }
    } catch (const std::exception&) { initialized = false; }

    ui::window mainWindow{1, 1};
    mainWindow.setTitle(L"Ambilight");
    mainWindow.onDestroy.addForever([&] {
        std::ofstream out{configPath};
        CONFIG_MAP(CONFIG_WRITE, out, config)
        ui::quit();
    });

    std::unique_ptr<appui::preview> preview;
    std::unique_ptr<ui::window> sizingWindow;
    std::unique_ptr<ui::window> tooltipWindow;
    std::unique_ptr<ui::window> previewWindow;
    appui::sizing_config sizingConfig{config};
    appui::tooltip_config tooltipConfig{config};
    std::atomic<bool> previewing{false};

    std::atomic<bool> terminate{false};
    // These more granular mutexes (mutices?) synchronize writes to each pair of strips
    // separately. If a capture thread is unable to acquire this mutex in a timely
    // manner, it assumes the main thread has acquired it for the purpose of displaying
    // a static pattern and will destroy the capture object until the mutex becomes
    // available again. (`videoMutex` must also be held while writing `averageColor`.)
    std::timed_mutex videoMutex;
    std::timed_mutex audioMutex;
    // Start in the non-capturing state. The locks will be released after everything
    // is ready, and maybe the initial configuration is done.
    std::unique_lock<std::timed_mutex> videoLock{videoMutex};
    std::unique_lock<std::timed_mutex> audioLock{audioMutex};
    // This mutex synchronizes writes to `frameData` with the serial thread's reads.
    // A write-release that set `frameDirty` must be preceded by firing off `frameEv`.
    // NOTE: deadlock-avoiding resource hierarchy: `videoLock`, then `audioLock`, then `mut`.
    std::mutex mut;
    std::condition_variable frameEv;
    FLOATX4 frameData[4][MAX_LEDS] = {};
    FLOATX4 averageColor = u2qd(config.color);
    bool frameDirty = false;

    auto updateLocked = [&](auto&& unsafePart) {
        if (auto lk = std::unique_lock<std::mutex>(mut)) {
            unsafePart();
            frameDirty = true;
            frameEv.notify_all();
        }
        if (previewing)
            mainWindow.post(0);
    };

    auto loopThread = [&](auto&& f) {
        return std::thread{[&, f = std::move(f)] {
            while (!terminate) try {
                f();
            } catch (const std::exception&) {
                // Probably just device reconfiguration or whatever.
                // TODO only these errors are non-fatal for audio capturing:
                //     AUDCLNT_E_DEVICE_INVALIDATED
                //     AUDCLNT_E_DEVICE_IN_USE
                //     AUDCLNT_E_SERVICE_NOT_RUNNING
                //     AUDCLNT_E_BUFFER_OPERATION_PENDING
                // TODO only these errors are non-fatal for video capturing:
                //     DXGI_ERROR_DRIVER_INTERNAL_ERROR
                //     DXGI_ERROR_DEVICE_REMOVED
                //     DXGI_ERROR_DEVICE_RESET
                //     DXGI_ERROR_ACCESS_LOST
                //     DXGI_ERROR_UNSUPPORTED
                //     DXGI_ERROR_SESSION_DISCONNECTED
                //     E_ACCESSDENIED
                //     E_OUTOFMEMORY
                Sleep(500);
            }
        }};
    };

    auto videoCaptureThread = loopThread([&] {
        // Wait until the main thread allows capture threads to proceed.
        { std::unique_lock<std::timed_mutex> lk(videoMutex); };
        uint32_t w = config.width;
        uint32_t h = config.height;
        auto cap = captureScreen(0, w, h);
        while (!terminate) if (auto in = cap->next()) {
            FLOATX4 sum = {0, 0, 0, 0};
            for (const auto& color : in)
                sum = sum.apply([](float x, float y) { return x + y; }, color);
            auto lk = std::unique_lock<std::timed_mutex>(videoMutex, std::chrono::milliseconds(30));
            if (!lk)
                return;
            updateLocked([&] {
                averageColor = sum.apply([&](float x) { return x / w / h; });
                auto a = frameData[0], b = frameData[1];
                for (auto x = w; x--; ) *a++ = in[(h - 1) * w + x]; // bottom right -> bottom left
                for (auto y = h; y--; ) *a++ = in[y * w];           // bottom left -> top left
                for (auto y = h; y--; ) *b++ = in[y * w + w - 1];   // bottom right -> top right
                for (auto x = w; x--; ) *b++ = in[x];               // top right -> top left
            });
        }
    });

    auto audioCaptureThread = loopThread([&] {
        { std::unique_lock<std::timed_mutex> lk(audioMutex); };
        auto cap = captureDefaultAudioOutput();
        while (!terminate) if (auto in = cap->next()) {
            auto lk = std::unique_lock<std::timed_mutex>(audioMutex, std::chrono::milliseconds(30));
            if (!lk)
                return;
            updateLocked([&, half = in.size() / 2, size = config.musicLeds / 2] {
                auto ac = rgba2hsva(averageColor);
                ac.s = std::min(ac.v, .5f) * 2 * ac.s; // Avoid abrupt color changes on fade to black.
                ac.v = std::max(ac.v, .5f); // Ensure the strip is always visible at all.
                size_t j = 0;
                for (auto* out : {frameData[2], frameData[3]}) {
                    size_t i = 0;
                    for (size_t k = half; k--;) {
                        auto c = hsva2rgba({ac.h, ac.s, ac.v * (k + 1) / half, ac.v * (k + 1) / half});
                        // Yay, hardcoded coefficients! TODO figure out a better mapping.
                        auto w = (size_t)(tanh(in[j++] / exp((k + 1) * 0.55) / 0.12) * (size - i));
                        while (w--) out[i++] = c;
                    }
                    while (i < size)
                        out[i++] = {0, 0, 0, 0};
                }
            });
        }
    });

    auto makeTransform = [&](uint8_t strip) {
        auto gamma = config.gamma.load();
        auto white = k2rgba((float)config.temperature.load());
        auto lower = strip < 2 ? pow(config.minLevel, 1 / config.gamma) : 0;
        auto upper = strip < 2 ? config.brightnessV.load() : config.brightnessA.load();
        return [=](FLOATX4 color) {
            return color.apply<false>([&](float x, float y) {
                return 65535 * (float)pow((x * upper * (1 - lower) + lower) * y, gamma); }, white); };
    };

    mainWindow.onMessage.addForever([&](uintptr_t) {
        if (previewing)
            if (auto lk = std::unique_lock<std::mutex>(mut))
                preview->setColors(frameData[0], frameData[1], frameData[2], frameData[3], makeTransform);
    });

    auto serialThread = loopThread([&] {
        auto port = config.serial.load();
        auto filename = L"\\\\.\\COM" + std::to_wstring(port);
        serial comm{filename.c_str()};
        while (port == config.serial && !terminate) {
            std::unique_lock<std::mutex> lock{mut};
            // Ping the arduino at least once per ~2s so that it knows the app is still running.
            if (frameEv.wait_for(lock, std::chrono::seconds(2), [&]{ return frameDirty; })) {
                frameDirty = false;
                for (uint8_t strip = 0; strip < 4; strip++)
                    comm.update(strip, frameData[strip], makeTransform(strip));
            }
            lock.unlock();
            comm.submit();
        }
    });

    DEFER {
        terminate = true;
        // Allow the threads to actually read the flag.
        if (videoLock) videoLock.unlock();
        if (audioLock) audioLock.unlock();
        // Also wake the serial thread so it terminates instantly.
        updateLocked([&] {});
        videoCaptureThread.join();
        audioCaptureThread.join();
        serialThread.join();
    };

    auto setTestPattern = [&] {
        if (!videoLock) videoLock.lock();
        if (!audioLock) audioLock.lock();
        updateLocked([&] {
            for (auto& strip : frameData)
                std::fill(std::begin(strip), std::end(strip), FLOATX4{0, 0, 0, 1});
            size_t w = config.width, h = config.height, m = config.musicLeds;
            // The pattern depicted in screensetup.png.
            frameData[0][0]         = frameData[1][0]         = {0, 1, 1, 1};
            frameData[0][w]         = frameData[0][w - 1]     = {1, 1, 0, 1};
            frameData[1][h]         = frameData[1][h - 1]     = {1, 0, 1, 1};
            frameData[0][w + h - 1] = frameData[1][w + h - 1] = {1, 1, 1, 1};
            // TODO maybe render 2 dots on each instead?
            std::fill(frameData[2], frameData[2] + m / 2, FLOATX4{1, 1, 0, 1});
            std::fill(frameData[3], frameData[3] + m / 2, FLOATX4{0, 1, 1, 1});
        });
    };

    auto setVideoPattern = [&](FLOATX4 color) {
        if (color.a) {
            if (!videoLock) videoLock.lock();
            updateLocked([&, s = config.width + config.height] {
                averageColor = color;
                std::fill(frameData[0], frameData[0] + s, color);
                std::fill(frameData[1], frameData[1] + s, color);
            });
        } else if (videoLock) {
            videoLock.unlock();
        }
    };

    auto setBothPatterns = [&] {
        mainWindow.setNotificationIcon(ui::loadSmallIcon(ui::fromBundled(IDI_APP)), L"Ambilight");
        setVideoPattern(u2qd(config.color));
        if (audioLock) {
            updateLocked([&] {
                // There's no guarantee that the audio capturer will have anything on first
                // iteration (might be nothing playing), so clear the test pattern explicitly.
                std::fill(std::begin(frameData[2]), std::end(frameData[2]), FLOATX4{0, 0, 0, 1});
                std::fill(std::begin(frameData[3]), std::end(frameData[3]), FLOATX4{0, 0, 0, 1});
            });
            audioLock.unlock();
        }
    };

    auto openPreview = [&] {
        if (previewing) {
            previewWindow->focus();
            return;
        }
        previewWindow.reset();
        preview = std::make_unique<appui::preview>(config.width.load(), config.height.load(), config.musicLeds.load());
        previewing = true;
        mainWindow.post(0);
        previewWindow = std::make_unique<ui::window>(800, 600, 100, 100);
        previewWindow->setRoot(preview.get());
        previewWindow->setBackground(0xcc111111u);
        previewWindow->setTitle(L"Ambilight Preview");
        previewWindow->setShadow(true);
        previewWindow->onDestroy.addForever([&] { previewing = false; });
        previewWindow->show();
    };

    auto showSizeConfig = [&] {
        mainWindow.clearNotificationIcon();
        auto w = GetSystemMetrics(SM_CXSCREEN);
        auto h = GetSystemMetrics(SM_CYSCREEN);
        auto s = std::min(w, h) * 3 / 4;
        previewWindow.reset();
        preview.reset();
        sizingWindow = std::make_unique<ui::window>(s, s, (w - s) / 2, (h - s) / 2);
        sizingWindow->setRoot(&sizingConfig);
        sizingWindow->setBackground(0xcc111111u);
        sizingWindow->setTitle(L"Ambilight Setup");
        sizingWindow->onClose.addForever([&]{ setBothPatterns(); });
        sizingWindow->setShadow(true);
        sizingWindow->show();
        setTestPattern();
    };

    sizingConfig.onChange.addForever([&](int i, uint32_t value) {
        switch (i) {
            case 0: config.width = value; break;
            case 1: config.height = value; break;
            case 2: config.musicLeds = value; break;
            case 3: config.serial = value; break;
        }
        setTestPattern();
    });
    tooltipConfig.onChange.addForever([&](appui::setting s, double v) {
        switch (s) {
            case appui::Y: config.gamma = v; break;
            case appui::T: config.temperature = v; break;
            case appui::Lv: config.brightnessV = v; break;
            case appui::La: config.brightnessA = v; break;
            case appui::Lm: config.minLevel = v; break;
        }
        // Ping the serial thread.
        updateLocked([&]{ });
    });
    tooltipConfig.onColor.addForever([&](uint32_t c) { setVideoPattern(u2qd(config.color = c)); });

    winapi::holder<HMENU, DestroyMenu> menu{CreatePopupMenu()};
    winapi::throwOnFalse(AppendMenu(menu.get(), MF_STRING, 1, L"Preview"));
    winapi::throwOnFalse(AppendMenu(menu.get(), MF_STRING, 2, L"Reconfigure..."));
    winapi::throwOnFalse(AppendMenu(menu.get(), MF_STRING, 3, L"Quit"));

    mainWindow.onNotificationIcon.addForever([&](POINT p, bool primary) {
        if (tooltipWindow)
            tooltipWindow.reset();
        if (!primary) {
            mainWindow.focus();
            switch (TrackPopupMenuEx(menu.get(), TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON, p.x, p.y, mainWindow, nullptr)) {
                case 1: openPreview(); break;
                case 2: showSizeConfig(); break;
                case 3: mainWindow.close(); break;
            }
            return;
        }
        ui::window::gravity hGravity = ui::window::gravity_start;
        ui::window::gravity vGravity = ui::window::gravity_start;
        POINT size = tooltipConfig.measure({500, 0});
        // Put the window into the corner nearest to the notification tray.
        MONITORINFO monitor = {};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfo(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &monitor)) {
            POINT corners[4] = {
                {monitor.rcWork.left, monitor.rcWork.top},
                {monitor.rcWork.right, monitor.rcWork.top},
                {monitor.rcWork.right, monitor.rcWork.bottom},
                {monitor.rcWork.left, monitor.rcWork.bottom},
            };
            auto sqdistance = [](POINT a, POINT b) {
                return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); };
            p = *std::min_element(std::begin(corners), std::end(corners),
                [&](POINT a, POINT b) { return sqdistance(p, a) < sqdistance(p, b); });
            hGravity = p.x == monitor.rcWork.left ? ui::window::gravity_start : ui::window::gravity_end;
            vGravity = p.y == monitor.rcWork.top  ? ui::window::gravity_start : ui::window::gravity_end;
        }
        if (hGravity == ui::window::gravity_end) p.x -= size.x;
        if (vGravity == ui::window::gravity_end) p.y -= size.y;
        tooltipWindow = std::make_unique<ui::window>(size.x, size.y, p.x, p.y, &mainWindow);
        tooltipWindow->onBlur.addForever([&] { tooltipWindow->close(); });
        tooltipWindow->setTitle(L"Ambilight");
        tooltipWindow->setBackground(0xa0000000u, true);
        tooltipWindow->setDragByEmptyAreas(false);
        tooltipWindow->setGravity(hGravity, vGravity);
        tooltipWindow->setRoot(&tooltipConfig);
        tooltipWindow->setTopmost(true);
        tooltipWindow->show();
        tooltipWindow->focus();
    });

    if (!initialized)
        showSizeConfig();
    else
        setBothPatterns();
    return ui::dispatch();
}
