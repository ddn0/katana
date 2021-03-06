// Backtrace information.  backward-cpp prints an informative backtrace on abrupt
// termination, but also provide program interface.

#include "katana/Backtrace.h"

#include "katana/Logging.h"

static uint32_t backtrace_id{0};

#include <backward.hpp>
// Install signal handlers
backward::SignalHandling sh;

static void
default_signals() {
  std::vector<int> posix_signals = sh.make_default_signals();
  for (size_t i = 0; i < posix_signals.size(); ++i) {
    int r = sigaction(posix_signals[i], nullptr, nullptr);
    if (r < 0) {
      KATANA_LOG_DEBUG(
          "failed to revert signal handler to default: {}", posix_signals[i]);
    }
  }
}

KATANA_EXPORT void
katana::PrintBacktrace() {
  if (backtrace_id == 0) {
    using namespace backward;
    StackTrace st;
    st.load_here(32);
    Printer p;
    p.print(st);
  }
}

KATANA_EXPORT void
katana::InitBacktrace(uint32_t ID) {
  backtrace_id = ID;
  // Only have one process print backtrace
  if (backtrace_id != 0) {
    default_signals();
  }
}
