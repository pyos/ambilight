#pragma once

#include "../resource.hpp"
#include "../widget.hpp"

namespace ui {
    struct font_symbol {
        uint16_t x;
        uint16_t y;
        uint16_t w;
        uint16_t h;
        uint16_t originX;
        uint16_t originY;
        uint16_t advance;
    };

    struct font {
        font(ui::resource texture, ui::resource charmap)
            : texture(ui::read(texture, L"PNG"))
            , charmap(ui::read(charmap, L"TEXT").reinterpret<const char>())
        {}

        font(int resourceId)
            : font(ui::fromBundled(resourceId),
                   ui::fromBundled(resourceId + 1))
        {}

        winapi::com_ptr<ID3D11Texture2D> loadTexture(ui::dxcontext& ctx) const {
            return ctx.textureFromPNG(texture);
        }

        // Return the native font size, i.e. the size of symbols in texture data.
        // The text looks best at that size, although there is distance field
        // based antialiasing.
        int nativeSize() const;

        // Retrieve info for a particular code point.
        const font_symbol& operator[](wchar_t) const;

    private:
        util::span<const uint8_t> texture;
        util::span<const char> charmap;
        mutable std::vector<font_symbol> ascii;
        mutable int nativeSize_ = -1;
    };

    struct text_part {
        util::span<const wchar_t> data;
        const font& font;
        uint32_t fontSize  = 18;
        uint32_t fontColor = 0xFFFFFFFFu;
        bool breakAfter = false;
        // TODO line alignment
    };

    struct label : widget {
        label(std::vector<text_part> data)
            : data(std::move(data))
            , lineHeight(1.3)
        {}

        void setText(std::vector<text_part> updated) {
            data = std::move(updated);
            invalidateSize();
        }

        void setLineHeight(double lh) {
            lineHeight = lh;
            invalidateSize();
        }

    private:
        POINT measureMinEx() const override;
        POINT measureEx(POINT) const override { return measureMin(); }
        void drawEx(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const override;

    private:
        std::vector<text_part> data;
        double lineHeight;
        mutable double originX;
    };
}