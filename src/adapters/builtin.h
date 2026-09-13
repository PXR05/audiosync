#ifndef AUDIOSYNC_BUILTIN_ADAPTERS_H
#define AUDIOSYNC_BUILTIN_ADAPTERS_H

#include "adapters/adapter.h"

#define ADAPTER(name) extern const remote_adapter_t name##_adapter;
#include "adapters/adapters.def"
#undef ADAPTER

#endif
