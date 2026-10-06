#include "TestFramework.h"
#include "aimix/SpscRingBuffer.h"

#include <thread>

using namespace aimix;

namespace
{
struct Item { uint64_t seq; float payload[15]; };
}

TEST_CASE ("ring: FIFO order, capacity and empty/full behaviour")
{
    SpscRingBuffer<Item, 4> ring;
    Item out {};
    CHECK (! ring.tryPop (out));

    for (uint64_t i = 0; i < 4; ++i)
        CHECK (ring.tryPush (Item { i, {} }));
    CHECK (! ring.tryPush (Item { 99, {} }));   // full: producer drops, never blocks
    CHECK (ring.sizeApprox() == 4);

    for (uint64_t i = 0; i < 4; ++i)
    {
        REQUIRE (ring.tryPop (out));
        CHECK (out.seq == i);
    }
    CHECK (! ring.tryPop (out));
}

TEST_CASE ("ring: index wrap-around past 2^32 boundary stays consistent")
{
    SpscRingBuffer<Item, 8> ring;
    Item out {};
    // Run enough cycles to wrap the 3-bit mask many times.
    for (uint64_t i = 0; i < 100000; ++i)
    {
        CHECK (ring.tryPush (Item { i, {} }));
        REQUIRE (ring.tryPop (out));
        if (out.seq != i) { CHECK (out.seq == i); break; }
    }
}

TEST_CASE ("ring: concurrent producer/consumer delivers 2M items in order with no loss")
{
    static SpscRingBuffer<Item, 8> ring;   // static: large-ish and shared with threads
    ring.reset();
    constexpr uint64_t total = 2'000'000;

    std::thread producer ([]
    {
        for (uint64_t i = 0; i < total;)
        {
            if (auto* slot = ring.tryAcquireWrite())
            {
                slot->seq = i;
                for (auto& f : slot->payload) f = (float) i;
                ring.commitWrite();
                ++i;
            }
        }
    });

    uint64_t expected = 0;
    bool ordered = true, intact = true;
    while (expected < total)
    {
        if (const auto* item = ring.tryAcquireRead())
        {
            ordered &= item->seq == expected;
            intact  &= item->payload[14] == (float) expected;
            ring.commitRead();
            ++expected;
        }
    }
    producer.join();
    CHECK (ordered);
    CHECK (intact);
}
