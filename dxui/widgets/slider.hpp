#pragma once

#include "../widget.hpp"

namespace ui {
    // Also known as a "trackbar", this is just a knob on a rail:
    //
    //     ----||--------------
    //
    // Values range from 0 to 1 inclusive.
    //
    struct slider : widget {
        enum orientation { deg0, deg90, deg180, deg270 };

        slider(double value = 0., orientation o = deg0)
            : value_(value)
            , currentOrientation(o)
        {}

        // Change the orientation of the slider, in degrees clockwise.
        //
        // NOTE: 0 is always top or left, and 1 is always bottom or right.
        //       The difference between deg0 <-> deg180 and deg90 <-> deg270
        //       is the orientation of textures, which may or may not be symmetric
        //       depending on the IDI_WIDGETS resource.
        //
        void setOrientation(orientation o) {
            currentOrientation = o;
            invalidateSize();
        }

        // Get the current value in a range from 0 to 1.
        double value() const {
            return value_;
        }

        // Set the current value, clamping if out of bounds. Does not emit the event.
        void setValue(double v) {
            value_ = std::min(std::max(v, 0.0), 1.0);
            invalidate();
        }

        // Set the current value, first remapping its interval to [0, 1].
        template <typename T>
        void setValue(T v, T min, T max, T step = 1) {
            setValue((double)(v - v % step - min) / (max - min));
        }

        // Map the current value from [0, 1] to an integer range [min, max].
        template <typename T>
        T mapValue(T min, T max, T step = 1) {
            T r = (T)(value_ * (max - min) + min + step / 2. /* round to closest */);
            return r - r % step;
        }

        bool onMouse(POINT abs, int keys) override {
            bool keepCapturing = true;
            switch (cap.update(keys)) {
                case capture_state::ignore:
                case capture_state::prepare: return false;
                case capture_state::capture: captureMouse(); break;
                case capture_state::release: keepCapturing = false; break;
            }
            auto [w, h] = measureMin();
            setValue(vertical() ? (relative(abs).y - h / 2.) / (size().y - h)
                                : (relative(abs).x - w / 2.) / (size().x - w));
            onChange(value_);
            return keepCapturing;
        }

        void onMouseLeave() override {
            cap.stop();
        }

    public:
        // Emitted when the current value changes.
        util::event<double> onChange;

    protected:
        virtual winapi::com_ptr<ID3D11Texture2D> getTexture(ui::dxcontext&) const;
        virtual RECT getTrack() const;
        virtual RECT getGroove() const;

    private:
        bool vertical() const { return currentOrientation == deg90  || currentOrientation == deg270; }
        bool inverted() const { return currentOrientation == deg180 || currentOrientation == deg270; }
        POINT measureMinImpl() const override;
        POINT measureImpl(POINT fit) const override;
        void drawImpl(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const override;

    private:
        double value_;
        orientation currentOrientation;
        capture_state cap;
    };
}
