// Reverie/Runtime/Core/SpscRing.h - a bounded, wait-free single-producer/single-consumer ring.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// This is the substrate of Reverie's real-time control<->audio boundary (see
// Docs/ArchitectureAssessment.md, Phase 1). A professional audio middleware must NEVER let the
// audio thread block on a lock the control thread holds, allocate on the audio thread, or free on
// the audio thread. An SPSC ring solves the first two: the control thread PUSHes POD commands and
// the audio thread POPs them at the top of its callback, wait-free, with zero allocation after
// construction. The reverse direction (audio -> control) carries finished objects to a
// "retire" consumer so their destructors/frees run OFF the audio thread.
//
// Contract:
//   * EXACTLY ONE producer thread calls TryPush; EXACTLY ONE consumer thread calls TryPop.
//     (Multiple producers or multiple consumers are UB - this is not an MPMC queue.)
//   * Capacity is fixed at construction and rounded UP to a power of two. All storage is allocated
//     then; TryPush/TryPop never allocate.
//   * T may be move-only (e.g. it may hold a std::shared_ptr). Slots are real T objects: TryPush
//     move-assigns into a slot, TryPop moves out. Neither operation allocates for shared_ptr.
//   * TryPush returns false when full; TryPop returns false when empty. Neither ever blocks.
//
// Memory ordering: the classic Lamport SPSC. The producer publishes `tail_` with release after
// writing a slot; the consumer acquires `tail_` before reading it. The consumer publishes `head_`
// with release after consuming a slot; the producer acquires `head_` before deciding a slot is
// free to overwrite. Counters are monotonic u64 (never wrap in any realistic runtime); the slot
// index is `counter & mask_`.
#pragma once

#include "Core/Types.h"

#include <atomic>
#include <vector>

namespace reverie {

template <typename T>
class SpscRing {
public:
    // `capacity` is the max number of in-flight items; it is rounded up to a power of two (min 2).
    explicit SpscRing(usize capacity) {
        usize cap = 2;
        while (cap < capacity) cap <<= 1;
        slots_.resize(cap); // the ONLY allocation; default-constructs `cap` T objects
        mask_ = cap - 1;
    }

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    // Producer side. Moves `item` into the ring. Returns false (dropping nothing) when full.
    bool TryPush(T&& item) {
        const u64 tail = tail_.load(std::memory_order_relaxed);
        if (tail - head_.load(std::memory_order_acquire) >= Capacity()) return false; // full
        slots_[tail & mask_] = std::move(item);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Convenience copy overload for trivially/cheaply copyable T (commands).
    bool TryPush(const T& item) {
        T tmp = item;
        return TryPush(std::move(tmp));
    }

    // Consumer side. Moves the next item into `out`. Returns false when empty.
    bool TryPop(T& out) {
        const u64 head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return false; // empty
        out = std::move(slots_[head & mask_]);
        slots_[head & mask_] = T{}; // release any resources the moved-from slot may still pin
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Approximate count (safe to call from either side; exact only when quiescent).
    usize SizeApprox() const {
        return static_cast<usize>(tail_.load(std::memory_order_acquire) -
                                  head_.load(std::memory_order_acquire));
    }

    usize Capacity() const { return mask_ + 1; }
    bool EmptyApprox() const { return SizeApprox() == 0; }

private:
    std::vector<T> slots_;
    usize mask_ = 0;
    // Cache-line-padded to keep the producer's tail_ and the consumer's head_ off the same line.
    alignas(64) std::atomic<u64> head_{0}; // next slot to read  (consumer owns)
    alignas(64) std::atomic<u64> tail_{0}; // next slot to write (producer owns)
};

} // namespace reverie
