#ifndef KATANA_LIBSUPPORT_KATANA_RESULT_H_
#define KATANA_LIBSUPPORT_KATANA_RESULT_H_

#include <cerrno>
#include <cstring>
#include <ostream>
#include <system_error>

#include <boost/outcome/outcome.hpp>
#include <boost/outcome/utils.hpp>
#include <fmt/format.h>

#include "katana/Logging.h"

namespace katana {

namespace internal {

struct abort_policy : BOOST_OUTCOME_V2_NAMESPACE::policy::base {
  template <class Impl>
  static constexpr void wide_value_check(Impl&& self) {
    if (!base::_has_value(std::forward<Impl>(self))) {
      AbortApplication();
    }
  }

  template <class Impl>
  static constexpr void wide_error_check(Impl&& self) {
    if (!base::_has_error(std::forward<Impl>(self))) {
      AbortApplication();
    }
  }

  template <class Impl>
  static constexpr void wide_exception_check(Impl&& self) {
    if (!base::_has_exception(std::forward<Impl>(self))) {
      AbortApplication();
    }
  }
};

}  // namespace internal

/// An ErrorInfo
///
/// TODO(ddn): Describe WithContext and the limitations of ErrorInfo.
class [[nodiscard]] ErrorInfo {
  constexpr static int kContextSize = 512;
  static_assert(
      fmt::inline_buffer_size > kContextSize / 2,
      "libfmt buffer size is small relative to max ErrorInfo context size");

public:
  ErrorInfo() = default;

  template <
      typename ErrorEnum, typename U = std::enable_if_t<
                              std::is_error_code_enum_v<ErrorEnum> ||
                              std::is_error_condition_enum_v<ErrorEnum>>>
  ErrorInfo(ErrorEnum && err)
      : error_code_(make_error_code(std::forward<ErrorEnum>(err))) {}

  ErrorInfo(const std::error_code& ec) : error_code_(ec) {}

  std::error_code error_code() { return error_code_; }

  void ThrowException() const;

  /// MakeWithSourceInfo makes an ErrorInfo from a root error with additional
  /// arguments passed to fmt::format
  template <typename F, typename... Args>
  static ErrorInfo MakeWithSourceInfo(
      const char* file_name, int line_no, const std::error_code& ec,
      F fmt_string, Args... args) {
    fmt::memory_buffer out;
    fmt::format_to(out, fmt_string, std::forward<Args>(args)...);
    const char* base_name = std::strrchr(file_name, '/');
    if (!base_name) {
      base_name = file_name;
    } else {
      base_name++;
    }

    fmt::format_to(out, " at ({}:{})", base_name, line_no);

    return ErrorInfo(ec, out.begin(), out.end());
  }

  template <typename F, typename... Args>
  ErrorInfo WithContext(F fmt_string, Args... args) {
    // TODO(ddn): too much copying
    fmt::memory_buffer out;
    fmt::format_to(out, fmt_string, std::forward<Args>(args)...);
    fmt::format_to(out, ": {}", context_);

    return ErrorInfo(error_code_, out.begin(), out.end());
  }

  template <typename ErrorEnum, typename F, typename... Args>
  std::enable_if_t<
      std::is_error_code_enum_v<ErrorEnum> ||
          std::is_error_condition_enum_v<ErrorEnum>,
      ErrorInfo>
  WithContext(ErrorEnum err, F fmt_string, Args... args) {
    // TODO(ddn): spill previous error context to buffer
    fmt::memory_buffer out;
    fmt::format_to(out, fmt_string, std::forward<Args>(args)...);
    fmt::format_to(out, ": {}", context_);

    return ErrorInfo(make_error_code(err), out.begin(), out.end());
  }

  friend std::ostream& operator<<(std::ostream& out, const ErrorInfo& ei) {
    return ei.Write(out);
  }

private:
  ErrorInfo(
      std::error_code ec, const char* context_begin, const char* context_end);

  std::ostream& Write(std::ostream & out) const {
    if (context_.empty()) {
      out << error_code_.message();
    } else {
      out << context_;
    }
    return out;
  }

  std::error_code error_code_;

  // TODO(ddn): Replace with pre-allocated thread-local buffer to make memory
  // usage more predictable

  // TODO(ddn): Store backtraces sometimes
  std::string context_;
};

/// KATANA_RESULT_ERROR creates new ErrorInfo and records information about the
/// callsite (e.g., line number or stacktrace).
#define KATANA_RESULT_ERROR(ec, fmt_string, ...)                               \
  ::katana::ErrorInfo::MakeWithSourceInfo(                                     \
      __FILE__, __LINE__, (ec), FMT_STRING(fmt_string), ##__VA_ARGS__);

/// make_error_code converts ErrorInfo into a standard error code. It is an STL
/// and outcome extension point and will be found with ADL if necessary.
inline std::error_code
make_error_code(ErrorInfo e) noexcept {
  return e.error_code();
}

/// A Result is a T or an ErrorInfo.
///
/// TODO(ddn): Describe Result, WithContext and the limitations of ErrorInfo,
/// portable errors via conversion to std::error_code or CopyableErrorInfo
///
/// See ErrorCode.h for an example of defining a new error code.
template <class T>
using Result = BOOST_OUTCOME_V2_NAMESPACE::std_result<
    T, ErrorInfo, internal::abort_policy>;

inline bool
operator==(const ErrorInfo& a, const ErrorInfo& b) {
  return make_error_code(a) == make_error_code(b);
}

inline bool
operator!=(const ErrorInfo& a, const ErrorInfo& b) {
  return !(a == b);
}

inline auto
ResultSuccess() {
  return BOOST_OUTCOME_V2_NAMESPACE::success();
}

inline std::error_code
ResultErrno() {
  KATANA_LOG_DEBUG_ASSERT(errno);
  return std::error_code(errno, std::system_category());
}

}  // namespace katana

#endif
