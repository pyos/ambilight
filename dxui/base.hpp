#pragma once

#include <memory>

#include "winapi.hpp"

namespace ui {
    namespace impl {
        extern HINSTANCE hInstance;

        template <typename T, BOOL (*F)(T)>
        struct deleter {
            void operator()(T x) {
                F(x);
            }
        };

        template <typename T, BOOL (*F)(T)>
        using holder = std::unique_ptr<std::remove_pointer_t<T>, deleter<T, F>>;
    }

    using handle = impl::holder<HANDLE, CloseHandle>;

    // Main entry point. Implement this in a unit somewhere.
    int main();

    // Dispatch all incoming messages until an exit is requested.
    int dispatch();

    // Initialize COM. Should be called from every thread (except the main one,
    // for which is it called by default.)
    void initCOM();

    // Send a quit message to the main thread.
    void quit(int statusCode = 0);
}
