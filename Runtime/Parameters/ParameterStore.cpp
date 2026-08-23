// Reverie/Runtime/Parameters/ParameterStore.cpp - see ParameterStore.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Parameters/ParameterStore.h"

#include <cmath>

namespace reverie {

namespace {
inline f32 Clamp(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

void ParameterStore::Reserve(u32 capacity) {
    if (capacity < 1) capacity = 1;
    if (capacity <= capacity_) return; // never shrink
    params_ = std::make_unique<Param[]>(capacity);
    capacity_ = capacity;
    count_ = 0;
    byName_.clear();
    byName_.reserve(capacity * 2);
    names_.clear();
    names_.reserve(capacity);
}

ParameterStore::Param* ParameterStore::Slot(ParameterId id) {
    if (id == kInvalidId || id > capacity_) return nullptr;
    Param& p = params_[id - 1];
    return p.used.load(std::memory_order_acquire) ? &p : nullptr;
}

const ParameterStore::Param* ParameterStore::Slot(ParameterId id) const {
    if (id == kInvalidId || id > capacity_) return nullptr;
    const Param& p = params_[id - 1];
    return p.used.load(std::memory_order_acquire) ? &p : nullptr;
}

ParameterId ParameterStore::Register(const char* name, f32 defaultValue, f32 minValue, f32 maxValue,
                                     f32 smoothMs) {
    if (name == nullptr || capacity_ == 0) return kInvalidId;
    if (ParameterId existing = Find(name); existing != kInvalidId) return existing;
    if (count_ >= capacity_) return kInvalidId; // pool full
    if (maxValue < minValue) { const f32 t = minValue; minValue = maxValue; maxValue = t; }

    const u32 slot = count_; // slots are handed out densely; the id is slot+1
    Param& p = params_[slot];
    const f32 def = Clamp(defaultValue, minValue, maxValue);
    p.minValue = minValue;
    p.maxValue = maxValue;
    p.defaultValue = def;
    p.smoothMs = smoothMs < 0.0f ? 0.0f : smoothMs;
    p.current.store(def, std::memory_order_relaxed);
    p.target.store(def, std::memory_order_relaxed);
    p.used.store(1, std::memory_order_release); // PUBLISH last

    const ParameterId id = static_cast<ParameterId>(slot + 1);
    byName_[name] = id;
    names_.push_back(name);
    ++count_;
    return id;
}

bool ParameterStore::DescribeAt(u32 index, Descriptor& out) const {
    if (index >= count_) return false;
    const Param& p = params_[index];
    out.name = index < names_.size() ? names_[index] : std::string();
    out.defaultValue = p.defaultValue;
    out.minValue = p.minValue;
    out.maxValue = p.maxValue;
    out.smoothMs = p.smoothMs;
    return true;
}

ParameterId ParameterStore::Find(const char* name) const {
    if (name == nullptr) return kInvalidId;
    auto it = byName_.find(name);
    return it != byName_.end() ? it->second : kInvalidId;
}

void ParameterStore::SetTarget(ParameterId id, f32 value) {
    Param* p = Slot(id);
    if (p == nullptr) return;
    p->target.store(Clamp(value, p->minValue, p->maxValue), std::memory_order_relaxed);
}

f32 ParameterStore::Target(ParameterId id) const {
    const Param* p = Slot(id);
    return p != nullptr ? p->target.load(std::memory_order_relaxed) : 0.0f;
}

f32 ParameterStore::Value(ParameterId id) const {
    const Param* p = Slot(id);
    return p != nullptr ? p->current.load(std::memory_order_relaxed) : 0.0f;
}

void ParameterStore::Advance(f32 blockSeconds) {
    if (capacity_ == 0 || blockSeconds <= 0.0f) return;
    // Only the densely-packed [0, count_) slots can be in use. count_ is written by the control
    // thread on Register; the audio thread may read a slightly stale (smaller) count_ - harmless,
    // it just advances a newly-registered parameter one block later.
    const u32 n = count_;
    for (u32 i = 0; i < n; ++i) {
        Param& p = params_[i];
        if (!p.used.load(std::memory_order_acquire)) continue;
        const f32 target = p.target.load(std::memory_order_relaxed);
        const f32 current = p.current.load(std::memory_order_relaxed);
        if (current == target) continue;
        f32 next;
        if (p.smoothMs <= 0.0f) {
            next = target;
        } else {
            const f32 tau = p.smoothMs / 1000.0f;
            const f32 coef = 1.0f - std::exp(-blockSeconds / tau); // 0..1 per block
            next = current + (target - current) * coef;
            // Snap when within a tiny epsilon so a value actually reaches its target.
            if (std::fabs(target - next) < 1e-6f) next = target;
        }
        p.current.store(next, std::memory_order_relaxed);
    }
}

} // namespace reverie
