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

        template <int resourceId>
        static const font& loadPermanently() {
            static const font f{resourceId};
            return f;
        }

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
        std::reference_wrapper<const font> font;
        uint32_t fontSize  = 18;
        uint32_t fontColor = 0xFFFFFFFFu;
        bool breakAfter = false;
        // TODO line alignment
    };

    struct label : widget {
        label(std::vector<text_part> data = {})
            : data(std::move(data))
            , hideOverflow(false)
            , lineHeight(1.3)
        {}

        template <typename Container>
        void setText(Container&& updated) {
            data.assign(std::begin(updated), std::end(updated));
            invalidateSize();
        }

        void setText(std::initializer_list<text_part> updated) {
            data.assign(std::begin(updated), std::end(updated));
            invalidateSize();
        }

        void setText(std::vector<text_part>&& updated) {
            data = std::move(updated);
            invalidateSize();
        }

        // Allow sizing this label smaller than the space required to display all lines
        // without cutting them off. (In that case, some of the text will be invisible.)
        //
        // TODO insert an ellipsis instead
        void setHideOverflow(bool value) {
            hideOverflow = value;
            invalidateSize();
        }

        void setLineHeight(double lh) {
            lineHeight = lh;
            invalidateSize();
        }

    private:
        POINT measureMinImpl() const override;
        POINT measureImpl(POINT) const override;
        void drawImpl(ui::dxcontext& ctx, ID3D11Texture2D* target, RECT total, RECT dirty) const override;

    private:
        std::vector<text_part> data;
        bool hideOverflow;
        double lineHeight;
        mutable double originX;
    };
}
