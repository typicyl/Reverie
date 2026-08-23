// Reverie/Runtime/Serialization/Bank.cpp - see Bank.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Layout (all little-endian): [magic u32][version u32] then two sections:
//   params: [count u32] { name str, default f32, min f32, max f32, smoothMs f32 } * count
//   buses:  [count u32] { name str, parent str, gain f32, muted u8, soloed u8,
//                         [sendCount u32] { destName str, level f32 } * sendCount } * count
#include "Serialization/Bank.h"

#include "Serialization/BinaryStream.h"

namespace reverie {

Result WriteBank(const BankData& data, std::vector<u8>& out) {
    BinaryWriter w(out);
    w.U32(kBankMagic);
    w.U32(kBankVersion);

    w.U32(static_cast<u32>(data.parameters.size()));
    for (const BankParam& p : data.parameters) {
        w.Str(p.name);
        w.F32(p.defaultValue);
        w.F32(p.minValue);
        w.F32(p.maxValue);
        w.F32(p.smoothMs);
    }

    w.U32(static_cast<u32>(data.buses.size()));
    for (const BankBus& b : data.buses) {
        w.Str(b.name);
        w.Str(b.parentName);
        w.F32(b.gain);
        w.U8(b.muted ? 1u : 0u);
        w.U8(b.soloed ? 1u : 0u);
        w.U32(static_cast<u32>(b.sends.size()));
        for (const BankSend& s : b.sends) {
            w.Str(s.destName);
            w.F32(s.level);
        }
    }
    return Result::Ok;
}

Result ReadBank(const u8* data, usize size, BankData& out) {
    if (data == nullptr) return Result::InvalidArgument;
    BinaryReader r(data, size);

    const u32 magic = r.U32();
    if (!r.Ok() || magic != kBankMagic) return Result::Unsupported;
    const u32 version = r.U32();
    if (!r.Ok() || version == 0 || version > kBankVersion) return Result::Unsupported;
    out = BankData{};
    out.version = version;

    const u32 paramCount = r.U32();
    if (!r.Ok()) return Result::Error;
    // Guard against an absurd count from a corrupt header (each param needs > paramCount bytes min).
    if (paramCount > r.Remaining()) return Result::Error;
    out.parameters.reserve(paramCount);
    for (u32 i = 0; i < paramCount; ++i) {
        BankParam p;
        p.name = r.Str();
        p.defaultValue = r.F32();
        p.minValue = r.F32();
        p.maxValue = r.F32();
        p.smoothMs = r.F32();
        if (!r.Ok()) return Result::Error;
        out.parameters.push_back(std::move(p));
    }

    const u32 busCount = r.U32();
    if (!r.Ok()) return Result::Error;
    if (busCount > r.Remaining()) return Result::Error;
    out.buses.reserve(busCount);
    for (u32 i = 0; i < busCount; ++i) {
        BankBus b;
        b.name = r.Str();
        b.parentName = r.Str();
        b.gain = r.F32();
        b.muted = r.U8() != 0;
        b.soloed = r.U8() != 0;
        const u32 sendCount = r.U32();
        if (!r.Ok() || sendCount > r.Remaining()) return Result::Error;
        b.sends.reserve(sendCount);
        for (u32 s = 0; s < sendCount; ++s) {
            BankSend snd;
            snd.destName = r.Str();
            snd.level = r.F32();
            if (!r.Ok()) return Result::Error;
            b.sends.push_back(std::move(snd));
        }
        if (!r.Ok()) return Result::Error;
        out.buses.push_back(std::move(b));
    }
    return Result::Ok;
}

} // namespace reverie
