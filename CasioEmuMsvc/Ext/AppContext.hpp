// Global lightweight context to hold shared services
#pragma once

#include "Ext/EventBus.hpp"

namespace casioemu {

struct AppContext {
  util::EventBus eventBus;
};

// Singleton-style accessor kept simple and testable
inline AppContext& GetAppContext() {
  static AppContext ctx;
  return ctx;
}

} // namespace casioemu



