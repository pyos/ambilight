#include "resource.hpp"

util::span<const uint8_t> ui::read(resource rs, const wchar_t* type) {
    auto resInfo = FindResource(rs.first, rs.second, type);
    if (!resInfo)
        return {};
    auto size = SizeofResource(rs.first, resInfo);
    auto data = reinterpret_cast<uint8_t*>(LockResource(LoadResource(rs.first, resInfo)));
    return {data, size};
}

ui::icon ui::loadIcon(resource rs, int w, int h) {
    return icon{(HICON)LoadImage(rs.first, rs.second, IMAGE_ICON, w, h, 0)};
}

ui::icon ui::loadNormalIcon(resource rs) {
    static const auto w = GetSystemMetrics(SM_CXICON);
    static const auto h = GetSystemMetrics(SM_CYICON);
    return loadIcon(rs, w, h);
}

ui::icon ui::loadSmallIcon(resource rs) {
    static const auto w = GetSystemMetrics(SM_CXSMICON);
    static const auto h = GetSystemMetrics(SM_CYSMICON);
    return loadIcon(rs, w, h);
}

ui::cursor ui::loadCursor(resource rs) {
    return cursor{LoadCursor(rs.first, rs.second)};
}
