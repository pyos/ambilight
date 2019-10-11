#pragma once

#include "../widget.hpp"

namespace ui {
    // A grid layout with stretch factors.
    //
    // Columns with a stretch factor 0 are given exactly as much horizontal space
    // as the item with the largest minimum size requires. After this is done,
    // all remaining space is allocated to the other columns proportionally to their
    // stretch factors. If some columns do not receive enough space to satisfy their
    // minimums through this process, they behave as if they are zero-stretch.
    // The same process applies to rows. Then, if any stretchy cell is bigger than
    // its contents, alignment rules are applied.
    //
    struct grid : widget {
        grid(size_t cols /* > 0 */, size_t rows /* > 0 */)
            : cells(cols * rows)
            , align(cols * rows)
            , cols(cols)
            , rows(rows)
        {
        }

        enum alignment { align_start, align_center, align_end };

        void set(size_t x /* < cols */, size_t y /* < rows */, widget* child,
                 alignment h = align_center, alignment v = align_center) {
            cells[x * rows.size() + y] = widget_handle{child, *this};
            align[x * rows.size() + y] = {h, v};
            invalidateSize();
        }

        void setColStretch(size_t x /* < cols */, uint32_t w) {
            cols[x].weight = w;
            invalidateSize();
        }

        void setRowStretch(size_t y /* < rows */, uint32_t w) {
            rows[y].weight = w;
            invalidateSize();
        }

    private:
        // TODO don't invalidate size if a stretchable widget is resized within the allocated bounds
        struct group {
            mutable LONG start;
            mutable LONG size;
            mutable LONG min;
            mutable uint32_t stretch;
            uint32_t weight = 0;
        };

        RECT itemRect(size_t x, size_t y, size_t i, POINT origin = {0, 0}) const;
        POINT measureMinEx() const override;
        POINT measureEx(POINT fit) const override;
        void drawEx(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const override;
        bool onMouse(POINT p, int keys) override;
        void onMouseLeave() override;

    private:
        std::vector<widget_handle> cells;
        std::vector<std::pair<alignment, alignment>> align;
        std::vector<group> cols;
        std::vector<group> rows;
        widget* lastMouseEvent = nullptr;
    };
}
