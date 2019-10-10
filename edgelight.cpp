#include "dxui/base.hpp"
#include "dxui/resource.hpp"
#include "dxui/window.hpp"

#include "resource.h"

int ui::main() {
    auto asd = ui::read(ui::fromBundled(IDI_WIDGETS), L"PNG");
    auto cursor = ui::loadCursor(ui::fromFile(IDC_ARROW));
    auto iconLg = ui::loadNormalIcon(ui::fromBundled(IDI_APP));
    auto iconSm = ui::loadSmallIcon(ui::fromBundled(IDI_APP));
    ui::spacer a{{50, 50}};
    ui::spacer b{{50, 50}};
    ui::button c{b};
    ui::spacer d{{50, 50}};
    ui::slider hs1, hs2;
    ui::slider vs1, vs2;
    hs1.setOrientation(ui::slider::deg0);
    vs1.setOrientation(ui::slider::deg90);
    hs2.setOrientation(ui::slider::deg180);
    vs2.setOrientation(ui::slider::deg270);
    ui::grid grid{3, 3};
    grid.set(0, 0, &a);
    grid.set(1, 1, &c);
    grid.set(2, 2, &d);
    grid.set(1, 0, &hs1);
    grid.set(2, 1, &vs1);
    grid.set(1, 2, &hs2);
    grid.set(0, 1, &vs2);
    grid.setColStretch(1, 1);
    grid.setRowStretch(1, 1);
    auto window = ui::window(L"edgelight", cursor, iconLg, iconSm, 800, 600, WS_OVERLAPPEDWINDOW);
    auto onDestroy = window.onDestroy.add([&] { ui::quit(); return true; });
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
        return true;
    });
    auto setW = [&](double v) {
        auto [_, h] = b.size();
        set({(LONG)(v * 40 + 5.5) * 10, h});
        return true;
    };
    auto setH = [&](double v) {
        auto [w, _] = b.size();
        set({w, (LONG)(v * 40 + 5.5) * 10});
        return true;
    };
    auto onChangeW1 = hs1.onChange.add(setW);
    auto onChangeW2 = hs2.onChange.add(setW);
    auto onChangeH1 = vs1.onChange.add(setH);
    auto onChangeH2 = vs2.onChange.add(setH);
    window.setRoot(&grid);
    window.makeTransparent(0x20000000u);
    window.show();
    return ui::dispatch();
}
