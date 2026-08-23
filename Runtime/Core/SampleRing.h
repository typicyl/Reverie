// Reverie/Runtime/Core/SampleRing.h - a bounded lock-free SPSC bulk float ring.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Like SpscRing but for CONTIGUOUS f32 samples with bulk memcpy - the buffer between a background
// decode thread (producer) and the audio thread (consumer) for streaming voices. Single producer,
// single consumer; capacity is fixed at Init and rounded up to a power of two. Read/Write never
// block and never allocate.
#pragma once

#include "Core/Types.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

namespace reverie {

class SampleRing {
public:
    // capacitySamples is rounded up to a power of two (min 2). Call once before use.
    void Init(usize capacitySamples) {
        usize cap = 2;
        while (cap < capacitySamples) cap <<= 1;
        buf_.assign(cap, 0.0f);
        mask_ = cap - 1;
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    // Resets indices; only safe when neither side is concurrently accessing (e.g. at slot reuse).
    void Clear() {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    usize Capacity() const { return mask_ + 1; }
    usize ReadAvail() const {
        return static_cast<usize>(tail_.load(std::memory_order_acquire) -
                                  head_.load(std::memory_order_relaxed));
    }
    usize WriteAvail() const {
        const u64 used = tail_.load(std::memory_order_relaxed) - head_.load(std::memory_order_acquire);
        return Capacity() - static_cast<usize>(used);
    }

    // Producer: write up to n samples; returns the count actually written (<= n, limited by space).
    usize Write(const f32* src, usize n) {
        const u64 tail = tail_.load(std::memory_order_relaxed);
        const u64 head = head_.load(std::memory_order_acquire);
        const usize space = Capacity() - static_cast<usize>(tail - head);
        if (n > space) n = space;
        const usize idx = static_cast<usize>(tail) & mask_;
        const usize first = std::min(n, Capacity() - idx);
        std::memcpy(&buf_[idx], src, first * sizeof(f32));
        if (n > first) std::memcpy(&buf_[0], src + first, (n - first) * sizeof(f32));
        tail_.store(tail + n, std::memory_order_release);
        return n;
    }

    // Consumer: read up to n samples; returns the count actually read (<= n, limited by data).
    usize Read(f32* dst, usize n) {
        const u64 head = head_.load(std::memory_order_relaxed);
        const u64 tail = tail_.load(std::memory_order_acquire);
        const usize avail = static_cast<usize>(tail - head);
        if (n > avail) n = avail;
        const usize idx = static_cast<usize>(head) & mask_;
        const usize first = std::min(n, Capacity() - idx);
        std::memcpy(dst, &buf_[idx], first * sizeof(f32));
        if (n > first) std::memcpy(dst + first, &buf_[0], (n - first) * sizeof(f32));
        head_.store(head + n, std::memory_order_release);
        return n;
    }

private:
    std::vector<f32> buf_;
    usize mask_ = 0;
    alignas(64) std::atomic<u64> head_{0}; // consumer (audio thread)
    alignas(64) std::atomic<u64> tail_{0}; // producer (decode thread)
};

} // namespace reverie
