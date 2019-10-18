#pragma once

#ifndef WIN32_MEAN_AND_LEAN
#define WIN32_MEAN_AND_LEAN 1
#endif

#ifndef NOMINMAX
#define NOMINMAX 1
#endif

#include <memory>
#include <stdexcept>
#include <windows.h>

namespace winapi {
    struct error : std::runtime_error {
        template <typename T>
        error(HRESULT hr, T message) : std::runtime_error(message), hr(hr) {}

        const HRESULT hr;
    };

    template <typename T>
    static T throwOnFalse(T&& x) {
        if (!x)
            throw error(HRESULT_FROM_WIN32(GetLastError()), ""); // TODO text
        return std::forward<T>(x);
    }

    static HRESULT throwOnFalse(HRESULT x) {
        if (FAILED(x))
            throw error(x, ""); // TODO text
        return x;
    }

    struct com_release {
        void operator()(IUnknown* x) const {
            x->Release();
        }
    };

    // A pointer to a refcounted COM entity.
    template <typename T>
    struct com_ptr : std::unique_ptr<T, com_release> {
        com_ptr() = default;
        // Ambiguous whether the pointer is managed or not. D3D initializers use `operator&` anyway.
        com_ptr(T*) = delete;
        com_ptr(com_ptr&&) = default;
        com_ptr(const com_ptr& p) : std::unique_ptr<T, com_release>(p.get()) { if (p) p->AddRef(); }
        com_ptr& operator=(com_ptr&&) = default;
        com_ptr& operator=(const com_ptr& p) { return *this = com_ptr<T>(p); }

        operator T*() const {
            return std::unique_ptr<T, com_release>::get();
        }

        T** operator&() {
            return reinterpret_cast<T**>(this);
        }

        template <typename F /* = HRESULT(T**) */,
                  typename R = std::enable_if_t<std::is_same_v<std::invoke_result_t<F, T**>, HRESULT>>>
        com_ptr(F&& f) { winapi::throwOnFalse(f(reinterpret_cast<T**>(this))); }
    };
}

// Call an out-pointer returning COM method.
#define COM(R, T, f, ...) winapi::com_ptr<R>([&](R** __p) { return f(__VA_ARGS__##, reinterpret_cast<T**>(__p)); })

// Call an out-pointer returning COM method that takes an interface as a single argument,
// such as `QueryInterface`, e.g. `COMi(ISomeInterface, someObject->QueryInterface)`
#define COMi(R, f) COM(R, void, f, __uuidof(R))

// Call an out-pointer returning COM method that takes an interface as the last argument.
#define COMv(R, f, ...) COM(R, void, f, ##__VA_ARGS__, __uuidof(R))

// Call an out-pointer returning COM method that always returns a fixed interface.
#define COMe(R, f, ...) COM(R, R, f, ##__VA_ARGS__)
