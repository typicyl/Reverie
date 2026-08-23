// Reverie/Runtime/Serialization/Bank.h - the versioned runtime data bank.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// A bank is Reverie's cooked, versioned, engine-independent runtime data: the mixer bus tree and
// the game-parameter (RTPC) definitions (events/audio follow in later versions). BankData is the
// neutral in-memory form; WriteBank/ReadBank are the on-disk binary codec. The header carries a
// magic and a format version so a mismatched or corrupt buffer is REJECTED (Result::Unsupported /
// Result::Error), never misread - the reliability bar a professional middleware needs. This is the
// authoring->runtime seam: tools produce a bank; the runtime loads it.
#pragma once

#include "Core/Types.h"

#include <string>
#include <vector>

namespace reverie {

// Bump when the binary layout changes incompatibly. ReadBank rejects a newer version.
constexpr u32 kBankVersion = 1;
constexpr u32 kBankMagic = 0x42534448u; // 'HDSB' little-endian

struct BankParam {
    std::string name;
    f32 defaultValue = 0.0f;
    f32 minValue = 0.0f;
    f32 maxValue = 1.0f;
    f32 smoothMs = 0.0f;
};

struct BankSend {
    std::string destName;
    f32 level = 1.0f;
};

struct BankBus {
    std::string name;
    std::string parentName; // empty = Master
    f32 gain = 1.0f;
    bool muted = false;
    bool soloed = false;
    std::vector<BankSend> sends;
};

struct BankData {
    u32 version = kBankVersion;
    std::vector<BankParam> parameters;
    std::vector<BankBus> buses; // in creation order (parents precede children)
};

// Serializes `data` to `out` (appends). Always succeeds for valid data.
Result WriteBank(const BankData& data, std::vector<u8>& out);

// Parses + validates a bank. Result::Unsupported on magic/version mismatch, Result::Error on a
// truncated/corrupt buffer. On success `out` holds the decoded data.
Result ReadBank(const u8* data, usize size, BankData& out);

} // namespace reverie
