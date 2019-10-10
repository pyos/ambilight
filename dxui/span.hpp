#pragma once

namespace util {
    template <typename T>
    // A stripped down `std::span<T>`.
    struct span {
        span() : span(nullptr, 0) {}
        span(T* data, size_t size) : data_(data), size_(size) {}
        span(std::initializer_list<T> container) : span(container.begin(), container.size()) {}
        template <typename U, size_t N> span(U (&array)[N]) : span(array, N) {}
        template <typename U> span(U& container) : span(container.data(), container.size()) {}
        template <typename U> span<U> reinterpret() { return {reinterpret_cast<U*>(data_), size_ * sizeof(T) / sizeof(U)}; }

        size_t size() const { return size_; }
        T* data() const { return data_; }
        T* begin() const { return data_; }
        T* end() const { return data_ + size_; }
        T& operator[](size_t i) const { return data_[i]; }
        explicit operator bool() const { return size_; }

    private:
        T* data_;
        size_t size_;
    };
}
