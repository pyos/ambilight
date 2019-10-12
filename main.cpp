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

#include <string>

namespace appui {
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
    struct controlled_number {
        controlled_number(ui::grid& grid, size_t row, util::span<const wchar_t> name,
                          size_t min, size_t max, size_t step)
            : label({15, 15}, {name, ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()})
            , min(min)
            , max(max)
            , step(step)
        {
            grid.set(0, row, &label.pad, ui::grid::align_end);
            grid.set(1, row, &decButton);
            grid.set(2, row, &numLabel);
            grid.set(3, row, &incButton);
            // Initialize the label.
            decButton.setBorderless(true);
            incButton.setBorderless(true);
        }

        size_t getValue() const {
            return value;
        }

        virtual void setValue(size_t v) {
            value = std::max(std::min(v - v % step, max), min);
            numBuffer = std::to_wstring(value);
            numLabel.setText({{numBuffer, ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}});
            onChange(value);
        }

    public:
        util::event<size_t> onChange;

    protected:
        size_t min;
        size_t max;
        size_t step;
        size_t value = 0;
    protected:
        ui::label decLabel{{{L"\uf068", ui::font::loadPermanently<IDI_FONT_ICONS>()}}};
        ui::label incLabel{{{L"\uf067", ui::font::loadPermanently<IDI_FONT_ICONS>()}}};
        padded_label label;
        ui::label numLabel{{}};
        ui::button decButton{decLabel};
        ui::button incButton{incLabel};
    private:
        util::event<>::handle decHandler = decButton.onClick.add([this]{ setValue(value - step); });
        util::event<>::handle incHandler = incButton.onClick.add([this]{ setValue(value + step); });
        std::wstring numBuffer;
    };

    // name [-] ---|----------- [+] 123
    struct controlled_slider : controlled_number {
        controlled_slider(ui::grid& grid, size_t row, util::span<const wchar_t> name,
                          size_t min, size_t max, size_t step)
            : controlled_number(grid, row, name, min, max, step)
        {
            grid.set(2, row, &slider.pad);
            grid.set(4, row, &numLabel);
        }

        void setValue(size_t v) override {
            slider.setValue(v, min, max, step);
            controlled_number::setValue(v);
        }

    private:
        padded<ui::slider> slider{{10, 15}};
        util::event<double>::handle sliderHandler = slider.onChange.add([this](double) {
            setValue(slider.mapValue(min, max, step)); });
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
    struct sizing_config : ui::grid {
        sizing_config(size_t wv, size_t hv, size_t ev, size_t sn)
            : ui::grid(1, 4)
        {
            set(0, 0, &image);
            set(0, 1, &sliderGrid.pad);
            set(0, 2, &help.pad);
            set(0, 3, &bottomRow.pad);
            setRowStretch(0, 1);
            setColStretch(0, 1);
            sliderGrid.set(4, 3, &constNumWidth);
            sliderGrid.setColStretch(2, 1);
            bottomRow.set(5, 0, &done);
            bottomRow.setColStretch(4, 1);
            w.setValue(wv);
            h.setValue(hv);
            e.setValue(ev);
            s.setValue(sn);
        }

    public:
        util::event<size_t /* parameter */, size_t /* new value */> onChange;
        util::event<> onDone;

    private:
        static constexpr size_t LIMIT = AMBILIGHT_SERIAL_CHUNK * AMBILIGHT_CHUNKS_PER_STRIP;
        screensetup image;
        padded_label help{{35, 20}, {L"Tweak the values until you get the pattern shown above.",
                                     ui::font::loadPermanently<IDI_FONT_SEGOE_UI>(), 22}};
        padded<ui::grid> sliderGrid{{20, 0}, 5, 4};
        padded<ui::grid> bottomRow{{35, 20}, 6, 1};
        // The actual limit is 1 <= w + h <= LIMIT, but sliders jumping around
        // would probably be confusing.
        controlled_slider w{sliderGrid, 0, L"Screen width",  1, LIMIT, 1};
        controlled_slider h{sliderGrid, 1, L"Screen height", 1, LIMIT, 1};
        controlled_slider e{sliderGrid, 2, L"Music LEDs",    2, LIMIT, 2};
        controlled_number s{bottomRow,  0, L"Serial port",   1, 16,    1};
        // A hacky way to set a constant size for the number labels:
        ui::spacer constNumWidth{{60, 1}};
        padded_label doneLabel{{20, 0}, {L"Done", ui::font::loadPermanently<IDI_FONT_SEGOE_UI>()}};
        ui::button done{doneLabel.pad};

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

    struct state {
        double brightnessV;
        double brightnessA;
        double gamma;
        double dr;
        double dg;
        double db;
        uint32_t color;
    };

