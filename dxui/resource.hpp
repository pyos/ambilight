#pragma once

#include "base.hpp"
#include "span.hpp"

namespace ui {
    using resource = std::pair<HINSTANCE, const wchar_t*>;
    static resource fromFile(const wchar_t* name) { return {nullptr, name}; };
    static resource fromBundled(int id) { return {impl::hInstance, MAKEINTRESOURCE(id)}; }
    util::span<uint8_t> read(resource, const wchar_t* type);

    using icon = impl::holder<HICON, DestroyIcon>;
    icon loadIcon(resource, int w, int h);
    icon loadNormalIcon(resource);
    icon loadSmallIcon(resource);

    using cursor = impl::holder<HCURSOR, DestroyCursor>;
    cursor loadCursor(resource);
}
