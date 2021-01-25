#include "katana/Result.h"

#include <sstream>
#include <type_traits>

#include "katana/ErrorCode.h"
#include "katana/Logging.h"

int
main() {
  static_assert(std::is_convertible_v<katana::ErrorCode, std::error_code>);

  static_assert(
      !std::is_convertible_v<katana::ErrorInfo, std::error_code>,
      "Do not allow implicit conversion from ErrorInfo to error_code becase it "
      "is lossy");

  // Check error_code to error_condition
  std::error_code not_found = katana::ErrorCode::NotFound;

  KATANA_LOG_VASSERT(
      not_found == std::errc::no_such_file_or_directory,
      "expected custom error code to be convertable to std error condition");

  // Check printing
  katana::Result<void> res = not_found;

  std::stringstream out;
  out << res.error();
  KATANA_LOG_VASSERT(
      out.str() == "not found", "expected string 'not found' to found: {}",
      out.str());
}
