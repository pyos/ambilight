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
#include "defer.hpp"
#include "serial.hpp"

#include <atomic>
#include <string>
#include <condition_variable>
#include <mutex>

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
            grid.set(1, row, &decButton.pad);
            if (useSlider) {
                grid.set(2, row, &slider);
                grid.set(4, row, &numLabel);
            } else {
                grid.set(2, row, &numLabel);
            }
            grid.set(3, row, &incButton.pad);
            decButton.setBorderless(true);
            incButton.setBorderless(true);
            decButton.onClick.addForever([=]{ setValue(value() - step); return onChange(value()); });
            incButton.onClick.addForever([=]{ setValue(value() + step); return onChange(value()); });
            slider.onChange.addForever([=](double) { setValue(value()); return onChange(value()); });
        }

        size_t value() const {
            return slider.mapValue(min, max, step);
        }

        void setValue(size_t v) {
            slider.setValue(v, min, max, step);
            numBuffer = std::to_wstring(v);
            numLabel.modText([&](auto& chunks) { chunks[0].data = numBuffer; });
        }

    public:
        util::event<size_t> onChange;

    private:
        size_t min, max, step;
        ui::label label{{{L"", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}}};
        ui::label numLabel{{{L"", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}}};
        ui::label decLabel{{{L"\uf068", ui::font::loadPermanently<IDI_FONT_ICONS>()}}};
        ui::label incLabel{{{L"\uf067", ui::font::loadPermanently<IDI_FONT_ICONS>()}}};
        padded<ui::button> decButton{{5, 0}, decLabel};
        padded<ui::button> incButton{{5, 0}, incLabel};
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
    // |       [with CMYW corners]       |
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
            w.onChange.addForever([this](size_t v) {
                w.setValue(v = std::min(v, LIMIT - h.value()));
                return onChange(0, v); });
            h.onChange.addForever([this](size_t v) {
                h.setValue(v = std::min(v, LIMIT - w.value()));
                return onChange(1, v); });
            e.onChange.addForever([this](size_t v) { return onChange(2, v); });
            s.onChange.addForever([this](size_t v) { return onChange(3, v); });
            done.onClick.addForever([this]{ return onDone(); });
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
        ui::button done{doneLabel.pad};
        static constexpr size_t LIMIT = AMBILIGHT_SERIAL_CHUNK * AMBILIGHT_CHUNKS_PER_STRIP;
        controlled_number w{sliderGrid, 0, L"Screen width",  1, LIMIT * 4 / 5, 1, true};
        controlled_number h{sliderGrid, 1, L"Screen height", 1, LIMIT * 4 / 5, 1, true};
        controlled_number e{sliderGrid, 2, L"Music LEDs",    2, LIMIT, 2, true};
        controlled_number s{bottomRow,  0, L"Serial port",   1, 16, 1};
        // A hacky way to set a constant size for the number labels:
        ui::spacer constNumWidth{{60, 1}};
    };

    template <int i>
    struct texslider : ui::slider {
    protected:
        winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext& ctx) const override {
            return ctx.cachedTexture<extraWidgets>(); }
        RECT getTrack() const override { return {0, 154, 15, 186}; }
        RECT getGroove() const override { return {15, 154 + 3 * i, 128, 157 + 3 * i}; }
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
            return enabled ? ctx.cachedTexture<extraWidgets>() : winapi::com_ptr<ID3D11Texture2D>{}; }
        RECT getOuter() const override { return {15, 163, 16, 164}; }
        RECT getInner() const override { return getOuter(); }
        bool enabled = true;
    };

    enum setting { Y, R, G, B, Lv, La };

    struct color_config_tab : gray_bg {
        color_config_tab(const state& init, util::event<setting, double>& onChange) {
            setContents(&grid.pad);
            grid.set({&yLabel.pad, &ySlider.pad,
                      &rLabel.pad, &rSlider.pad,
                      &gLabel.pad, &gSlider.pad,
                      &bLabel.pad, &bSlider.pad});
            grid.setColStretch(1, 1);
            ySlider.setValue((init.gamma - 1) / 2); // use gamma from 1 to 3
            rSlider.setValue(init.dr);
            gSlider.setValue(init.dg);
            bSlider.setValue(init.db);
            ySlider.onChange.addForever([&](double value) { return onChange(Y, value * 2 + 1); });
            rSlider.onChange.addForever([&](double value) { return onChange(R, value); });
            gSlider.onChange.addForever([&](double value) { return onChange(G, value); });
            bSlider.onChange.addForever([&](double value) { return onChange(B, value); });
        }

    private:
        padded<ui::grid> grid{{10, 10}, 2, 4};
        padded_label yLabel{{10, 10}, {L"\uf042", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22}};
        padded_label rLabel{{10, 10}, {L"\uf185", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22, 0xFFFF3333u}};
        padded_label gLabel{{10, 10}, {L"\uf185", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22, 0xFF33FF33u}};
        padded_label bLabel{{10, 10}, {L"\uf185", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22, 0xFF3333FFu}};
        padded<ui::slider> ySlider{{10, 10}};
        padded<ui::slider> rSlider{{10, 10}};
        padded<ui::slider> gSlider{{10, 10}};
        padded<ui::slider> bSlider{{10, 10}};
    };

    struct color_select_tab : gray_bg {
        color_select_tab(const state& init, util::event<uint32_t>& onChange) {
            setContents(&grid.pad);
            grid.set({&hueSlider.pad, &satSlider.pad});
            grid.setColStretch(0, 1);
            auto [a, h, s, v] = argb2ahsv(u2qd(init.color));
            hueSlider.setValue(h);
            satSlider.setValue(s);
            hueSlider.onChange.addForever([&](double) { return onChange(value()); });
            satSlider.onChange.addForever([&](double) { return onChange(value()); });
        }

        uint32_t value() const {
            return qd2u(ahsv2argb({1, hueSlider.value(), satSlider.value(), 1}));
        }

    private:
        padded<ui::grid> grid{{10, 10}, 1, 2};
        padded<texslider<0>> hueSlider{{10, 10}};
        padded<texslider<1>> satSlider{{10, 10}};
    };

    struct tooltip_config : ui::grid {
        tooltip_config(const state& init)
            : ui::grid(1, 5)
            , gammaTab(init, onChange)
            , colorTab(init, onColor)
        {
            if (init.color >> 24)
                set(0, 0, &colorTab);
            else
                colorBg.toggle();
            gammaBg.toggle();
            set(0, 3, &brightnessGrid.pad);
            set(0, 4, &buttons);
            setColStretch(0, 1);

            buttons.set({&gammaBg, &colorBg, nullptr, &sButton, &qButton});
            buttons.setColStretch(2, 1);
            sButton.setBorderless(true);
            qButton.setBorderless(true);
            gammaButton.setBorderless(true);
            colorButton.setBorderless(true);

            brightnessGrid.set({&bLabel.pad, &bSlider.pad,
                                &mLabel.pad, &mSlider.pad});
            brightnessGrid.setColStretch(1, 1);
            bSlider.setValue(init.brightnessV);
            mSlider.setValue(init.brightnessA);

            gammaButton.onClick.addForever([this] { set(0, 0, gammaBg.toggle() ? &gammaTab : nullptr); });
            colorButton.onClick.addForever([this] {
                bool show = colorBg.toggle();
                set(0, 1, show ? &colorTab : nullptr);
                return onColor(colorTab.value() & (show ? 0xFFFFFFFFu : 0x00FFFFFFu));
            });

            bSlider.onChange.addForever([this](double v) { return onChange(Lv, v); });
            mSlider.onChange.addForever([this](double v) { return onChange(La, v); });
            sButton.onClick.addForever([this]{ return onSettings(); });
            qButton.onClick.addForever([this]{ return onQuit(); });
        }

    public:
        util::event<setting, double> onChange;
        util::event<uint32_t> onColor;
        util::event<> onSettings;
        util::event<> onQuit;

    private:
        ui::grid buttons{5, 1};
        color_config_tab gammaTab;
        color_select_tab colorTab;
        padded_label gammaLabel{{5, 5}, {L"\uf0d0", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        padded_label colorLabel{{5, 5}, {L"\uf043", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        padded_label sLabel{{5, 5}, {L"\uf013", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        padded_label qLabel{{5, 5}, {L"\uf011", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        ui::button gammaButton{gammaLabel.pad};
        ui::button colorButton{colorLabel.pad};
        ui::button sButton{sLabel.pad};
        ui::button qButton{qLabel.pad};
        gray_bg gammaBg{gammaButton};
        gray_bg colorBg{colorButton};

        padded<ui::grid> brightnessGrid{{10, 10}, 2, 2};
        padded_label bLabel{{10, 10}, {L"\uf185", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22}};
        padded_label mLabel{{10, 10}, {L"\uf001", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22}};
        padded<texslider<2>> bSlider{{10, 10}};
        padded<texslider<2>> mSlider{{10, 10}};
    };
}

int ui::main() {
    ui::window mainWindow{1, 1};
    mainWindow.setTitle(L"Ambilight");
    mainWindow.onDestroy.addForever([&] { ui::quit(); });
    std::unique_ptr<ui::window> sizingWindow;
    std::unique_ptr<ui::window> tooltipWindow;
    // TODO load from file
    appui::state fake = {72, 40, 76, 3, 0.7, 0.4, 2.0, 1.0, 1.0, 1.0, 0x00FFFFFFu};
    appui::sizing_config sizingConfig{fake};
    appui::tooltip_config tooltipConfig{fake};

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

    std::atomic<uint32_t> averageColor = fake.color;
    bool frameDirty = false;
    UINT frameData[4][AMBILIGHT_CHUNKS_PER_STRIP * AMBILIGHT_SERIAL_CHUNK] = {};

    auto updateLocked = [&](auto&& unsafePart) {
        std::lock_guard<std::mutex> lock(mut);
        unsafePart();
        frameDirty = true;
        frameEv.notify_all();
        // TODO render the preview
    };

    auto loopThread = [&](auto&& f) {
        return std::thread{[&, f = std::move(f)] {
            while (!terminate) try {
                f();
            } catch (const std::exception&) {
                // Probably just device reconfiguration or whatever.
                // TODO there are TODOs scattered around `captureVideo.cpp` and `captureAudio.cpp`;
                //      these mark actual places where errors can occur due to reconfiguration.
                //      The rest should be fatal.
                Sleep(500);
            }
        }};
    };

    auto videoCaptureThread = loopThread([&] {
        // Wait until the main thread allows capture threads to proceed.
        { std::unique_lock<std::timed_mutex> lk(videoMutex); };
        size_t w = fake.width;
        size_t h = fake.height;
        auto cap = captureScreen(0, w, h);
        while (!terminate) if (auto in = cap->next()) {
            int32_t averageComponents[4] = {0, 0, 0, 0};
            for (const auto& color : in.reinterpret<const BYTE[4]>())
                for (int c = 0; c < 4; c++)
                    averageComponents[c] += color[c];
            auto lk = std::unique_lock<std::timed_mutex>(videoMutex, std::chrono::milliseconds(30));
            if (!lk)
                return;
            averageColor = 0xFF000000u
                        | (UINT)(averageComponents[2] / w / h) << 16
                        | (UINT)(averageComponents[1] / w / h) << 8
                        | (UINT)(averageComponents[0] / w / h);
            updateLocked([&] {
#define W_PX(out, y, x) do { *out++ = in[(y) * w + (x)]; } while (0)
                auto a = frameData[0], b = frameData[1];
                for (auto x = w; x--; ) W_PX(a, h - 1, x); // bottom right -> bottom left
                for (auto y = h; y--; ) W_PX(a, y, 0);     // bottom left -> top left
                for (auto y = h; y--; ) W_PX(b, y, w - 1); // bottom right -> top right
                for (auto x = w; x--; ) W_PX(b, 0, x);     // top right -> top left
#undef W_PX
            });
        }
    });

    auto audioCaptureThread = loopThread([&] {
        { std::unique_lock<std::timed_mutex> lk(audioMutex); };
        double lastH = 0;
        double lastS = 0;
        auto cap = captureDefaultAudioOutput();
        while (!terminate) if (auto in = cap->next()) {
            // 1. Need a lower bound on value so that the strip is always visible at all. 
            // 2. When value is 0, both hue and saturation become undefined (all colors are black),
            //    causing an abrupt switch to white at the end of a fade to black. Keep the old
            //    hue/saturation when any value is OK to avoid that.
            auto [a, h, s, v] = argb2ahsv(u2qd(averageColor));
            auto [_, r, g, b] = ahsv2argb({1, (h = lastH = s ? h : lastH), (s = lastS = v ? s : lastS), std::max(v, 0.5)});
            auto delta = qd2u({0, r * 2 / in.size(), g * 2 / in.size(), b * 2 / in.size()});
            auto lk = std::unique_lock<std::timed_mutex>(audioMutex, std::chrono::milliseconds(30));
            if (!lk)
                return;
            updateLocked([&] {
                size_t j = 0, size = fake.musicLeds / 2;
                for (auto* out : {frameData[2], frameData[3]}) {
                    size_t i = 0;
                    for (size_t k = in.size() / 2; k--; j++)
                        // Yay, hardcoded coefficients! TODO figure out a better mapping.
                        for (size_t w = (size_t)(tanh(in[j] / exp((k + 1) * 0.55) / 0.12) * (size - i)); w--; )
                            out[i++] = 0xFF000000u | (delta * (UINT)(k + 1));
                    while (i < size)
                        out[i++] = 0xFF000000u;
                }
            });
        }
    });

    auto serialThread = loopThread([&] {
        auto port = fake.serial.load();
        auto filename = L"\\\\.\\COM" + std::to_wstring(port);
        serial comm{filename.c_str()};
        while (port == fake.serial && !terminate) {
            std::unique_lock<std::mutex> lock{mut};
            // Ping the arduino at least once per ~2s so that it knows the app is still running.
            if (!frameEv.wait_for(lock, std::chrono::seconds(2), [&]{ return frameDirty; }))
                continue;
            // TODO apply color offsets
            for (uint8_t strip = 0; strip < 4; strip++)
                comm.update(strip, frameData[strip], fake.gamma, strip < 2 ? fake.brightnessV : fake.brightnessA);
            frameDirty = false;
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
                std::fill(std::begin(strip), std::end(strip), 0xFF000000u);
            size_t w = fake.width, h = fake.height, m = fake.musicLeds;
            // The pattern depicted in screensetup.png.
            frameData[0][0] = frameData[1][0] = 0xFF00FFFFu;
            frameData[0][w] = frameData[0][w - 1] = 0xFFFFFF00u;
            frameData[1][h] = frameData[1][h - 1] = 0xFFFF00FFu;
            frameData[0][w + h - 1] = frameData[1][w + h - 1] = 0xFFFFFFFFu;
            // TODO maybe render 2 dots on each instead?
            std::fill(frameData[2], frameData[2] + m / 2, 0xFFFFFF00u);
            std::fill(frameData[3], frameData[3] + m / 2, 0xFF00FFFFu);
        });
    };

    auto setVideoPattern = [&](uint32_t color) {
        if (color & 0xFF000000u) {
            if (!videoLock) videoLock.lock();
            averageColor = color;
            updateLocked([&] {
                size_t s = fake.width + fake.height;
                std::fill(frameData[0], frameData[0] + s, color);
                std::fill(frameData[1], frameData[1] + s, color);
            });
        } else if (videoLock) {
            videoLock.unlock();
        }
    };

    auto setBothPatterns = [&] {
        mainWindow.setNotificationIcon(ui::loadSmallIcon(ui::fromBundled(IDI_APP)), L"Ambilight");
        setVideoPattern(fake.color);
        if (audioLock) {
            updateLocked([&] {
                // There's no guarantee that the audio capturer will have anything on first
                // iteration (might be nothing playing), so clear the test pattern explicitly.
                std::fill(std::begin(frameData[2]), std::end(frameData[2]), 0xFF000000u);
                std::fill(std::begin(frameData[3]), std::end(frameData[3]), 0xFF000000u);
            });
            audioLock.unlock();
        }
    };

    sizingConfig.onDone.addForever([&] { sizingWindow->close(); });
    sizingConfig.onChange.addForever([&](int i, size_t value) {
        switch (i) {
            case 0: fake.width = value; break;
            case 1: fake.height = value; break;
            case 2: fake.musicLeds = value; break;
            case 3: fake.serial = value; break;
        }
        setTestPattern();
    });

    tooltipConfig.onQuit.addForever([&] { mainWindow.close(); });
    tooltipConfig.onSettings.addForever([&] {
        mainWindow.clearNotificationIcon();
        auto w = GetSystemMetrics(SM_CXSCREEN);
        auto h = GetSystemMetrics(SM_CYSCREEN);
        sizingWindow = std::make_unique<ui::window>(800, 800, (w - 800) / 2, (h - 800) / 2);
        sizingWindow->setRoot(&sizingConfig.pad);
        sizingWindow->setBackground(0xcc111111u);
        sizingWindow->setTitle(L"Ambilight Setup");
        sizingWindow->onClose.addForever([&]{ setBothPatterns(); });
        sizingWindow->setShadow(true);
        sizingWindow->show();
        setTestPattern();
    });
    tooltipConfig.onChange.addForever([&](appui::setting s, double v) {
        switch (s) {
            case appui::Y: fake.gamma = v; break;
            case appui::R: fake.dr = v; break;
            case appui::G: fake.dg = v; break;
            case appui::B: fake.db = v; break;
            case appui::Lv: fake.brightnessV = v; break;
            case appui::La: fake.brightnessA = v; break;
        }
        // Ping the serial thread.
        updateLocked([&]{ });
    });
    tooltipConfig.onColor.addForever([&](uint32_t c) {
        setVideoPattern(fake.color = c);
    });

    mainWindow.onNotificationIcon.addForever([&](POINT p, bool primary) {
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
    });

    if (false /* TODO if config not loaded */)
        tooltipConfig.onSettings();
    else
        setBothPatterns();
    return ui::dispatch();
}
