#include "katana/Result.h"


katana::ErrorInfo::ErrorInfo(std::error_code ec, const char* context_begin, const char* context_end) {
  error_code_ = ec;
  (void) context_begin;
  (void) context_end;
  // TODO(ddn) copy context
}
