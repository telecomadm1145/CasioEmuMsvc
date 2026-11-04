#pragma once

#include "Config.hpp"

#if defined(_WIN32) && defined(ENABLE_SENTRY)
#include <sentry.h>

void ConfigureSentryCrashHandler(sentry_options_t* options);
#endif
