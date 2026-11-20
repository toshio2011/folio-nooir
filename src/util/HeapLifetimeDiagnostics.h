#pragma once

#ifndef NOOIR_HEAP_LIFETIME_TRACE
#define NOOIR_HEAP_LIFETIME_TRACE 0
#endif

#include "HeapLifetimeTraceStore.h"

namespace heap_lifetime_trace {

void start();
void setContext(CallerId caller);
void checkpoint(const char* label);
void stopAndDump();

}  // namespace heap_lifetime_trace
