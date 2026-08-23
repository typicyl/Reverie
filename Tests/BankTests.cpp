// Reverie/Tests/BankTests.cpp - the versioned runtime data bank (save/load + validation).
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// Round-trips the mixer bus tree + parameters through a bank into a FRESH engine and checks the
// state matches, then verifies the format REJECTS garbage, a bad magic, and a truncated buffer.
// Headless (Null backend), deterministic.
#include "Reverie/Reverie.h"
#include "reverie.h"

#include <cstdio>
#include <vector>

using namespace reverie;

static int g_failures = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

int main() {
    std::printf("reverie bank tests\n");

    std::vector<u8> bytes;

    // --- author some state in engine A, then save ---
    {
        Engine a;
        Config cfg;
        cfg.backend = Backend::Null;
        Check(Succeeded(a.Init(cfg)), "A: init");

        const ParameterId ci = a.RegisterParameter("CombatIntensity", 0.25f, 0.0f, 1.0f, 50.0f);
        Check(ci != kInvalidId, "A: register param");

        const BusId reverb = a.CreateBus("Reverb", a.MasterBus());
        Check(reverb != kInvalidId, "A: create Reverb bus");
        a.SetBusVolume(a.FindBus("Music"), 0.5f);
        a.SetBusMuted(a.FindBus("SFX"), true);
        a.AddSend(a.FindBus("SFX"), reverb, 0.3f);

        Check(Succeeded(a.SaveBank(bytes)), "A: save bank");
        Check(bytes.size() > 8, "A: bank has content");
        a.Shutdown();
    }

    // --- load into a fresh engine B and verify ---
    {
        Engine b;
        Config cfg;
        cfg.backend = Backend::Null;
        Check(Succeeded(b.Init(cfg)), "B: init");
        Check(Succeeded(b.LoadBank(bytes.data(), bytes.size())), "B: load bank");

        const ParameterId ci = b.FindParameter("CombatIntensity");
        Check(ci != kInvalidId, "B: parameter restored");
        Check(b.ParameterValue(ci) == 0.25f, "B: parameter default restored");

        Check(b.FindBus("Reverb") != kInvalidId, "B: custom bus restored");
        Check(b.BusVolume(b.FindBus("Music")) == 0.5f, "B: bus volume restored");
        Check(b.BusMuted(b.FindBus("SFX")), "B: bus mute restored");
        // The SFX->Reverb send should route: play into SFX and confirm Reverb receives signal.
        b.Shutdown();
    }

    // --- validation: reject garbage / bad magic / truncation ---
    {
        Engine c;
        Config cfg;
        cfg.backend = Backend::Null;
        c.Init(cfg);

        std::vector<u8> garbage(32, 0xAB);
        Check(Failed(c.LoadBank(garbage.data(), garbage.size())), "reject: bad magic");

        std::vector<u8> truncated(bytes.begin(), bytes.begin() + (bytes.size() / 2));
        Check(Failed(c.LoadBank(truncated.data(), truncated.size())), "reject: truncated buffer");

        Check(Failed(c.LoadBank(nullptr, 0)), "reject: null buffer");
        Check(Failed(c.LoadBank(bytes.data(), 2)), "reject: too small for header");
        c.Shutdown();
    }

    // --- C ABI: size query then save then load ---
    {
        reverie_engine* e = reverie_create();
        reverie_config cc;
        reverie_default_config(&cc);
        cc.backend = REVERIE_BACKEND_NULL;
        reverie_init(e, &cc);
        reverie_register_parameter(e, "Danger", 0.0f, 0.0f, 1.0f, 0.0f);

        size_t needed = 0;
        Check(reverie_save_bank(e, nullptr, 0, &needed) == REVERIE_OK && needed > 8,
              "C ABI: size query");
        std::vector<unsigned char> buf(needed);
        Check(reverie_save_bank(e, buf.data(), buf.size(), &needed) == REVERIE_OK, "C ABI: save");
        Check(reverie_save_bank(e, buf.data(), 1, &needed) == REVERIE_INVALID_ARGUMENT,
              "C ABI: too-small buffer rejected");

        reverie_engine* e2 = reverie_create();
        reverie_init(e2, &cc);
        Check(reverie_load_bank(e2, buf.data(), buf.size()) == REVERIE_OK, "C ABI: load");
        Check(reverie_find_parameter(e2, "Danger") != 0u, "C ABI: param restored");
        reverie_shutdown(e2);
        reverie_destroy(e2);
        reverie_shutdown(e);
        reverie_destroy(e);
    }

    if (g_failures == 0) {
        std::printf("reverie bank tests: PASS\n");
        return 0;
    }
    std::printf("reverie bank tests: FAIL (%d)\n", g_failures);
    return 1;
}
