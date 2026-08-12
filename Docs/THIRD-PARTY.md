# Third-party components in Reverie

Reverie (Copyright (c) Hollow Dream Studios) incorporates the components below under their own
licenses. This file aggregates their attributions; each component's full license text is
retained alongside its source. Update this file whenever a dependency is added or removed.

## Currently vendored / linked

### miniaudio
- Location: `ThirdParty/miniaudio/` (single-header `miniaudio.h`, v0.11.x).
- Role: Reverie's default audio output device and audio decoding (WAV/FLAC/MP3), used strictly
  underneath Reverie's own `AudioDevice` / `AudioDecoder` abstractions. Not exposed publicly.
- License: dual-licensed, your choice of **public domain (Unlicense)** or **MIT-0**. See
  `ThirdParty/miniaudio/LICENSE`. No attribution is required, but it is retained for clarity.

## Planned (documented here ahead of integration)

### HDS Resonance Audio (spatial backend, added in the Resonance-backend phase)
When Reverie's `ISpatialRenderer` gains its Resonance implementation, Reverie will link the HDS
Resonance Audio fork. That fork carries these obligations, which Reverie must then honor and list
here (verified against the fork's actual files):
- **Google Resonance Audio** - Apache-2.0 (Copyright 2018 Google Inc.). The upstream project ships
  **no NOTICE file**; Reverie must retain the Apache-2.0 license + Google copyright headers and, if
  it modifies any Resonance source file, mark it as changed.
- **Eigen** - MPL-2.0 (weak, file-level copyleft; built with `-DEIGEN_MPL2_ONLY`). Linking imposes
  no copyleft on Reverie; ship `COPYING.MPL2` and disclose any modified Eigen file.
- **pffft** - FFTPACK/BSD-like (Julien Pommier 2013; UCAR/NCAR 2004). Retain notice + disclaimer;
  honor the no-endorsement clause. Compiled into the shipping library.
- **SADIE HRTF database** - Apache-2.0 in-repo; the assets are compiled into the library. The
  upstream SADIE dataset (Univ. of York) additionally carries academic/citation provenance not
  captured in that license file - complete legal diligence before shipping.
- googletest / ios-cmake - BSD-3, tooling/tests only (not shipped).

## Provenance classes (keep separate)
1. **Resonance-derived** code - Apache-2.0, Google copyright (in the separate HDSResonance-Audio repo).
2. **HDS modifications** to Resonance - documented in that repo's `MODIFICATIONS.md`.
3. **New HDS Reverie code** - this repository; Copyright (c) Hollow Dream Studios, proprietary.
