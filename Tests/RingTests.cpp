// Reverie/Tests/RingTests.cpp - the lock-free SPSC ring primitive (Core/SpscRing.h).
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Verifies the substrate of the real-time control<->audio boundary: single-threaded FIFO semantics
// and full/empty behavior, move-only payloads (shared_ptr) round-tripping with no leak, and a real
// two-thread producer/consumer stress that asserts an exact, in-order, no-loss transfer. Headless
// and deterministic (the stress test asserts a total and a monotonic sequence, not timing).
#include "Core/SpscRing.h"

#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>

using namespace reverie;

static int g_failures = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

// --- 1. Single-threaded FIFO order + full/empty --------------------------------------------------
static void TestBasicFifo() {
    SpscRing<int> ring(4); // rounds to 4
    Check(ring.Capacity() == 4, "capacity rounds up to power of two");
    Check(ring.EmptyApprox(), "starts empty");

    int out = -1;
    Check(!ring.TryPop(out), "pop on empty returns false");

    Check(ring.TryPush(10), "push 1");
    Check(ring.TryPush(20), "push 2");
    Check(ring.TryPush(30), "push 3");
    Check(ring.TryPush(40), "push 4 (fills)");
    Check(!ring.TryPush(50), "push on full returns false");
    Check(ring.SizeApprox() == 4, "size reports 4 when full");

    Check(ring.TryPop(out) && out == 10, "pop FIFO 10");
    Check(ring.TryPop(out) && out == 20, "pop FIFO 20");
    Check(ring.TryPush(50), "push after a pop freed a slot");
    Check(ring.TryPop(out) && out == 30, "pop FIFO 30");
    Check(ring.TryPop(out) && out == 40, "pop FIFO 40");
    Check(ring.TryPop(out) && out == 50, "pop FIFO 50 (the re-pushed one)");
    Check(!ring.TryPop(out), "empty again");
}

// --- 2. Wraparound over many cycles (index masking) ----------------------------------------------
static void TestWraparound() {
    SpscRing<int> ring(2); // capacity 2, forces constant wrap
    int out = -1;
    bool ok = true;
    for (int i = 0; i < 10000; ++i) {
        ok = ok && ring.TryPush(i);
        ok = ok && ring.TryPop(out) && out == i;
    }
    Check(ok, "10k push/pop cycles preserve value through index wrap");
    Check(ring.EmptyApprox(), "empty after balanced cycles");
}

// --- 3. Move-only payload (shared_ptr) round-trips with no leak -----------------------------------
// A Reverie command that carries a std::shared_ptr<const AudioBuffer> is move-only and must not
// leak or double-free as it passes through the ring. We measure resource ownership by use_count()
// of a single shared object (the authoritative measure across move/copy/assign), NOT by ctor/dtor
// counting (which move-assignment deliberately bypasses).
static void TestMoveOnlyNoLeak() {
    auto token = std::make_shared<int>(0); // use_count starts at 1 (this local reference)
    struct Payload {
        std::shared_ptr<int> ref; // a command-carried resource handle
        Payload() = default;
        explicit Payload(std::shared_ptr<int> r) : ref(std::move(r)) {}
        Payload(Payload&&) = default;
        Payload& operator=(Payload&&) = default;
        Payload(const Payload&) = delete;
        Payload& operator=(const Payload&) = delete;
    };

    {
        SpscRing<Payload> ring(8);
        for (int i = 0; i < 5; ++i) Check(ring.TryPush(Payload{token}), "push move-only payload");
        Check(token.use_count() == 1 + 5, "5 payload references held inside the ring");

        Payload got;
        for (int i = 0; i < 5; ++i) Check(ring.TryPop(got), "pop move-only payload");
        // Each pop moves a ref into `got` (releasing got's previous) and clears the ring slot,
        // so exactly one reference survives (in `got`) plus our local `token`.
        Check(token.use_count() == 1 + 1, "ring drained: only the last popped reference survives");
    }
    Check(token.use_count() == 1, "all references released after ring + local destroyed (no leak/no double-free)");
}

// --- 4. Two-thread producer/consumer stress: exact, in-order, no-loss ----------------------------
static void TestThreadedStress() {
    constexpr u64 kCount = 2'000'000;
    SpscRing<u64> ring(1024);

    std::atomic<bool> producerStarted{false};
    std::thread producer([&] {
        producerStarted.store(true, std::memory_order_release);
        for (u64 i = 0; i < kCount;) {
            if (ring.TryPush(u64{i}))
                ++i;
            else
                std::this_thread::yield(); // full: back off, never block the ring
        }
    });

    u64 received = 0;
    u64 expected = 0;
    bool ordered = true;
    while (received < kCount) {
        u64 v;
        if (ring.TryPop(v)) {
            if (v != expected) ordered = false;
            ++expected;
            ++received;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();

    Check(producerStarted.load(std::memory_order_acquire), "producer ran");
    Check(received == kCount, "consumer received every item exactly once");
    Check(ordered, "items arrived in strict FIFO order with no loss/duplication");
    Check(ring.EmptyApprox(), "ring drained empty at the end");
}

int main() {
    std::printf("reverie ring tests\n");
    TestBasicFifo();
    TestWraparound();
    TestMoveOnlyNoLeak();
    TestThreadedStress();
    if (g_failures == 0) {
        std::printf("reverie ring tests: PASS\n");
        return 0;
    }
    std::printf("reverie ring tests: FAIL (%d)\n", g_failures);
    return 1;
}
