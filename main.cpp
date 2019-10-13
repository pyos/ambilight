#include "dxui/base.hpp"
#include "dxui/resource.hpp"
#include "dxui/window.hpp"
#include "dxui/widgets/button.hpp"
#include "dxui/widgets/grid.hpp"
#include "dxui/widgets/label.hpp"
#include "dxui/widgets/slider.hpp"
#include "dxui/widgets/spacer.hpp"
#include "dxui/widgets/wincontrol.hpp"

#define AMBILIGHT_PC
#include "arduino/arduino.h"
#undef AMBILIGHT_PC

#include "capture.h"
#include "color.hpp"
#include "serial.hpp"
#include "thread.hpp"

#include <atomic>
#include <string>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace appui {
    struct state {
        std::atomic<size_t> width;
        std::atomic<size_t> height;
        std::atomic<size_t> musicLeds;
        std::atomic<size_t> serial;
        double brightnessV;
        double brightnessA;
        double gamma;
        double dr;
        double dg;
        double db;
        uint32_t color;
    };

    template <typename T>
    struct padded : T {
        template <typename... Args>
        padded(POINT padding, Args&&... args)
            : T(std::forward<Args>(args)...)
            , pad({padding.x, padding.y, padding.x, padding.y}, *this)
        {
        }

        ui::spacer pad;
    };

    struct padded_label : padded<ui::label> {
        padded_label(POINT padding, const ui::text_part& text)
            : padded<ui::label>{padding, std::vector<ui::text_part>{text}}
        {}
    };

    // name [-] N [+]
    // name [-] ---|----------- [+] N
    struct controlled_number {
        controlled_number(ui::grid& grid, size_t row, util::span<const wchar_t> name,
                          size_t min, size_t max, size_t step, bool useSlider = false)
            : min(min), max(max), step(step)
        {
            label.modText([&](auto& chunks) { chunks[0].data = name; });
            grid.set(0, row, &label, ui::grid::align_end);
            grid.set(1, row, &decButton);
            if (useSlider) {
                grid.set(2, row, &slider);
                grid.set(4, row, &numLabel);
            } else {
                grid.set(2, row, &numLabel);
            }
            grid.set(3, row, &incButton);
            decButton.setBorderless(true);
            incButton.setBorderless(true);
        }

        bool setValue(size_t v) {
            slider.setValue(v, min, max, step);
            numBuffer = std::to_wstring(v);
            numLabel.modText([&](auto& chunks) { chunks[0].data = numBuffer; });
            return onChange(v);
        }

    public:
        util::event<size_t> onChange;

    private:
        size_t min, max, step;
        ui::label label{{{L"", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}}};
        ui::label numLabel{{{L"", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}}};
        ui::label decLabel{{{L"\uf068", ui::font::loadPermanently<IDI_FONT_ICONS>()}}};
        ui::label incLabel{{{L"\uf067", ui::font::loadPermanently<IDI_FONT_ICONS>()}}};
        ui::button decButton{decLabel};
        ui::button incButton{incLabel};
        ui::slider slider;
        util::event<>::handle m1 = decButton.onClick.add([this]{
            return setValue(slider.mapValue(min, max, step) - step); });
        util::event<>::handle p1 = incButton.onClick.add([this]{
            return setValue(slider.mapValue(min, max, step) + step); });
        util::event<double>::handle sv = slider.onChange.add([this](double) {
            return setValue(slider.mapValue(min, max, step)); });
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
            setRowStretch(0, 1); setRowStretch(1, 6); setRowStretch(2, 1);
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
    // |       [with rgb corners ]       |
    // |                                 |
    // |  Width [-] ---|---------- [+] 1 |
    // | Height [-] ---|---------- [+] 2 |
    // |  Extra [-] ---|---------- [+] 3 |
    // | Serial [-] 3 [+]        [apply] |
    // +---------------------------------+
    struct sizing_config : padded<ui::grid> {
        sizing_config(const state& init)
            : padded<ui::grid>({20, 20}, 1, 4)
        {
            set(0, 0, &image);
            set(0, 1, &sliderGrid.pad);
            set(0, 2, &helpLabel.pad);
            set(0, 3, &bottomRow.pad);
            setPrimaryCell(0, 0);
            sliderGrid.set(4, 3, &constNumWidth);
            sliderGrid.setColStretch(2, 1);
            bottomRow.set(5, 0, &done);
            bottomRow.setColStretch(4, 1);
            w.setValue(init.width);
            h.setValue(init.height);
            e.setValue(init.musicLeds);
            s.setValue(init.serial);
        }

    public:
        // 0 = width, 1 = height, 2 = music leds, 3 = serial port
        util::event<int /* parameter */, size_t /* new value */> onChange;
        util::event<> onDone;

    private:
        screensetup image;
        padded_label helpLabel{{20, 20}, {L"Tweak the values until you get the pattern shown above.",
                                          ui::font::loadPermanently<IDI_FONT_SEGOE_UI>(), 22}};
        padded_label doneLabel{{20, 0}, {L"Done", ui::font::loadPermanently<IDI_FONT_SEGOE_UI>()}};
        padded<ui::grid> sliderGrid{{20, 0}, 5, 4};
        padded<ui::grid> bottomRow{{20, 20}, 6, 1};
        // The actual limit is 1 <= w + h <= LIMIT, but sliders jumping around
        // would probably be confusing.
        static constexpr size_t LIMIT = AMBILIGHT_SERIAL_CHUNK * AMBILIGHT_CHUNKS_PER_STRIP;
        controlled_number w{sliderGrid, 0, L"Screen width",  1, LIMIT, 1, true};
        controlled_number h{sliderGrid, 1, L"Screen height", 1, LIMIT, 1, true};
        controlled_number e{sliderGrid, 2, L"Music LEDs",    2, LIMIT, 2, true};
        controlled_number s{bottomRow,  0, L"Serial port",   1, 16,    1};
        ui::button done{doneLabel.pad};
        // A hacky way to set a constant size for the number labels:
        ui::spacer constNumWidth{{60, 1}};

        util::event<size_t>::handle wHandler = w.onChange.add([this](size_t v) { return onChange(0, v); });
        util::event<size_t>::handle hHandler = h.onChange.add([this](size_t v) { return onChange(1, v); });
        util::event<size_t>::handle eHandler = e.onChange.add([this](size_t v) { return onChange(2, v); });
        util::event<size_t>::handle sHandler = s.onChange.add([this](size_t v) { return onChange(3, v); });
        util::event<>::handle doneHandler = done.onClick.add([this]{ return onDone(); });
    };

    struct texslider : ui::slider {
        texslider(RECT track, RECT groove) : track(track), groove(groove) {}
    private:
        winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext& ctx) const override {
            return ctx.cachedTexture<extraWidgets>(); }
        RECT getTrack() const override { return track; }
        RECT getGroove() const override { return groove; }
        RECT track, groove;
    };

    struct gray_bg : ui::texrect {
        using ui::texrect::texrect;

        void setTransparent(bool value) {
            transparent = value;
            invalidate();
        }

    protected:
        static winapi::com_ptr<ID3D11Texture2D> grayPixel(ui::dxcontext& ctx) {
            return ctx.textureFromRaw({64, 64, 64, 64, 0, 0, 0, 0}, 2, false); }
        winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext& ctx) const override {
            return ctx.cachedTexture<grayPixel>(); }
        RECT getOuter() const override { return {transparent, 0, transparent + 1, 1}; }
        RECT getInner() const override { return {transparent, 0, transparent + 1, 1}; }
        bool transparent = false;
    };

    struct h_slider : texslider { h_slider() : texslider({0,154,15,186}, {15,154,128,157}) {} };
    struct s_slider : texslider { s_slider() : texslider({0,154,15,186}, {15,157,128,160}) {} };
    struct v_slider : texslider { v_slider() : texslider({0,154,15,186}, {15,160,128,163}) {} };

    struct color_config_tab : gray_bg {
        color_config_tab(const state& init, util::event<int, double>& onChange)
            : onChange(onChange)
        {
            setContents(&grid.pad);
            grid.set(0, 0, &gammaLabel.pad);
            grid.set(0, 1, &rOffLabel.pad);
            grid.set(0, 2, &gOffLabel.pad);
            grid.set(0, 3, &bOffLabel.pad);
            grid.set(1, 0, &gammaSlider.pad);
            grid.set(1, 1, &rOffSlider.pad);
            grid.set(1, 2, &gOffSlider.pad);
            grid.set(1, 3, &bOffSlider.pad);
            grid.setColStretch(1, 1);
            gammaSlider.setValue((init.gamma - 1) / 2); // use gamma from 1 to 3
            rOffSlider.setValue(init.dr);
            gOffSlider.setValue(init.dg);
            bOffSlider.setValue(init.db);
        }

    private:
        padded<ui::grid> grid{{10, 10}, 2, 4};
        padded_label gammaLabel{{10, 10}, {L"\uf042", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        padded_label rOffLabel{{10, 10}, {L"\uf185", ui::font::loadPermanently<IDI_FONT_ICONS>(), 18, 0xFFFF3333u}};
        padded_label gOffLabel{{10, 10}, {L"\uf185", ui::font::loadPermanently<IDI_FONT_ICONS>(), 18, 0xFF33FF33u}};
        padded_label bOffLabel{{10, 10}, {L"\uf185", ui::font::loadPermanently<IDI_FONT_ICONS>(), 18, 0xFF3333FFu}};
        padded<ui::slider> gammaSlider{{10, 10}};
        padded<ui::slider> rOffSlider{{10, 10}};
        padded<ui::slider> gOffSlider{{10, 10}};
        padded<ui::slider> bOffSlider{{10, 10}};
        util::event<int, double>& onChange;
        util::event<double>::handle gammaHandler = gammaSlider.onChange.add([this](double value) { return onChange(0, value * 2 + 1); });
        util::event<double>::handle rOffHandler = rOffSlider.onChange.add([this](double value) { return onChange(1, value); });
        util::event<double>::handle gOffHandler = gOffSlider.onChange.add([this](double value) { return onChange(2, value); });
        util::event<double>::handle bOffHandler = bOffSlider.onChange.add([this](double value) { return onChange(3, value); });
    };

    struct color_select_tab : gray_bg {
        color_select_tab(const state& init, util::event<uint32_t>& onChange)
            : onChange(onChange)
        {
            setContents(&grid.pad);
            grid.set(0, 0, &hueSlider.pad);
            grid.set(0, 1, &satSlider.pad);
            grid.setColStretch(0, 1);
            auto [a, h, s, v] = argb2ahsv(u2qd(init.color));
            hueSlider.setValue(h);
            satSlider.setValue(s);
        }

        uint32_t value() const {
            return qd2u(ahsv2argb({1, hueSlider.value(), satSlider.value(), 1}));
        }

    public:
        util::event<uint32_t>& onChange;
    private:
        padded<ui::grid> grid{{10, 10}, 1, 2};
        padded<h_slider> hueSlider{{10, 10}};
        padded<s_slider> satSlider{{10, 10}};
        util::event<double>::handle hueHandler = hueSlider.onChange.add([this](double) { return onChange(value()); });
        util::event<double>::handle satHandler = satSlider.onChange.add([this](double) { return onChange(value()); });
    };

    struct extra_button : gray_bg {
        extra_button(const ui::text_part& text)
            : label({text})
        {
            setContents(&button);
            pad.set(0, 0, &label);
            pad.setColStretch(0, 1);
            pad.setRowStretch(0, 1);
            button.setBorderless(true);
            setTransparent(true);
        }

        bool setState(bool value) {
            state = value;
            setTransparent(!state);
            return onClick(state);
        }

    public:
        util::event<bool> onClick;

    private:
        bool state = false;
        ui::label label;
        ui::grid pad{1, 1};
        ui::button button{pad};
        util::event<>::handle h = button.onClick.add([this] { return setState(!state); });
    };

    struct extra_tabs : ui::grid {
        extra_tabs(const state& init, util::event<int, double>& onGamma, util::event<uint32_t>& onColor)
            : ui::grid(1, 3)
            , gammaTab(init, onGamma)
            , colorTab(init, onColor)
        {
            if (init.color >> 24)
                colorButton.setState(true);
            buttons.set(0, 0, &gammaButton);
            buttons.set(1, 0, &colorButton);
            buttons.setColStretch(0, 1);
            buttons.setColStretch(1, 1);
            set(0, 2, &buttons);
            setColStretch(0, 1);
        }

    private:
        color_config_tab gammaTab;
        color_select_tab colorTab;
        ui::grid buttons{2, 1};
        extra_button gammaButton{{L"\uf0d0", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        extra_button colorButton{{L"\uf043", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        util::event<bool>::handle gammaH = gammaButton.onClick.add([this](bool show) {
            set(0, 0, show ? &gammaTab : nullptr);
        });
        util::event<bool>::handle colorH = colorButton.onClick.add([this](bool show) {
            set(0, 1, show ? &colorTab : nullptr);
            return colorTab.onChange(colorTab.value() & (show ? 0xFFFFFFFFu : 0x00FFFFFFu));
        });
    };

    struct tooltip_config : ui::grid {
        tooltip_config(const state& init)
            : ui::grid(1, 2)
            , extraTabs(init, onGamma, onColor)
        {
            set(0, 0, &extraTabs);
            set(0, 1, &onTransparent.pad);
            setColStretch(0, 1);

            onTransparent.set(0, 0, &brightnessGrid);
            onTransparent.set(0, 1, &bottomRow);
            onTransparent.setColStretch(0, 1);
            brightnessGrid.set(0, 0, &bLabel.pad);
            brightnessGrid.set(1, 0, &bSlider.pad);
            brightnessGrid.set(0, 1, &mLabel.pad);
            brightnessGrid.set(1, 1, &mSlider.pad);
            brightnessGrid.setColStretch(1, 1);
            bottomRow.set(0, 0, &statusText.pad, ui::grid::align_start);
            bottomRow.set(1, 0, &sButton.pad);
            bottomRow.set(2, 0, &qButton.pad);
            bottomRow.setColStretch(0, 1);
            bSlider.setValue(init.brightnessV);
            mSlider.setValue(init.brightnessA);
            sButton.setBorderless(true);
            qButton.setBorderless(true);
        }

        void setStatusMessage(util::span<const wchar_t> msg) {
            statusText.modText([&](auto& chunks) { chunks[0].data = msg; });
        }

    public:
        util::event<int, double> onBrightness; // 0 = video, 1 = music
        util::event<int, double> onGamma;      // 0 = gamma, 1..3 = rgb offsets
        util::event<uint32_t> onColor;         // 0 = live, 0xFF?????? = constant
        util::event<> onSettings;
        util::event<> onQuit;

    private:
        extra_tabs extraTabs;
        padded<ui::grid> onTransparent{{10, 10}, 1, 3};
        padded<ui::grid> brightnessGrid{{10, 0}, 2, 2};
        padded_label bLabel{{10, 10}, {L"\uf185", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        padded_label mLabel{{10, 10}, {L"\uf001", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        padded<v_slider> bSlider{{10, 10}};
        padded<v_slider> mSlider{{10, 10}};

        padded<ui::grid> bottomRow{{10, 0}, 3, 1};
        padded_label statusText{{10, 10}, {L"", ui::font::loadPermanently<IDI_FONT_SEGOE_UI>(), 18, 0x80FFFFFFu}};
        ui::label sLabel{{{L"\uf013", ui::font::loadPermanently<IDI_FONT_ICONS>()}}};
        ui::label qLabel{{{L"\uf011", ui::font::loadPermanently<IDI_FONT_ICONS>()}}};
        padded<ui::button> sButton{{10, 10}, sLabel};
        padded<ui::button> qButton{{10, 10}, qLabel};

        util::event<double>::handle bHandler = bSlider.onChange.add([this](double v) { return onBrightness(0, v); });
        util::event<double>::handle mHandler = mSlider.onChange.add([this](double v) { return onBrightness(1, v); });
        util::event<>::handle sHandler = sButton.onClick.add([this]{ return onSettings(); });
        util::event<>::handle qHandler = qButton.onClick.add([this]{ return onQuit(); });
    };
}

int ui::main() {
    ui::window mainWindow{1, 1};
    mainWindow.onDestroy.add([&] { ui::quit(); }).release();

    // TODO load from file
    appui::state fake = {
        72, 40, 76, 3, 0.7, 0.4, 2.0, 1.0, 1.0, 1.0, 0x00FFFFFFu
    };

    auto loopThread = [&](auto&& f) {
        return [&, f = std::move(f)](std::atomic<bool>& terminate) {
            while (!terminate) try {
                f(terminate);
            } catch (const std::exception /*util::retry*/&) {
                Sleep(500);
            } /*catch (const util::fatal& err) {
                std::lock_guard<std::mutex> lock(mut);
                unexpectedErrorText = err.msg;
                PostMessage(window, ID_MESSAGE_FATAL_ERROR, 0, 0);
                return;
            }*/
        };
    };

    std::mutex mut;
    std::condition_variable frameEv;
    bool frameDirty = false;
    UINT frameData[4][AMBILIGHT_CHUNKS_PER_STRIP * AMBILIGHT_SERIAL_CHUNK] = {};

    auto updateLocked = [&](auto&& unsafePart) {
        std::lock_guard<std::mutex> lock(mut);
        if (!unsafePart())
            return;
        frameDirty = true;
        frameEv.notify_all();
        // TODO render the preview
    };

    auto updateFrom = [&](UINT* a, UINT* b, UINT* c, UINT* d, size_t w, size_t h, size_t m) {
        UINT* ptrs[] = {a, b, c, d};
        size_t lens[] = {w + h, w + h, m / 2, m / 2};
        updateLocked([&]{
            bool any = false;
            for (size_t i = 0; i < 4; i++) if (ptrs[i]) {
                if (std::equal(frameData[i], frameData[i] + lens[i], ptrs[i]))
                    continue;
                std::copy(ptrs[i], ptrs[i] + lens[i], frameData[i]);
                any = true;
            }
            return any;
        });
    };

    std::atomic<uint32_t> averageColor = fake.color;
    auto captureVideo = loopThread([&](std::atomic<bool>& terminate) {
        size_t w = fake.width;
        size_t h = fake.height;
        auto cap = captureScreen(0, w, h);
        std::vector<UINT> data((w + h) * 2);
        while (!terminate) if (auto in = cap->next()) {
            UINT averageComponents[4] = {0, 0, 0, 0};
            for (const auto& color : in.reinterpret<const BYTE[4]>())
                for (UINT c = 0; c < 4; c++)
                    averageComponents[c] += color[c];
            averageColor = 0xFF000000u
                        | (averageComponents[2] / w / h) << 16
                        | (averageComponents[1] / w / h) << 8
                        | (averageComponents[0] / w / h);
            auto out = data.data();
#define W_PX(y, x) do { *out++ = in[(y) * w + (x)]; } while (0)
            // bottom right -> bottom left -> top left; bottom right -> top right -> top left
            for (UINT x = w; x--; ) W_PX(h - 1, x);
            for (UINT y = h; y--; ) W_PX(y, 0);
            for (UINT y = h; y--; ) W_PX(y, h - 1);
            for (UINT x = w; x--; ) W_PX(0, x);
#undef W_PX
            updateFrom(data.data(), data.data() + w + h, nullptr, nullptr, w, h, 0);
        }
    });

// Coefficients for ensuring uniformity of the spectrum. Each octave's power
// is divided by e^(Mx) / N, where x = the index of that octave from the end.
#define DFT_EQUALIZER_M 0.55f
#define DFT_EQUALIZER_N 0.12f

    auto captureAudio = loopThread([&](std::atomic<bool>& terminate) {
        double lastH = 0;
        double lastS = 0;
        auto cap = captureDefaultAudioOutput();
        std::vector<UINT> data(fake.musicLeds);
        updateFrom(nullptr, nullptr, data.data(), data.data() + data.size() / 2, 0, 0, data.size());
        while (!terminate) if (auto in = cap->next()) {
            // 1. Need a lower bound on value so that the strip is always visible at all. 
            // 2. When value is 0, both hue and saturation become undefined (all colors are black),
            //    causing an abrupt switch to white at the end of a fade to black. Keep the old
            //    hue/saturation when any value is OK to avoid that.
            auto [a, h, s, v] = argb2ahsv(u2qd(averageColor));
            auto [_, r, g, b] = ahsv2argb({1, (h = lastH = s ? h : lastH), (s = lastS = v ? s : lastS), std::max(v, 0.5)});
            UINT delta = qd2u({0, r * 2 / in.size(), g * 2 / in.size(), b * 2 / in.size()});
            size_t i = 0, j = 0;
            for (size_t lim : {data.size() / 2, data.size()}) {
                for (size_t k = in.size() / 2; k--; j++)
                    for (size_t w = tanh(in[j] / exp((k + 1) * DFT_EQUALIZER_M) / DFT_EQUALIZER_N) * (lim - i); w--; )
                        data[i++] = 0xFF000000u | (delta * (k + 1));
                while (i < lim)
                    data[i++] = 0xFF000000u;
            }
            updateFrom(nullptr, nullptr, data.data(), data.data() + data.size() / 2, 0, 0, data.size());
        }
    });

    auto transmitData = loopThread([&](std::atomic<bool>& terminate) {
        auto filename = L"\\\\.\\COM" + std::to_wstring(fake.serial.load());
        serial comm{filename.c_str()};
        for (size_t iter = 0; !terminate; iter++) {
            if (auto lock = std::unique_lock<std::mutex>(mut)) {
                // Ping the arduino at least once per ~1.6s so that it knows the app is still running.
                if (iter % 16 && !frameEv.wait_for(lock, std::chrono::milliseconds(100), [&]{ return frameDirty; }))
                    continue;
                const size_t w = fake.width, h = fake.height, m = fake.musicLeds;
                const size_t lengths[4] = {w + h, w + h, m / 2, m / 2};
                for (size_t i = 0, strip = 0; strip < 4; i += lengths[strip++])
                    // TODO color offsets
                    comm.update((uint8_t)strip, frameData[strip],
                        (float)fake.gamma, (float)(strip < 2 ? fake.brightnessV : fake.brightnessA), 0, 0);
                frameDirty = false;
            }
            comm.submit();
        }
    });

    std::optional<util::thread> serialThread{transmitData};
    std::optional<util::thread> videoCaptureThread;
    std::optional<util::thread> audioCaptureThread;

    std::unique_ptr<ui::window> sizingWindow;
    std::unique_ptr<ui::window> tooltipWindow;
    appui::sizing_config sizingConfig{fake};
    appui::tooltip_config tooltipConfig{fake};

    auto setTestPattern = [&] {
        videoCaptureThread.reset();
        audioCaptureThread.reset();
        updateLocked([&] {
            // See screensetup.png.
            size_t w = fake.width, h = fake.height, m = fake.musicLeds, s = w + h;
            for (auto& strip : frameData)
                std::fill(std::begin(strip), std::end(strip), 0xFF000000u);
            frameData[0][0] = frameData[1][0] = 0xFF00FFFFu;
            frameData[0][w] = frameData[0][w - 1] = 0xFFFFFF00u;
            frameData[1][h] = frameData[1][h - 1] = 0xFFFF00FFu;
            frameData[0][s - 1] = frameData[1][s - 1] = 0xFFFFFFFFu;
            std::fill(frameData[2], frameData[2] + m / 2, 0xFFFFFF00u);
            std::fill(frameData[3], frameData[3] + m / 2, 0xFF00FFFFu);
            return true;
        });
    };

    auto setVideoPattern = [&] {
        uint32_t c = fake.color;
        if (c & 0xFF000000) {
            videoCaptureThread.reset();
            averageColor = c;
            updateLocked([&] {
                size_t s = fake.width + fake.height;
                std::fill(frameData[0], frameData[0] + s, c);
                std::fill(frameData[1], frameData[1] + s, c);
                return true;
            });
        } else if (!videoCaptureThread) {
            videoCaptureThread.emplace(captureVideo);
        }
    };

    sizingConfig.onDone.add([&] {
        if (sizingWindow)
            sizingWindow->close();
        mainWindow.setNotificationIcon(ui::loadSmallIcon(ui::fromBundled(IDI_APP)), L"Ambilight");
        setVideoPattern();
        audioCaptureThread.emplace(captureAudio);
    }).release();
    sizingConfig.onChange.add([&](int i, size_t value) {
        switch (i) {
            case 0: fake.width = value; break;
            case 1: fake.height = value; break;
            case 2: fake.musicLeds = value; break;
            case 3: fake.serial = value; break;
        }
        if (i == 3) {
            serialThread.reset();
            serialThread.emplace(transmitData);
        } else {
            setTestPattern();
        }
    }).release();

    //tooltipConfig.setStatusMessage(L"Serial port offline");
    tooltipConfig.onQuit.add([&] { mainWindow.close(); }).release();
    tooltipConfig.onSettings.add([&] {
        mainWindow.clearNotificationIcon();
        auto w = GetSystemMetrics(SM_CXSCREEN);
        auto h = GetSystemMetrics(SM_CYSCREEN);
        sizingWindow = std::make_unique<ui::window>(800, 800, (w - 800) / 2, (h - 800) / 2);
        sizingWindow->setRoot(&sizingConfig.pad);
        sizingWindow->setBackground(0xa0000000u, true);
        sizingWindow->setShadow(true);
        sizingWindow->show();
        setTestPattern();
    }).release();
    tooltipConfig.onBrightness.add([&](int i, double v) {
        switch (i) {
            case 0: fake.brightnessV = v; break;
            case 1: fake.brightnessA = v; break;
        }
        // Ping the serial thread.
        updateLocked([&]{ return true; });
    }).release();
    tooltipConfig.onGamma.add([&](int i, double v) {
        switch (i) {
            case 0: fake.gamma = v; break;
            case 1: fake.dr = v; break;
            case 2: fake.dg = v; break;
            case 3: fake.db = v; break;
        }
        updateLocked([&]{ return true; });
    }).release();
    tooltipConfig.onColor.add([&](uint32_t c) {
        fake.color = c;
        setVideoPattern();
    }).release();

    mainWindow.onNotificationIcon.add([&](POINT p, bool primary) {
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
        tooltipWindow->onBlur.add([&] { tooltipWindow->close(); }).release();
        tooltipWindow->setBackground(0xa0000000u, true);
        tooltipWindow->setDragByEmptyAreas(false);
        tooltipWindow->setGravity(hGravity, vGravity);
        tooltipWindow->setRoot(&tooltipConfig);
        tooltipWindow->setTopmost(true);
        tooltipWindow->show();
    }).release();

    if (false /* TODO if config not loaded */) {
        tooltipConfig.onSettings();
    } else {
        sizingConfig.onDone();
    }
    return ui::dispatch();
}
