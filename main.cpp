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
        padded<ui::button> decButton{{5, 0}, decLabel};
        padded<ui::button> incButton{{5, 0}, incLabel};
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
        ui::button done{doneLabel.pad};
        // The actual limit is 1 <= w + h <= LIMIT, but sliders jumping around
        // would probably be confusing.
        static constexpr size_t LIMIT = AMBILIGHT_SERIAL_CHUNK * AMBILIGHT_CHUNKS_PER_STRIP;
        controlled_number w{sliderGrid, 0, L"Screen width",  1, LIMIT, 1, true};
        controlled_number h{sliderGrid, 1, L"Screen height", 1, LIMIT, 1, true};
        controlled_number e{sliderGrid, 2, L"Music LEDs",    2, LIMIT, 2, true};
        controlled_number s{bottomRow,  0, L"Serial port",   1, 16,    1};
        // A hacky way to set a constant size for the number labels:
        ui::spacer constNumWidth{{60, 1}};

        util::event<size_t>::handle wHandler = w.onChange.add([this](size_t v) { return onChange(0, v); });
        util::event<size_t>::handle hHandler = h.onChange.add([this](size_t v) { return onChange(1, v); });
        util::event<size_t>::handle eHandler = e.onChange.add([this](size_t v) { return onChange(2, v); });
        util::event<size_t>::handle sHandler = s.onChange.add([this](size_t v) { return onChange(3, v); });
        util::event<>::handle doneHandler = done.onClick.add([this]{ return onDone(); });
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
    protected:
        winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext& ctx) const override {
            return ctx.cachedTexture<extraWidgets>(); }
        RECT getOuter() const override { return {15, 163, 16, 164}; }
        RECT getInner() const override { return getOuter(); }
    };

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
        padded_label gammaLabel{{10, 10}, {L"\uf042", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22}};
        padded_label rOffLabel{{10, 10}, {L"\uf185", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22, 0xFFFF3333u}};
        padded_label gOffLabel{{10, 10}, {L"\uf185", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22, 0xFF33FF33u}};
        padded_label bOffLabel{{10, 10}, {L"\uf185", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22, 0xFF3333FFu}};
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
        padded<texslider<0>> hueSlider{{10, 10}};
        padded<texslider<1>> satSlider{{10, 10}};
        util::event<double>::handle hueHandler = hueSlider.onChange.add([this](double) { return onChange(value()); });
        util::event<double>::handle satHandler = satSlider.onChange.add([this](double) { return onChange(value()); });
    };

    struct extra_button : gray_bg {
        extra_button(const ui::text_part& text, bool state = false)
            : state(state), label({10, 0}, {text})
        {
            setContents(&button);
            button.setBorderless(true);
        }

    public:
        util::event<bool> onClick;

    protected:
        winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext& ctx) const override {
            return state ? gray_bg::getTexture(ctx) : winapi::com_ptr<ID3D11Texture2D>{};
        }

    private:
        bool state;
        padded_label label;
        ui::button button{label.pad};
        util::event<>::handle h = button.onClick.add([this] { invalidate(); return onClick(state ^= 1); });
    };

    struct tooltip_config : ui::grid {
        tooltip_config(const state& init)
            : ui::grid(1, 5)
            , gammaTab(init, onGamma)
            , colorTab(init, onColor)
            , gammaButton({L"\uf0d0", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22})
            , colorButton({L"\uf043", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22}, init.color >> 24)
        {
            if (init.color >> 24)
                set(0, 0, &colorTab);
            set(0, 3, &brightnessGrid.pad);
            set(0, 4, &buttons);
            setColStretch(0, 1);

            buttons.set(0, 0, &gammaButton);
            buttons.set(1, 0, &colorButton);
            buttons.set(3, 0, &sButton);
            buttons.set(4, 0, &qButton);
            buttons.setColStretch(2, 1);
            sButton.setBorderless(true);
            qButton.setBorderless(true);

            brightnessGrid.set(0, 0, &bLabel.pad);
            brightnessGrid.set(1, 0, &bSlider.pad);
            brightnessGrid.set(0, 1, &mLabel.pad);
            brightnessGrid.set(1, 1, &mSlider.pad);
            brightnessGrid.setColStretch(1, 1);
            bSlider.setValue(init.brightnessV);
            mSlider.setValue(init.brightnessA);
        }

    public:
        util::event<int, double> onBrightness; // 0 = video, 1 = music
        util::event<int, double> onGamma;      // 0 = gamma, 1..3 = rgb offsets
        util::event<uint32_t> onColor;         // 0 = live, 0xFF?????? = constant
        util::event<> onSettings;
        util::event<> onQuit;

    private:
        ui::grid buttons{5, 1};
        color_config_tab gammaTab;
        color_select_tab colorTab;
        extra_button gammaButton;
        extra_button colorButton;
        padded_label sLabel{{10, 0}, {L"\uf013", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22}};
        padded_label qLabel{{10, 0}, {L"\uf011", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22}};
        ui::button sButton{sLabel.pad};
        ui::button qButton{qLabel.pad};

        padded<ui::grid> brightnessGrid{{10, 10}, 2, 2};
        padded_label bLabel{{10, 10}, {L"\uf185", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22}};
        padded_label mLabel{{10, 10}, {L"\uf001", ui::font::loadPermanently<IDI_FONT_ICONS>(), 22}};
        padded<texslider<2>> bSlider{{10, 10}};
        padded<texslider<2>> mSlider{{10, 10}};

        util::event<bool>::handle gammaH = gammaButton.onClick.add([this](bool show) {
            set(0, 0, show ? &gammaTab : nullptr);
        });
        util::event<bool>::handle colorH = colorButton.onClick.add([this](bool show) {
            set(0, 1, show ? &colorTab : nullptr);
            return onColor(colorTab.value() & (show ? 0xFFFFFFFFu : 0x00FFFFFFu));
        });

        util::event<double>::handle bHandler = bSlider.onChange.add([this](double v) { return onBrightness(0, v); });
        util::event<double>::handle mHandler = mSlider.onChange.add([this](double v) { return onBrightness(1, v); });
        util::event<>::handle sHandler = sButton.onClick.add([this]{ return onSettings(); });
        util::event<>::handle qHandler = qButton.onClick.add([this]{ return onQuit(); });
    };
}

