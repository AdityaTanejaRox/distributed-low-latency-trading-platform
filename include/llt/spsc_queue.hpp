#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace llt {

    template <typename T, std::size_t Capacity>
    class SpscQueue 
    {
        public:
            static_assert(Capacity >= 2);

            bool push(const T& item) 
            {
                const auto head = head_.load(std::memory_order_relaxed);
                const auto next = increment(head);

                if (next == tail_.load(std::memory_order_acquire)) 
                {
                    dropped_.fetch_add(1, std::memory_order_relaxed);
                    return false;   // full
                }

                buffer_[head] = item;
                head_.store(next, std::memory_order_release);
                return true;
            }

            std::optional<T> pop() 
            {
                const auto tail = tail_.load(std::memory_order_relaxed);

                if (tail == head_.load(std::memory_order_acquire)) 
                {
                    return std::nullopt;    // empty
                }

                T item = buffer_[tail];
                tail_.store(increment(tail), std::memory_order_release);
                return item;
            }

            bool empty() const 
            {
                return head_.load(std::memory_order_acquire) ==
                    tail_.load(std::memory_order_acquire);
            }

            std::size_t dropped() const 
            {
                return dropped_.load(std::memory_order_relaxed);
            }

        private:
            static constexpr std::size_t increment(std::size_t value) 
            {
                return (value + 1) % Capacity;
            }

            alignas(64) std::array<T, Capacity> buffer_{};
            alignas(64) std::atomic<std::size_t> head_{0};
            alignas(64) std::atomic<std::size_t> tail_{0};
            alignas(64) std::atomic<std::size_t> dropped_{0};
    };

}