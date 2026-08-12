// Reverie/Runtime/Core/Log.cpp - see Log.h.
//
// Copyright (c) Hollow Dream Studios. All rights reserved.
#include "Core/Log.h"

#include <cstdarg>
#include <cstdio>

namespace reverie {

namespace {

void DefaultSink(void* /*user*/, LogLevel level, const char* message) {
    const char* tag = "";
    switch (level) {
        case LogLevel::Trace:   tag = "[reverie trace] "; break;
        case LogLevel::Info:    tag = "[reverie] ";       break;
        case LogLevel::Warning: tag = "[reverie warn] ";  break;
        case LogLevel::Error:   tag = "[reverie ERROR] "; break;
    }
    std::fprintf(stderr, "%s%s\n", tag, message);
}

LogSink g_sink = &DefaultSink;
void* g_user = nullptr;

} // namespace

void SetLogSink(LogSink sink, void* user) {
    g_sink = sink != nullptr ? sink : &DefaultSink;
    g_user = user;
}

void LogMessage(LogLevel level, const char* message) {
    if (g_sink != nullptr && message != nullptr) g_sink(g_user, level, message);
}

void LogFormat(LogLevel level, const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    LogMessage(level, buffer);
}

} // namespace reverie
