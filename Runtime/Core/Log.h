// Reverie/Runtime/Core/Log.h - dependency-free logging + assert.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
//
// A tiny logging seam so the runtime never pulls in a logging framework. The host can
// install a sink to route Reverie diagnostics into its own log; by default messages go to
// stderr. No <format>/fmt dependency - callers pass an already-formatted string.
#pragma once

namespace reverie {

enum class LogLevel { Trace, Info, Warning, Error };

// Signature of a host-installed log sink. `user` is the pointer passed to SetLogSink.
using LogSink = void (*)(void* user, LogLevel level, const char* message);

// Installs a sink (nullptr restores the default stderr sink). Not thread-safe with respect
// to concurrent logging; set it once at startup.
void SetLogSink(LogSink sink, void* user);

// Emits a message. `message` is a null-terminated, already-formatted string.
void LogMessage(LogLevel level, const char* message);

// printf-style convenience (formats into a fixed stack buffer; long messages are truncated).
void LogFormat(LogLevel level, const char* fmt, ...);

} // namespace reverie

// Lightweight assert that logs and (in debug) traps, without pulling in <cassert>'s abort
// semantics into a realtime audio callback. In release it evaluates to nothing.
#if defined(NDEBUG)
#define REVERIE_ASSERT(cond, msg) ((void)0)
#else
#define REVERIE_ASSERT(cond, msg)                                                          \
    do {                                                                                   \
        if (!(cond)) {                                                                      \
            ::reverie::LogFormat(::reverie::LogLevel::Error, "ASSERT %s:%d: %s (%s)",       \
                                 __FILE__, __LINE__, (msg), #cond);                          \
        }                                                                                  \
    } while (0)
#endif
