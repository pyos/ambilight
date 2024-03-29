#pragma once

#include "base.hpp"
#include "span.hpp"
#include "../resource.h"

namespace ui {
    using resource = std::pair<HINSTANCE, const wchar_t*>;
    static resource fromFile(const wchar_t* name) { return {nullptr, name}; };
    static resource fromBundled(int id) { return {hInstance, MAKEINTRESOURCE(id)}; }
    static resource fromBundled(const wchar_t* name) { return {hInstance, name}; }
    util::span<const uint8_t> read(resource, const wchar_t* type);

    using icon = winapi::holder<HICON, DestroyIcon>;
    icon loadIcon(resource, int w, int h);
    icon loadNormalIcon(resource);
    icon loadSmallIcon(resource);

    using cursor = winapi::holder<HCURSOR, DestroyCursor>;
    cursor loadCursor(resource);
}