    struct color_config_tab : gray_bg {
        color_config_tab(const state& init)
            : gray_bg(grid.pad)
        {
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

    public:
        util::event<int, double> onChange;

    private:
        padded<ui::grid> grid{{10, 20}, 2, 4};
        padded_label gammaLabel{{10, 10}, {L"\uf042", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        padded_label rOffLabel{{10, 10}, {L"R", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}};
        padded_label gOffLabel{{10, 10}, {L"G", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}};
        padded_label bOffLabel{{10, 10}, {L"B", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}};
        padded<ui::slider> gammaSlider{{10, 10}};
        padded<ui::slider> rOffSlider{{10, 10}};
        padded<ui::slider> gOffSlider{{10, 10}};
        padded<ui::slider> bOffSlider{{10, 10}};
        util::event<double>::handle gammaHandler = gammaSlider.onChange.add([this](double value) { return onChange(0, value * 2 + 1); });
        util::event<double>::handle rOffHandler = rOffSlider.onChange.add([this](double value) { return onChange(1, value); });
        util::event<double>::handle gOffHandler = gOffSlider.onChange.add([this](double value) { return onChange(2, value); });
        util::event<double>::handle bOffHandler = bOffSlider.onChange.add([this](double value) { return onChange(3, value); });
    };

    struct color_select_tab : gray_bg {
        color_select_tab(const state& init, util::event<uint32_t>& onChange)
            : gray_bg(grid.pad)
            , onChange(onChange)
        {
            grid.set(0, 0, &hueLabel.pad, ui::grid::align_end);
            grid.set(1, 0, &hueSlider.pad);
            grid.set(0, 1, &satLabel.pad, ui::grid::align_end);
            grid.set(1, 1, &satSlider.pad);
            grid.setColStretch(1, 1);
            // TODO convert color to HSV & set sliders
        }

        bool emitEvent() {
            // TODO convert values to RGB & emit onChange
            return onChange(0x00000000u);
        }

    private:
        util::event<uint32_t>& onChange;
        padded<ui::grid> grid{{10, 20}, 2, 2};
        padded_label hueLabel{{10, 10}, {L"Hue", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}};
        padded_label satLabel{{10, 10}, {L"Saturation", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}};
        padded<h_slider> hueSlider{{10, 10}};
        padded<s_slider> satSlider{{10, 10}};
        util::event<double>::handle hueHandler = hueSlider.onChange.add([this](double) { return emitEvent(); });
        util::event<double>::handle satHandler = satSlider.onChange.add([this](double) { return emitEvent(); });
    };

    struct extra_button : gray_bg {
        extra_button(const ui::text_part& text)
            : gray_bg(button)
            , label({text})
        {
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
        extra_tabs(const state& init)
            : ui::grid(1, 3)
            , gammaTab(init)
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

    public:
        util::event<int, double> onGamma;
        util::event<uint32_t> onColor;

    private:
        color_config_tab gammaTab;
        color_select_tab colorTab;

        ui::grid buttons{2, 1};
        extra_button gammaButton{{L"\uf0d0", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        extra_button colorButton{{L"\uf043", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        util::event<bool>::handle gammaH = gammaButton.onClick.add([this](bool show) {
            set(0, 0, show ? &gammaTab : nullptr); });
        util::event<bool>::handle colorH = colorButton.onClick.add([this](bool show) {
            set(0, 1, show ? &colorTab : nullptr);
            return colorTab.emitEvent(); });
    };

    // +------------------------------+
    // | B -----|-------------------- |    (pretend those letters are icons signifying
    // | M -----|-------------------- |     Brightness, Music, Settings, and Quit)
    // |                      [S] [Q] |
    // +------------------------------+
    struct tooltip_config : ui::grid {
        tooltip_config(const state& init)
            : ui::grid(1, 2)
            , extraTabs(init)
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
            statusText.setText({{msg, ui::font::loadPermanently<IDI_FONT_SEGOE_UI>(), 18, 0x80FFFFFFu}});
        }

    public:
        util::event<int, double> onBrightness;
        util::event<> onSettings;
        util::event<> onQuit;

    private:
        extra_tabs extraTabs;

        padded<ui::grid> onTransparent{{10, 20}, 1, 3};
        padded<ui::grid> brightnessGrid{{10, 0}, 2, 2};
        padded_label bLabel{{10, 10}, {L"\uf185", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        padded_label mLabel{{10, 10}, {L"\uf001", ui::font::loadPermanently<IDI_FONT_ICONS>()}};
        padded<v_slider> bSlider{{10, 10}};
        padded<v_slider> mSlider{{10, 10}};

        padded<ui::grid> bottomRow{{10, 0}, 3, 1};
        padded<ui::label> statusText{{10, 10}};
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

    const appui::state fake = {
        0.7, 0.4, 2.0, 1.0, 1.0, 1.0, 0x00000000u
    };

    std::unique_ptr<ui::window> sizingWindow;
    std::unique_ptr<ui::window> tooltipWindow;
    appui::padded<appui::sizing_config> sizingConfig{{20, 20}, 72, 40, 76, 3};
    appui::tooltip_config tooltipConfig{fake};

    sizingConfig.onDone.add([&] {
        if (sizingWindow)
            sizingWindow->close();
        mainWindow.setNotificationIcon(ui::loadSmallIcon(ui::fromBundled(IDI_APP)), L"Ambilight");
    }).release();

    tooltipConfig.setStatusMessage(L"Serial port offline");
    tooltipConfig.onQuit.add([&] { mainWindow.close(); }).release();
    tooltipConfig.onSettings.add([&] {
        mainWindow.clearNotificationIcon();
        auto w = GetSystemMetrics(SM_CXSCREEN);
        auto h = GetSystemMetrics(SM_CYSCREEN);
        sizingWindow = std::make_unique<ui::window>(800, 800, (w - 800) / 2, (h - 800) / 2);
        sizingWindow->setRoot(&sizingConfig.pad);
        sizingWindow->setBackground(0xa0000000u, true);
        sizingWindow->setTopmost();
        sizingWindow->show();
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

    if (false /*config not loaded*/) {
        tooltipConfig.onSettings();
    } else {
        sizingConfig.onDone();
    }
    return ui::dispatch();
}
