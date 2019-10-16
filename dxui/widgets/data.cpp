#include "data.hpp"

winapi::com_ptr<ID3D11Texture2D> ui::builtinTexture(ui::dxcontext& ctx) {
    auto resource = ui::read(ui::fromBundled(IDI_WIDGETS), L"PNG");
    if (!resource)
        throw std::runtime_error("IDI_WIDGETS PNG-type resource could not be loaded");
    return ctx.textureFromPNG(resource, /*mipmaps=*/true);
}

RECT ui::builtinRect(builtin_rect r) {
    static RECT rects[BUILTIN_RECT_COUNT];
    static int initialized = [&]{
        auto resource = ui::read(ui::fromBundled(IDD_WIDGETS_RECTS), L"TEXT");
        if (!resource)
            throw std::runtime_error("IDD_WIDGETS_RECTS TEXT-type resource could nont be loaded");
        std::unordered_map<std::string_view, RECT> map;
        // x, y, width, height, name with optional whitespace
        readPseudoCSV<5>(resource.reinterpret<const char>(), [&](size_t, util::span<const char> columns[5]) {
            POINT p = {atol(columns[0].data()), atol(columns[1].data())};
            POINT q = {atol(columns[2].data()), atol(columns[3].data())};
            std::string_view name{columns[4].data(), columns[4].size()};
            map.emplace(name, RECT{p.x, p.y, p.x + q.x, p.y + q.y});
        });
        rects[BUTTON_OUTER]                   = map.at("button outer");
        rects[BUTTON_OUTER_HOVER]             = map.at("button outer hover");
        rects[BUTTON_OUTER_ACTIVE]            = map.at("button outer active");
        rects[BUTTON_INNER]                   = map.at("button inner");
        rects[BUTTON_INNER_HOVER]             = map.at("button inner hover");
        rects[BUTTON_INNER_ACTIVE]            = map.at("button inner active");
        rects[BUTTON_BORDERLESS_OUTER]        = map.at("button borderless outer");
        rects[BUTTON_BORDERLESS_OUTER_HOVER]  = map.at("button borderless outer hover");
        rects[BUTTON_BORDERLESS_OUTER_ACTIVE] = map.at("button borderless outer active");
        rects[BUTTON_BORDERLESS_INNER]        = map.at("button borderless inner");
        rects[BUTTON_BORDERLESS_INNER_HOVER]  = map.at("button borderless inner hover");
        rects[BUTTON_BORDERLESS_INNER_ACTIVE] = map.at("button borderless inner active");
        rects[SLIDER_TRACK]                   = map.at("slider track");
        rects[SLIDER_GROOVE]                  = map.at("slider groove");
        rects[WIN_CLOSE_OUTER]                = map.at("window close button outer");
        rects[WIN_CLOSE_OUTER_HOVER]          = map.at("window close button outer hover");
        rects[WIN_CLOSE_OUTER_ACTIVE]         = map.at("window close button outer active");
        rects[WIN_CLOSE_INNER]                = map.at("window close button inner");
        rects[WIN_CLOSE_INNER_HOVER]          = map.at("window close button inner hover");
        rects[WIN_CLOSE_INNER_ACTIVE]         = map.at("window close button inner active");
        rects[WIN_FRAME_OUTER]                = map.at("window frame button outer");
        rects[WIN_FRAME_OUTER_HOVER]          = map.at("window frame button outer hover");
        rects[WIN_FRAME_OUTER_ACTIVE]         = map.at("window frame button outer active");
        rects[WIN_FRAME_INNER]                = map.at("window frame button inner");
        rects[WIN_FRAME_INNER_HOVER]          = map.at("window frame button inner hover");
        rects[WIN_FRAME_INNER_ACTIVE]         = map.at("window frame button inner active");
        rects[WIN_ICON_CLOSE]                 = map.at("window icon close");
        rects[WIN_ICON_MAXIMIZE]              = map.at("window icon maximize");
        rects[WIN_ICON_UNMAXIMIZE]            = map.at("window icon unmaximize");
        rects[WIN_ICON_MINIMIZE]              = map.at("window icon minimize");
        return 0;
    }();
    return rects[r];
};
