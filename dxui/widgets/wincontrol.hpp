#pragma once

#include "button.hpp"
#include "../window.hpp"

namespace ui {
#define DXUI_GENERATE_BUTTON(name, clickAction) \
    struct name : button {                                                              \
        name() : button(icon) { onClick.addForever([&]{                                 \
            if (auto __w = parentWindow()) clickAction(*__w); }); }                     \
    protected:                                                                          \
        RECT getOuter() const override;                                                 \
        RECT getInner() const override;                                                 \
    private:                                                                            \
        struct icon : texrect {                                                         \
            RECT getOuter() const override;                                             \
        } icon;                                                                         \
    }

    DXUI_GENERATE_BUTTON(win_close, [](window& w) { w.close(); });
    DXUI_GENERATE_BUTTON(win_maximize, [](window& w) { w.isMaximized() ? w.show() : w.maximize(); });
    DXUI_GENERATE_BUTTON(win_minimize, [](window& w) { w.minimize(); });

#undef DXUI_GENERATE_BUTTON
}
