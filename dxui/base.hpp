#pragma once

#include <memory>

#include "winapi.hpp"

namespace ui {
    namespace impl {
        extern HINSTANCE hInstance;

        template <typename T, void (*F)(T)>
        struct deleter {
            void operator()(T x) {
                F(x);
            }
        };

        template <typename T, BOOL (*F)(T)>
        void adapt_stdcall(T x) {
            F(x);
        }

        template <typename T, void (*F)(T)>
        using holder_ex = std::unique_ptr<std::remove_pointer_t<T>, deleter<T, F>>;

        template <typename T, BOOL (*F)(T)>
        using holder = holder_ex<T, adapt_stdcall<T, F>>;
    }

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
