#include <system_error>
#include <thread>

#include <benchmark/benchmark.h>
#include <boost/outcome/outcome.hpp>

#include "katana/Random.h"
#include "katana/Result.h"

namespace {

void
MakeArguments(benchmark::internal::Benchmark* b) {
  for (long num_threads : {1, 4}) {
    for (long size : {1024, 64 * 1024}) {
      for (long depth : {16, 24, 32}) {
        for (long fail_delta : {-8, -1, 8}) {
          long fail_depth = depth + fail_delta;
          b->Args({num_threads, size, depth, fail_depth});
        }
      }
    }
  }
}

bool
ShouldStop(int depth, int max_depth) {
  if (depth >= max_depth) {
    return true;
  }

  int shift = max_depth - depth - 1;
  int64_t len = 1;
  len <<= shift;
  return katana::RandomUniformInt(len) == 0;
}

bool
ShouldFail(int depth, int fail_depth) {
  return ShouldStop(depth, fail_depth);
}

template <typename Scaffold>
void
Launch(benchmark::State& state, Scaffold& s) {
  int num_threads = state.range(0);
  long size = state.range(1);

  if (num_threads == 1) {
    for (long i = 0; i < size; ++i) {
      s.Start();
    }
    return;
  }

  std::vector<std::thread> threads;
  threads.reserve(num_threads);

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&]() {
      for (long i = 0; i < size; ++i) {
        s.Start();
      }
    });
  }

  for (std::thread& t : threads) {
    t.join();
  }
}

class ExceptionScaffold {
public:
  ExceptionScaffold(int max_depth, int fail_depth)
      : max_depth_(max_depth), fail_depth_(fail_depth) {}

  int Go(int depth) {
    // Proxy for minimal computation that can't be optimized away.
    int rv = katana::RandomUniformInt(1);

    if (ShouldStop(depth, max_depth_)) {
      return rv;
    }

    if (ShouldFail(depth, fail_depth_)) {
      throw depth;
    }

    if (rv) {
      return Go(depth + 1);
    }
    return GoWithCatch(depth + 1);
  }

  int GoWithCatch(int depth) {
    int rv = katana::RandomUniformInt(1);

    if (ShouldStop(depth, max_depth_)) {
      return rv;
    }

    try {
      if (ShouldFail(depth, fail_depth_)) {
        throw depth;
      }

      if (rv) {
        return Go(depth + 1);
      }
      return GoWithCatch(depth + 1);
    } catch (const int& e) {
      return e;
    }

    // unreachable
    std::abort();
  }

  int Start() { return GoWithCatch(0); }

  int max_depth_;
  int fail_depth_;
};

template <typename R>
class ResultScaffold {
public:
  ResultScaffold(int max_depth, int fail_depth)
      : max_depth_(max_depth), fail_depth_(fail_depth) {}

  R Go2(int depth) {
    // Proxy for minimal computation that can't be optimized away.
    int rv = katana::RandomUniformInt(1);

    if (ShouldStop(depth, max_depth_)) {
      return rv;
    }

    if (ShouldFail(depth, fail_depth_)) {
      return std::errc::invalid_argument;
    }

    if (rv) {
      return Go2(depth + 1);
    }

    return Go1(depth + 1);
  }

  R Go1(int depth) {
    int rv = katana::RandomUniformInt(1);

    if (ShouldStop(depth, max_depth_)) {
      return rv;
    }

    if (ShouldFail(depth, fail_depth_)) {
      if (depth == 0) {
        return rv;
      } else {
        return std::errc::invalid_argument;
      }
    }

    if (rv) {
      if (auto r = Go2(depth + 1); !r) {
        return rv;
      } else {
        return r.value();
      }
    }
    if (auto r = Go1(depth + 1); !r) {
      return rv;
    } else {
      return r.value();
    }
  }

  int Start() { return Go1(0).value(); }

  int max_depth_;
  int fail_depth_;
};

class StringErrorInfo : public std::error_code {
  std::string c;

public:
  StringErrorInfo() = default;

  template <
      typename ErrorEnum, typename U = std::enable_if_t<
                              std::is_error_code_enum_v<ErrorEnum> ||
                              std::is_error_condition_enum_v<ErrorEnum>>>
  StringErrorInfo(ErrorEnum&& e)
      : std::error_code(make_error_code(std::forward<ErrorEnum>(e))) {}
};

inline std::error_code
make_error_code(const StringErrorInfo& ec) {
  return ec;
}

void
ReturnException(benchmark::State& state) {
  ExceptionScaffold s(state.range(2), state.range(3));

  for (auto _ : state) {
    Launch(state, s);
  }
}

void
ReturnErrorCodeResult(benchmark::State& state) {
  using Result = BOOST_OUTCOME_V2_NAMESPACE::std_result<int>;

  ResultScaffold<Result> s(state.range(2), state.range(3));

  for (auto _ : state) {
    Launch(state, s);
  }
}

void
ReturnStringResult(benchmark::State& state) {
  using Result = BOOST_OUTCOME_V2_NAMESPACE::std_result<int, StringErrorInfo>;

  ResultScaffold<Result> s(state.range(2), state.range(3));

  for (auto _ : state) {
    Launch(state, s);
  }
}

void
ReturnKatanaResult(benchmark::State& state) {
  ResultScaffold<katana::Result<int>> s(state.range(2), state.range(3));

  for (auto _ : state) {
    Launch(state, s);
  }
}

BENCHMARK(ReturnException)->Apply(MakeArguments)->UseRealTime();
BENCHMARK(ReturnErrorCodeResult)->Apply(MakeArguments)->UseRealTime();
BENCHMARK(ReturnStringResult)->Apply(MakeArguments)->UseRealTime();
BENCHMARK(ReturnKatanaResult)->Apply(MakeArguments)->UseRealTime();

}  // namespace

BENCHMARK_MAIN();
