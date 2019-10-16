#include "data.hpp"
#include "wincontrol.hpp"

#define DXUI_GENERATE_BUTTON(name, rect, iconRect) \
    RECT ui::name::getOuter() const {                                                       \
        switch (getState()) {                                                               \
            default:     return builtinRect(rect##_OUTER);                                  \
            case hover:  return builtinRect(rect##_OUTER_HOVER);                            \
            case active: return builtinRect(rect##_OUTER_ACTIVE);                           \
        }                                                                                   \
    }                                                                                       \
    RECT ui::name::getInner() const {                                                       \
        switch (getState()) {                                                               \
            default:     return builtinRect(rect##_INNER);                                  \
            case hover:  return builtinRect(rect##_INNER_HOVER);                            \
            case active: return builtinRect(rect##_INNER_ACTIVE);                           \
        }                                                                                   \
    }                                                                                       \
    RECT ui::name::icon::getOuter() const {                                                 \
        return builtinRect(iconRect);                                                       \
    }

DXUI_GENERATE_BUTTON(win_close, WIN_CLOSE, WIN_ICON_CLOSE);
DXUI_GENERATE_BUTTON(win_maximize, WIN_FRAME, WIN_ICON_MAXIMIZE);
DXUI_GENERATE_BUTTON(win_minimize, WIN_FRAME, WIN_ICON_MINIMIZE);

#undef DXUI_GENERATE_BUTTON
