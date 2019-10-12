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

    // name [-] N [+]
    struct controlled_number {
        controlled_number(ui::grid& grid, size_t row, util::span<const wchar_t> name,
                          size_t min, size_t max, size_t step)
            : label({15, 15}, std::vector<ui::text_part>{{name, ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}})
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
        // TODO use icons instead
        ui::label decLabel{{{L"-", ui::font::loadPermanently<IDI_FONT_SEGOE_UI>()}}};
        ui::label incLabel{{{L"+", ui::font::loadPermanently<IDI_FONT_SEGOE_UI>()}}};
        padded<ui::label> label;
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

    struct screensetup : ui::grid {
        screensetup()
            : ui::grid(3, 3)
        {
            set(1, 1, &screen);
            set(1, 2, &strip);
            setColStretch(0, 1); setColStretch(1, 6); setColStretch(2, 1);
            setRowStretch(0, 1); setRowStretch(1, 6); setRowStretch(2, 1);
            strip.set(1, 0, &stripLeft, ui::grid::align_end);
            strip.set(2, 0, &stripRight, ui::grid::align_start);
            strip.setColStretch(0, 1);
            strip.setColStretch(1, 3);
            strip.setColStretch(2, 3);
            strip.setColStretch(3, 1);
        }

    private:
        static winapi::com_ptr<ID3D11Texture2D> loadTexture(ui::dxcontext& ctx) {
            return ctx.textureFromPNG(ui::read(ui::fromBundled(IDI_SCREENSETUP), L"PNG")); }

        struct fixedAspectRatio : ui::widget {
            POINT measureMinImpl() const override { return {60, 1}; }
            POINT measureImpl(POINT fit) const override {
                return {std::min(fit.x, fit.y * 16 / 9 + 60), std::min(fit.x * 9 / 16 - 33, fit.y)}; }
            void drawImpl(ui::dxcontext&, ID3D11Texture2D*, RECT, RECT) const override {}
        } stretch16To9;

        struct screen : ui::texrect {
            using ui::texrect::texrect;
            winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext& ctx) const override {
                return ctx.cachedTexture<loadTexture>(); }
            RECT getOuter() const override { return {0, 0, 128, 128}; }
            RECT getInner() const override { return {64, 64, 64, 64}; }
        } screen{stretch16To9};

        struct hstretch : ui::widget  {
            POINT measureMinImpl() const override { return {0, 0}; }
            POINT measureImpl(POINT fit) const override { return {fit.x, 0}; }
            void drawImpl(ui::dxcontext&, ID3D11Texture2D*, RECT, RECT) const override {}
        } stretchAnyTo0x1, stretchAnyTo0x2;

        struct stripLeft : screen {
            using screen::screen;
            RECT getOuter() const override { return {22, 128, 64, 154}; }
            RECT getInner() const override { return {44, 142, 44, 142}; }
        } stripLeft{stretchAnyTo0x1};

        struct stripRight : stripLeft {
            using stripLeft::stripLeft;
            RECT getOuter() const override { return {64, 128, 106, 154}; }
            RECT getInner() const override { return {84, 142, 84, 142}; }
        } stripRight{stretchAnyTo0x2};

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
        padded<ui::label> help{{35, 20},
            std::vector<ui::text_part>{{L"Tweak the values until you get the pattern shown above.",
                                        ui::font::loadPermanently<IDI_FONT_SEGOE_UI>(), 22}}};
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
        padded<ui::label> doneLabel{{20, 0},
            std::vector<ui::text_part>{{L"Done", ui::font::loadPermanently<IDI_FONT_SEGOE_UI>()}}};
        ui::button done{doneLabel.pad};

        util::event<size_t>::handle wHandler = w.onChange.add([this](size_t v) { return onChange(0, v); });
        util::event<size_t>::handle hHandler = h.onChange.add([this](size_t v) { return onChange(1, v); });
        util::event<size_t>::handle eHandler = e.onChange.add([this](size_t v) { return onChange(2, v); });
        util::event<size_t>::handle sHandler = s.onChange.add([this](size_t v) { return onChange(3, v); });
        util::event<>::handle doneHandler = done.onClick.add([this]{ return onDone(); });
    };

    // +------------------------------+
    // | B -----|-------------------- |    (pretend those letters are icons signifying
    // | M -----|-------------------- |     Brightness, Music, Settings, and Quit)
    // |                      [S] [Q] |
    // +------------------------------+    TODO static colors
    struct tooltip_config : ui::grid {
        tooltip_config(double bv, double ba)
            : ui::grid(1, 2)
        {
            set(0, 0, &brightnessGrid);
            set(0, 1, &bottomRow);
            setColStretch(0, 1);
            brightnessGrid.set(0, 0, &bLabel.pad);
            brightnessGrid.set(1, 0, &bSlider.pad);
            brightnessGrid.set(0, 1, &mLabel.pad);
            brightnessGrid.set(1, 1, &mSlider.pad);
            brightnessGrid.setColStretch(1, 1);
            bottomRow.set(0, 0, &statusText.pad, ui::grid::align_start);
            bottomRow.set(1, 0, &sButton.pad);
            bottomRow.set(2, 0, &qButton.pad);
            bottomRow.setColStretch(0, 1);
            bSlider.setValue(bv);
            mSlider.setValue(ba);
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
        padded<ui::grid> brightnessGrid{{10, 0}, 2, 2};
        padded<ui::label> bLabel{{10, 10},
            std::vector<ui::text_part>{{L"B", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}}};
        padded<ui::label> mLabel{{10, 10},
            std::vector<ui::text_part>{{L"M", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}}};
        padded<ui::slider> bSlider{{10, 10}};
        padded<ui::slider> mSlider{{10, 10}};

        padded<ui::grid> bottomRow{{10, 0}, 3, 1};
        padded<ui::label> statusText{{10, 10}};
        ui::label sLabel{{{L"S", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}}};
        ui::label qLabel{{{L"Q", ui::font::loadPermanently<IDI_FONT_SEGOE_UI_BOLD>()}}};
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

    std::unique_ptr<ui::window> sizingWindow;
    std::unique_ptr<ui::window> tooltipWindow;
    appui::padded<appui::sizing_config> sizingConfig{{20, 20}, 72, 40, 76, 3};
    appui::padded<appui::tooltip_config> tooltipConfig{{0, 10}, 0.7, 0.4};

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
        tooltipWindow->setRoot(&tooltipConfig.pad);
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
