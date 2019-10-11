#pragma once

#include "../widget.hpp"
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
                case capture_state::capture: setState(active); captureMouse(); return true;
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

    public:
        // Emitted when the left mouse button is released over this widget.
        util::event<> onClick;

    protected:
        enum state { idle, hover, active };
        state getState() const { return currentState; }
        winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext&) const override;
        RECT getOuter() const override;
        RECT getInner() const override;

    private:
        void setState(state s) {
            if (currentState != s)
                currentState = s, invalidate();
        }

    private:
        state currentState = idle;
        capture_state cap;
    };
}