int ui::main() {
    ui::window mainWindow{1, 1};
    mainWindow.onDestroy.add([&] { ui::quit(); }).release();
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
        for (size_t iter = 0; port == fake.serial && !terminate; iter++) {
            if (auto lock = std::unique_lock<std::mutex>(mut)) {
                // Ping the arduino at least once per ~1.6s so that it knows the app is still running.
                if (iter % 16 && !frameEv.wait_for(lock, std::chrono::milliseconds(100), [&]{ return frameDirty; }))
                    continue;
                // TODO apply color offsets
                for (uint8_t strip = 0; strip < 4; strip++)
                    comm.update(strip, frameData[strip], fake.gamma, strip < 2 ? fake.brightnessV : fake.brightnessA);
                frameDirty = false;
            }
            comm.submit();
        }
    });

    DEFER {
        terminate = true;
        // Must release the locks first to allow the threads to actually check the flag.
        if (videoLock) videoLock.unlock();
        if (audioLock) audioLock.unlock();
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
            // The pattern depicted in screensetup.png.
            size_t w = fake.width, h = fake.height, m = fake.musicLeds, s = w + h;
            frameData[0][0] = frameData[1][0] = 0xFF00FFFFu;
            frameData[0][w] = frameData[0][w - 1] = 0xFFFFFF00u;
            frameData[1][h] = frameData[1][h - 1] = 0xFFFF00FFu;
            frameData[0][s - 1] = frameData[1][s - 1] = 0xFFFFFFFFu;
            std::fill(frameData[2], frameData[2] + m / 2, 0xFFFFFF00u);
            std::fill(frameData[3], frameData[3] + m / 2, 0xFF00FFFFu);
        });
    };

    auto setVideoPattern = [&](uint32_t color) {
        if (color & 0xFF000000) {
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

    sizingConfig.onDone.add([&] {
        if (sizingWindow)
            sizingWindow->close();
        mainWindow.setNotificationIcon(ui::loadSmallIcon(ui::fromBundled(IDI_APP)), L"Ambilight");
        setVideoPattern(fake.color);
        if (audioLock) {
            updateLocked([&] {
                std::fill(std::begin(frameData[2]), std::end(frameData[2]), 0xFF000000u);
                std::fill(std::begin(frameData[3]), std::end(frameData[3]), 0xFF000000u);
            });
            audioLock.unlock();
        }
    }).release();
    sizingConfig.onChange.add([&](int i, size_t value) {
        switch (i) {
            case 0: fake.width = value; break;
            case 1: fake.height = value; break;
            case 2: fake.musicLeds = value; break;
            case 3: fake.serial = value; break;
        }
        setTestPattern();
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
        updateLocked([&]{ });
    }).release();
    tooltipConfig.onGamma.add([&](int i, double v) {
        switch (i) {
            case 0: fake.gamma = v; break;
            case 1: fake.dr = v; break;
            case 2: fake.dg = v; break;
            case 3: fake.db = v; break;
        }
        updateLocked([&]{ });
    }).release();
    tooltipConfig.onColor.add([&](uint32_t c) {
        setVideoPattern(fake.color = c);
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

    if (false /* TODO if config not loaded */)
        tooltipConfig.onSettings();
    else
        sizingConfig.onDone();
    return ui::dispatch();
}
