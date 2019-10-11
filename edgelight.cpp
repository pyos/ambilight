#include "dxui/base.hpp"
#include "dxui/resource.hpp"
#include "dxui/window.hpp"
#include "dxui/widgets/button.hpp"
#include "dxui/widgets/grid.hpp"
#include "dxui/widgets/label.hpp"
#include "dxui/widgets/slider.hpp"
#include "dxui/widgets/spacer.hpp"
#include "dxui/widgets/wincontrol.hpp"

int ui::main() {
    ui::window window{800, 600};
    auto onDestroy = window.onDestroy.add([&] { ui::quit(); });

    ui::spacer b{{50, 50}};
    ui::button c{b};
    ui::slider hs1, hs2;
    ui::slider vs1, vs2;
    hs1.setOrientation(ui::slider::deg0);
    vs1.setOrientation(ui::slider::deg90);
    hs2.setOrientation(ui::slider::deg180);
    vs2.setOrientation(ui::slider::deg270);
    ui::font segoe{IDI_FONT_SEGOE_UI};
    ui::font segoe_bold{IDI_FONT_SEGOE_UI_BOLD};
    ui::font cambria{IDI_FONT_CAMBRIA};
    ui::label heading{{{L"This is a heading.", cambria, 60, 0xFFFFFFFFu, false}}};
    ui::label label{{
        {L"The quick ",    segoe,      12, 0xFFFFFFFFu, false},
        {L"brown fox ",    segoe_bold, 12, 0xFFFFAAAAu, false},
        {L"jumps over ",   segoe,      12, 0xFFFFFFFFu, false},
        {L"the lazy dog.", segoe_bold, 12, 0xFFAAAAFFu, true},
        {L"The quick ",    segoe,      18, 0xFFFFFFFFu, false},
        {L"brown fox ",    segoe_bold, 18, 0xFFFFAAAAu, false},
        {L"jumps over ",   segoe,      18, 0xFFFFFFFFu, false},
        {L"the lazy dog.", segoe_bold, 18, 0xFFAAAAFFu, true},
        {L"The quick ",    segoe,      32, 0xFFFFFFFFu, false},
        {L"brown fox ",    segoe_bold, 32, 0xFFFFAAAAu, false},
        {L"jumps over ",   segoe,      32, 0xFFFFFFFFu, false},
        {L"the lazy dog.", segoe_bold, 32, 0xFFAAAAFFu, true},
    }};
    ui::grid grid{3, 5};
    grid.set(1, 0, &heading, ui::grid::align_start);
    grid.set(1, 2, &c);
    grid.set(1, 1, &hs1);
    grid.set(2, 2, &vs1);
    grid.set(1, 3, &hs2);
    grid.set(0, 2, &vs2);
    grid.setColStretch(1, 1);
    grid.setRowStretch(2, 1);
    grid.set(1, 4, &label);
    auto set = [&](POINT p) {
        b.setSize(p);
        hs1.setValue((p.x - 50) / 400.);
        hs2.setValue((p.x - 50) / 400.);
        vs1.setValue((p.y - 50) / 400.);
        vs2.setValue((p.y - 50) / 400.);
    };
    auto onClick = c.onClick.add([&] {
        auto [w, h] = b.size();
        set({w + 10, h + 10});
    });
    auto setW = [&](double v) { set({(LONG)(v * 40 + 5.5) * 10, b.size().y}); };
    auto setH = [&](double v) { set({b.size().x, (LONG)(v * 40 + 5.5) * 10}); };
    auto onChangeW1 = hs1.onChange.add(setW);
    auto onChangeW2 = hs2.onChange.add(setW);
    auto onChangeH1 = vs1.onChange.add(setH);
    auto onChangeH2 = vs2.onChange.add(setH);

    ui::grid mainContentWithPadding{3, 4};
    ui::spacer ltPad{{50, 50}};
    ui::spacer rbPad{{50, 50}};
    mainContentWithPadding.set(0, 0, &ltPad);
    mainContentWithPadding.set(1, 1, &grid);
    mainContentWithPadding.set(2, 2, &rbPad);
    mainContentWithPadding.setPrimaryCell(1, 1);

    ui::grid titlebar{4, 1};
    ui::label title{{{L"DXUI test application", segoe, 16, 0xFFFFFFFFu, false}}};
    title.setHideOverflow(true);
    ui::win_minimize titlebarMinimize{window};
    ui::win_maximize titlebarMaximize{window};
    ui::win_close titlebarClose{window};
    titlebar.set(0, 0, &title, ui::grid::align_global_center);
    titlebar.set(1, 0, &titlebarMinimize);
    titlebar.set(2, 0, &titlebarMaximize);
    titlebar.set(3, 0, &titlebarClose);
    titlebar.setColStretch(0, 1);

    ui::grid mainContentWithTitlebar{1, 2};
    mainContentWithTitlebar.set(0, 0, &titlebar);
    mainContentWithTitlebar.set(0, 1, &mainContentWithPadding);
    mainContentWithTitlebar.setPrimaryCell(0, 1);
    window.setRoot(&mainContentWithTitlebar);

    window.setBackground(0x7F000000u);
    window.show();
    return ui::dispatch();
}
