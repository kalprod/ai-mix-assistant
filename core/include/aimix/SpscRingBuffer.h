#pragma once

// Wait-free single-producer / single-consumer ring buffer.
//
// * Fixed capacity (power of two), storage inline: no pointers, so the whole
//   object can live in a shared-memory segment mapped at different addresses.
// * Producer = one plugin instance's audio thread, consumer = Master Engine thread.
// * Zero-copy API: the producer writes straight into the slot it acquires,
//   which avoids a second 10 KB copy on the audio thread.
// * Never blocks, never allocates. When full, the producer drops the frame.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace aimix
{
template <typename T, uint32_t Capacity>
class SpscRingBuffer
{
    static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");
    static_assert (std::is_trivially_copyable_v<T>, "items are copied with memcpy semantics");
    static_assert (std::atomic<uint32_t>::is_always_lock_free, "indices must be lock-free (and address-free) for shared memory");

public:
    static constexpr uint32_t capacity = Capacity;

    // Only call while neither side is active (e.g. while claiming a bus slot).
    void reset() noexcept
    {
        head.store (0, std::memory_order_relaxed);
        tail.store (0, std::memory_order_relaxed);
    }

    // ---- producer side --------------------------------------------------------
    T* tryAcquireWrite() noexcept
    {
        const auto t = tail.load (std::memory_order_relaxed);
        const auto h = head.load (std::memory_order_acquire);
        if (t - h >= Capacity)
            return nullptr;
        return &items[t & (Capacity - 1)];
    }

    void commitWrite() noexcept
    {
        tail.store (tail.load (std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    bool tryPush (const T& value) noexcept
    {
        if (auto* slot = tryAcquireWrite())
        {
            std::memcpy (static_cast<void*> (slot), &value, sizeof (T));
            commitWrite();
            return true;
        }
        return false;
    }

    // ---- consumer side --------------------------------------------------------
    const T* tryAcquireRead() noexcept
    {
        const auto h = head.load (std::memory_order_relaxed);
        const auto t = tail.load (std::memory_order_acquire);
        if (h == t)
            return nullptr;
        return &items[h & (Capacity - 1)];
    }

    void commitRead() noexcept
    {
        head.store (head.load (std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    bool tryPop (T& out) noexcept
    {
        if (const auto* slot = tryAcquireRead())
        {
            std::memcpy (static_cast<void*> (&out), slot, sizeof (T));
            commitRead();
            return true;
        }
        return false;
    }

    uint32_t sizeApprox() const noexcept
    {
        return tail.load (std::memory_order_acquire) - head.load (std::memory_order_acquire);
    }

private:
    // Head and tail on separate cache lines to avoid false sharing between the
    // audio thread and the engine thread.
    alignas (64) std::atomic<uint32_t> head { 0 };   // next item to read  (consumer-owned)
    alignas (64) std::atomic<uint32_t> tail { 0 };   // next item to write (producer-owned)
    alignas (64) T items[Capacity];
};

} // namespace aimix
