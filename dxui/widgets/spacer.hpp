#pragma once

#include "../widget.hpp"

namespace ui {
    // Just an empty space with a fixed size.
    struct spacer : widget {
        spacer(POINT size) : size_(size) { }

        POINT size() { return size_; }

        void setSize(POINT newSize) {
            size_ = newSize;
            invalidateSize();
        }

    private:
        POINT measureMinEx() const override { return size_; }
        POINT size_;
    };
}
