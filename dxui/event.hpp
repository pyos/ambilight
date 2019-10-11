#pragma once

#include <functional>
#include <list>

namespace util {
    template <typename... ArgTypes>
    struct event {
        event() = default;
        event(const event&) = delete;
        event& operator=(const event&) = delete;

        template <typename... Args>
        bool operator()(Args&&... args) {
            for (auto& f : callbacks)
                if (!f(args...))
                    return false;
            return true;
        }

        struct unsubscribe {
            void operator()(event* ev) {
                ev->callbacks.erase(it);
            }

            typename std::list<std::function<bool(ArgTypes...)>>::iterator it;
        };

        using handle = std::unique_ptr<event, unsubscribe>;

        template <typename F /* = bool(ArgTypes...) or void(ArgTypes...) */>
        handle add(F&& cb) {
            if constexpr (std::is_same_v<std::invoke_result_t<F, ArgTypes...>, void>)
                callbacks.emplace_back([cb = std::forward<F>(cb)](ArgTypes... args) { cb(args...); return true; });
            else
                callbacks.emplace_back(std::forward<F>(cb));
            auto it = callbacks.end();
            return {this, unsubscribe{--it}};
        }

    private:
        std::list<std::function<bool(ArgTypes...)>> callbacks;
    };
}
