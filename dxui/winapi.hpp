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
        if (!x) {
            // TODO GetLastError() and such
            throw std::exception();
        }
        return std::forward<T>(x);
    }

    static HRESULT throwOnFalse(HRESULT x) {
        if (FAILED(x)) {
            // TODO
            throw std::exception();
        }
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
    };

    namespace impl {
        template <typename R, typename F /* = HRESULT(R**) */>
        com_ptr<R> com_call(F&& f) {
            com_ptr<R> ret;
            winapi::throwOnFalse((HRESULT)f(&ret));
            return ret;
        }
    }
}

// Call an out-pointer returning COM method.
#define COM(R, T, f, ...) winapi::impl::com_call<R>([&](R** __p) { return f(__VA_ARGS__##, reinterpret_cast<T**>(__p)); })

// Call an out-pointer returning COM method that takes an interface as a single argument,
// such as `QueryInterface`, e.g. `COMi(ISomeInterface, someObject->QueryInterface)`
#define COMi(R, f) COM(R, void, f, __uuidof(R))

// Call an out-pointer returning COM method that takes an interface as the last argument.
#define COMv(R, f, ...) COM(R, void, f, ##__VA_ARGS__, __uuidof(R))

// Call an out-pointer returning COM method that always returns a fixed interface.
#define COMe(R, f, ...) COM(R, R, f, ##__VA_ARGS__)
