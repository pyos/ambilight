#pragma once

#include <atomic>
#include <thread>

namespace util {
    // A thread that can be terminated when at certain points.
    struct thread {
        template <typename F>
        thread(F&& f) : t{[this, f = std::forward<F>(f)] { f(terminate); }} {}
        thread(const thread&) = delete;
        thread& operator=(const thread&) = delete;
        ~thread() { terminate = true; t.join(); }

    private:
        std::thread t;
        std::atomic<bool> terminate{false};
    };
}
