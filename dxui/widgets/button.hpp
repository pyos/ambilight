#pragma once

#include "../window.hpp"
#include "texrect.hpp"

namespace ui {
    // A widget that handles mouse events like a button would.
    struct buttonlike : texrect {
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
                case capture_state::release: setState(idle); if (hovering) handle(); return false;
            }
            return false;
        }

        void onMouseLeave() override {
            cap.stop();
            setState(idle);
        }

        enum state { idle, hover, active };
        virtual void setState(state) = 0;
        virtual void handle() = 0;

    private:
        capture_state cap;
    };

    // A rectangular push button:
    //
    //     +-------------------------------+
    //     | Why did I draw a button here? |
    //     +-------------------------------+
    //
    struct button : buttonlike {
        using buttonlike::buttonlike;

    public:
        // Emitted when the left mouse button is released over this widget.
        util::event<> onClick;

    protected:
        void setState(state s) override {
            if (currentState != s)
                currentState = s, invalidate();
        }

        void handle() override {
            onClick();
        }

        RECT getOuter() const override {
            switch (currentState) {
            default:     return builtinRect(BUTTON_OUTER);
            case hover:  return builtinRect(BUTTON_OUTER_HOVER);
            case active: return builtinRect(BUTTON_OUTER_ACTIVE);
            }
        }

        RECT getInner() const override {
            switch (currentState) {
            default:     return builtinRect(BUTTON_INNER);
            case hover:  return builtinRect(BUTTON_INNER_HOVER);
            case active: return builtinRect(BUTTON_INNER_ACTIVE);
            }
        }

    protected:
        state currentState = idle;
    };

    struct borderless_button : button {
        using button::button;

    protected:
        RECT getOuter() const override {
            switch (currentState) {
            default:     return builtinRect(BUTTON_BORDERLESS_OUTER);
            case hover:  return builtinRect(BUTTON_BORDERLESS_OUTER_HOVER);
            case active: return builtinRect(BUTTON_BORDERLESS_OUTER_ACTIVE);
            }
        }

        RECT getInner() const override {
            switch (currentState) {
            default:     return builtinRect(BUTTON_BORDERLESS_INNER);
            case hover:  return builtinRect(BUTTON_BORDERLESS_INNER_HOVER);
            case active: return builtinRect(BUTTON_BORDERLESS_INNER_ACTIVE);
            }
        }
    };

    struct button_container : buttonlike {
        using buttonlike::buttonlike;

        void setTarget(buttonlike* newTarget) {
            target = newTarget;
        }

    protected:
        void setState(state n) override { if (target) target->setState(n); }
        void handle() override { if (target) target->handle(); }
        RECT getOuter() const override { return builtinRect(BUTTON_BORDERLESS_OUTER); }
        RECT getInner() const override { return getOuter(); }

    private:
        buttonlike* target = nullptr;
    };
}
