#pragma once

#include "../window.hpp"
#include "texrect.hpp"

namespace ui {
    // A rectangular push button:
    //
    //     +-------------------------------+
    //     | Why did I draw a button here? |
    //     +-------------------------------+
    //
    struct button : texrect {
        using texrect::texrect;

        bool onMouse(POINT abs, int keys) override {
            bool hovering = rectHit(size(), relative(abs));
            switch (cap.update(keys)) {
                case capture_state::ignore:  return false;
                case capture_state::prepare: setState(hover); return false;
                case capture_state::capture: setState(active); if (auto w = parentWindow()) w->captureMouse(*this); return true;
                case capture_state::drag:    setState(hovering ? active : idle); return true;
                // Unconditionally switching to `idle` even when hovering over the button
                // gives a little feedback that the click was actually registered.
                case capture_state::release: setState(idle); if (hovering) onClick(); return false;
            }
            return false;
        }

        void onMouseLeave() override {
            cap.stop();
            setState(idle);
        }

        void setBorderless(bool value = true) {
            borderless = value;
            invalidate();
        }

    public:
        // Emitted when the left mouse button is released over this widget.
        util::event<> onClick;

    protected:
        enum state { idle, hover, active };
        state getState() const { return currentState; }

        RECT getOuter() const override {
            switch (getState()) {
            default:     return builtinRect(borderless ? BUTTON_BORDERLESS_OUTER : BUTTON_OUTER);
            case hover:  return builtinRect(borderless ? BUTTON_BORDERLESS_OUTER_HOVER : BUTTON_OUTER_HOVER);
            case active: return builtinRect(borderless ? BUTTON_BORDERLESS_OUTER_ACTIVE : BUTTON_OUTER_ACTIVE);
            }
        }

        RECT getInner() const override {
            switch (getState()) {
            default:     return builtinRect(borderless ? BUTTON_BORDERLESS_INNER : BUTTON_INNER);
            case hover:  return builtinRect(borderless ? BUTTON_BORDERLESS_INNER_HOVER : BUTTON_INNER_HOVER);
            case active: return builtinRect(borderless ? BUTTON_BORDERLESS_INNER_ACTIVE : BUTTON_INNER_ACTIVE);
            }
        }

    private:
        void setState(state s) {
            if (currentState != s)
                currentState = s, invalidate();
        }

    private:
        state currentState = idle;
        capture_state cap;
        bool borderless = false;
    };
}
