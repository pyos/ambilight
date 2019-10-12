#pragma once

namespace util {
    struct defer {
        template <typename F>
        struct impl {
            impl(F&& f) : f(std::move(f)) {}
            impl(const impl&) = delete;
            impl& operator=(const impl&) = delete;
            ~impl() { f(); }
            F f;
        };

        template <typename F>
        impl<F> operator|(F&& f) && { return std::move(f); }
    };
}

#define CAT(a, b) CAT2(a, b)
#define CAT2(a, b) CAT3(a ## b)
#define CAT3(x) x

// Run a block on scope exit:
//     {
//         doInitialization();
//         DEFER { doCleanup(); };
//         doActualWork();
//     } // doCleanup called here or if doActualWork throws an exception
#define DEFER const auto& CAT(__defer_, __LINE__) = util::defer{} | [&]()
