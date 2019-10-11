#include "button.hpp"
#include "data.hpp"

winapi::com_ptr<ID3D11Texture2D> ui::button::getTexture(ui::dxcontext& ctx) const {
    return ctx.cachedTexture<builtinTexture>();
}

RECT ui::button::getOuter() const {
    switch (getState()) {
        default:     return builtinRect(borderless ? BUTTON_BORDERLESS_OUTER : BUTTON_OUTER);
        case hover:  return builtinRect(borderless ? BUTTON_BORDERLESS_OUTER_HOVER : BUTTON_OUTER_HOVER);
        case active: return builtinRect(borderless ? BUTTON_BORDERLESS_OUTER_ACTIVE : BUTTON_OUTER_ACTIVE);
    }
}

RECT ui::button::getInner() const {
    switch (getState()) {
        default:     return builtinRect(borderless ? BUTTON_BORDERLESS_INNER : BUTTON_INNER);
        case hover:  return builtinRect(borderless ? BUTTON_BORDERLESS_INNER_HOVER : BUTTON_INNER_HOVER);
        case active: return builtinRect(borderless ? BUTTON_BORDERLESS_INNER_ACTIVE : BUTTON_INNER_ACTIVE);
    }
}
