// VECTRA self-contained test harness.
//
// Rationale: the project must build with zero external dependencies, so the
// test framework is intentionally tiny: static registration, assertion macros
// that throw, and a runner that reports every failure (nothing is hidden).
#pragma once

#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace vectra::testing {

// Thrown by failing ASSERT macros; caught by the runner.
struct AssertFailed {};

struct TestCase {
  const char* suite;
  const char* name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> instances;
  return instances;
}

struct Registrar {
  Registrar(const char* suite, const char* name, std::function<void()> fn) {
    registry().push_back(TestCase{suite, name, std::move(fn)});
  }
};

template <class T>
std::string dbg(const T& v) {
  if constexpr (requires(std::ostream& os, const T& x) { os << x; }) {
    std::ostringstream os;
    os << v;
    return os.str();
  } else if constexpr (std::is_enum_v<std::remove_cvref_t<T>>) {
    return std::to_string(static_cast<long long>(static_cast<std::underlying_type_t<std::remove_cvref_t<T>>>(v)));
  } else {
    return "<value>";
  }
}

[[noreturn]] inline void fail_at(const char* file, int line, const std::string& message) {
  std::fprintf(stderr, "    FAILED %s:%d\n      %s\n", file, line, message.c_str());
  throw AssertFailed{};
}

}  // namespace vectra::testing

#define VT_TEST(SUITE, NAME)                                                        \
  static void vectra_test_##SUITE##_##NAME();                                       \
  static ::vectra::testing::Registrar vectra_reg_##SUITE##_##NAME(                  \
      #SUITE, #NAME, &vectra_test_##SUITE##_##NAME);                                \
  static void vectra_test_##SUITE##_##NAME()

#define VT_FAIL(msg) ::vectra::testing::fail_at(__FILE__, __LINE__, (msg))

#define VT_ASSERT_TRUE(cond)                                                     \
  do {                                                                           \
    if (!(cond)) {                                                               \
      ::vectra::testing::fail_at(__FILE__, __LINE__, "expected true: " #cond);   \
    }                                                                            \
  } while (false)

#define VT_ASSERT_FALSE(cond)                                                    \
  do {                                                                           \
    if ((cond)) {                                                                \
      ::vectra::testing::fail_at(__FILE__, __LINE__, "expected false: " #cond);  \
    }                                                                            \
  } while (false)

#define VT_ASSERT_EQ(a, b)                                                                    \
  do {                                                                                        \
    const auto& vt_a_ = (a);                                                                  \
    const auto& vt_b_ = (b);                                                                  \
    if (!(vt_a_ == vt_b_)) {                                                                  \
      ::vectra::testing::fail_at(__FILE__, __LINE__,                                          \
                                 std::string("expected " #a " == " #b " | got ") +            \
                                     ::vectra::testing::dbg(vt_a_) + " vs " +                 \
                                     ::vectra::testing::dbg(vt_b_));                          \
    }                                                                                         \
  } while (false)

#define VT_ASSERT_NE(a, b)                                                             \
  do {                                                                                 \
    const auto& vt_a_ = (a);                                                           \
    const auto& vt_b_ = (b);                                                           \
    if ((vt_a_ == vt_b_)) {                                                            \
      ::vectra::testing::fail_at(__FILE__, __LINE__,                                  \
                                 std::string("expected " #a " != " #b " | both ") +    \
                                     ::vectra::testing::dbg(vt_a_));                   \
    }                                                                                  \
  } while (false)

#define VT_ASSERT_NEAR(a, b, tol)                                                              \
  do {                                                                                         \
    const double vt_a_ = static_cast<double>(a);                                               \
    const double vt_b_ = static_cast<double>(b);                                               \
    const double vt_tol_ = static_cast<double>(tol);                                           \
    if (!(std::fabs(vt_a_ - vt_b_) <= vt_tol_)) {                                              \
      ::vectra::testing::fail_at(__FILE__, __LINE__,                                           \
                                 std::string("expected |" #a " - " #b "| <= " #tol " | ") +    \
                                     ::vectra::testing::dbg(vt_a_) + " vs " +                  \
                                     ::vectra::testing::dbg(vt_b_) + " (d=" +                  \
                                     ::vectra::testing::dbg(vt_a_ - vt_b_) + ")");             \
    }                                                                                          \
  } while (false)

#define VT_ASSERT_STREQ(a, b)                                                           \
  do {                                                                                  \
    const std::string vt_a_ = (a);                                                      \
    const std::string vt_b_ = (b);                                                      \
    if (vt_a_ != vt_b_) {                                                               \
      ::vectra::testing::fail_at(__FILE__, __LINE__,                                    \
                                 std::string("expected " #a " == " #b " | got \"") +    \
                                     vt_a_ + "\" vs \"" + vt_b_ + "\"");                \
    }                                                                                   \
  } while (false)
