#include "button.hpp"
#include "data.hpp"

winapi::com_ptr<ID3D11Texture2D> ui::button::getTexture(ui::dxcontext& ctx) const {
    return ctx.cachedTexture<builtinTexture>();
}

RECT ui::button::getOuter() const {
    switch (currentState) {
        default:     return builtinRect(BUTTON_OUTER);
        case hover:  return builtinRect(BUTTON_OUTER_HOVER);
        case active: return builtinRect(BUTTON_OUTER_ACTIVE);
    }
}

RECT ui::button::getInner() const {
    switch (currentState) {
        default:     return builtinRect(BUTTON_INNER);
        case hover:  return builtinRect(BUTTON_INNER_HOVER);
        case active: return builtinRect(BUTTON_INNER_ACTIVE);
    }
}
