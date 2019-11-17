#pragma once

#include "button.hpp"

namespace ui {
    // A checkbox with two or three state.
    //   +-----+ +-----+ +-----+
    //   |     | |   / | | \ / |
    //   |     | | \/  | | / \ |
    //   +-----+ +-----+ +-----+
    // NOTE: it does not switch states on click by itself, as the desired transitions
    //       are unclear in tri-state mode.
    struct checkbox : button {
        enum mode_t { twostate, tristate };
        enum state_t { unchecked, checked, partial };

        mode_t mode() const { return m; }
        state_t state() const { return s; }

        void setMode(mode_t nm) {
            if (m != nm)
                m = nm;
            if (s == partial && m == twostate)
                setState(checked);
        }

        void setState(state_t ns) {
            if (s != ns)
                s = ns, invalidate();
        }

    private:
        RECT getInner() const override { return ui::texrect::getInner(); }
        RECT getOuter() const override {
            auto b = button::currentState;
            return builtinRect(
                s == checked ?
                    b == button::hover  ? CHECKBOX_CHECKED_HOVER :
                    b == button::active ? CHECKBOX_CHECKED_ACTIVE :
                  /*b == button::idle ?*/ CHECKBOX_CHECKED :
                s == partial ?
                    b == button::hover  ? CHECKBOX_PARTIAL_HOVER :
                    b == button::active ? CHECKBOX_PARTIAL_ACTIVE :
                  /*b == button::idle ?*/ CHECKBOX_PARTIAL :
                /* s == unchecked ? */
                    b == button::hover  ? CHECKBOX_HOVER :
                    b == button::active ? CHECKBOX_ACTIVE :
                  /*b == button::idle ?*/ CHECKBOX
            );
        }

    private:
        mode_t m = twostate;
        state_t s = unchecked;
    };
}
