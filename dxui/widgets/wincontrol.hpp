#pragma once

#include "button.hpp"
#include "../window.hpp"

namespace ui {
#define DXUI_GENERATE_BUTTON(name, rect, iconRect, clickAction) \
    struct name : button {                                                              \
        name() { setContents(&icon); onClick.addForever([&]{                            \
            if (auto __w = parentWindow()) clickAction(*__w), invalidate(); }); }       \
    protected:                                                                          \
        RECT getOuter() const override {                                                \
            switch (currentState) {                                                     \
            default:     return builtinRect(rect##_OUTER);                              \
            case hover:  return builtinRect(rect##_OUTER_HOVER);                        \
            case active: return builtinRect(rect##_OUTER_ACTIVE);                       \
            }                                                                           \
        }                                                                               \
        RECT getInner() const override {                                                \
            switch (currentState) {                                                     \
            default:     return builtinRect(rect##_INNER);                              \
            case hover:  return builtinRect(rect##_INNER_HOVER);                        \
            case active: return builtinRect(rect##_INNER_ACTIVE);                       \
            }                                                                           \
        }                                                                               \
    private:                                                                            \
        image<builtinTexture> icon{builtinRect(iconRect)};                              \
    }

    DXUI_GENERATE_BUTTON(win_close, WIN_CLOSE, WIN_ICON_CLOSE,
        [](auto& w) { w.close(); });
    DXUI_GENERATE_BUTTON(win_maximize, WIN_FRAME,
        [](auto* w) { return w && w->isMaximized() ? WIN_ICON_UNMAXIMIZE : WIN_ICON_MAXIMIZE; }(parentWindow()),
        [](auto& w) { w.isMaximized() ? w.show() : w.maximize(); });
    DXUI_GENERATE_BUTTON(win_minimize, WIN_FRAME, WIN_ICON_MINIMIZE,
        [](auto& w) { w.minimize(); });

#undef DXUI_GENERATE_BUTTON
}
