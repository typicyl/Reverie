// Reverie/Runtime/Platform/MiniaudioImpl.cpp - the single miniaudio implementation TU.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// miniaudio is a single-header library; its implementation must be compiled in exactly ONE
// translation unit across the whole linked image. That TU is this file, and only this file.
// Every other Reverie file includes "miniaudio.h" for declarations only. If Reverie is ever
// linked into a host that also compiles miniaudio, exactly one MINIAUDIO_IMPLEMENTATION must
// remain - Reverie's - and the host must stop compiling its own.
//
// Decoding backends (WAV/FLAC/MP3) are kept for AudioDecoder/AudioStream; encoding is not
// needed by the runtime.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#include "miniaudio.h"
