#pragma once

#include "button.hpp"
#include "grid.hpp"
#include "../window.hpp"

namespace ui {
#define DXUI_GENERATE_BUTTON(name, clickAction) \
    struct name : button {                                                              \
        name(window& target)                                                            \
            : button(icon)                                                              \
            , doIt(onClick.add([&]{ clickAction; return true; }))                       \
        {}                                                                              \
    protected:                                                                          \
        winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext&) const override;     \
        RECT getOuter() const override;                                                 \
        RECT getInner() const override;                                                 \
    private:                                                                            \
        struct icon : texrect {                                                         \
            winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext&) const override; \
            RECT getOuter() const override;                                             \
        } icon;                                                                         \
        util::event<>::handle doIt;                                                     \
    }

    DXUI_GENERATE_BUTTON(win_close, target.close());
    DXUI_GENERATE_BUTTON(win_maximize, target.isMaximized() ? target.show() : target.maximize());
    DXUI_GENERATE_BUTTON(win_minimize, target.minimize());

#undef DXUI_GENERATE_BUTTON
}
