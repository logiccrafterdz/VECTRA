// VECTRA test runner.
//
// Exit code is non-zero if any test fails; failures are never hidden.
#include "vectra_test.hpp"

#include <cstring>
#include <string>

namespace {

void print_usage() {
  std::printf("Usage: vectra_tests [--list] [--filter <substring>]\n");
}

}  // namespace

int main(int argc, char** argv) {
  bool list_only = false;
  std::string filter;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--list") == 0) {
      list_only = true;
    } else if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
      filter = argv[++i];
    } else if (std::strcmp(argv[i], "--help") == 0) {
      print_usage();
      return 0;
    } else {
      std::fprintf(stderr, "Unknown argument: %s\n", argv[i]);
      print_usage();
      return 2;
    }
  }

  auto& tests = vectra::testing::registry();
  std::size_t executed = 0;
  std::size_t passed = 0;
  std::size_t failed = 0;

  if (list_only) {
    for (const auto& test : tests) {
      std::printf("%s.%s\n", test.suite, test.name);
    }
    return 0;
  }

  for (const auto& test : tests) {
    const std::string full_name = std::string(test.suite) + "." + test.name;
    if (!filter.empty() && full_name.find(filter) == std::string::npos) {
      continue;
    }
    ++executed;
    try {
      test.fn();
      ++passed;
      std::printf("[  OK  ] %s\n", full_name.c_str());
    } catch (const vectra::testing::AssertFailed&) {
      ++failed;
      std::printf("[ FAIL] %s\n", full_name.c_str());
    } catch (const std::exception& ex) {
      ++failed;
      std::printf("[ FAIL] %s\n    unexpected exception: %s\n", full_name.c_str(), ex.what());
    } catch (...) {
      ++failed;
      std::printf("[ FAIL] %s\n    unexpected non-standard exception\n", full_name.c_str());
    }
  }

  std::printf("\n--------------------------------------------------\n");
  std::printf("tests run: %zu, passed: %zu, failed: %zu\n", executed, passed, failed);
  if (executed == 0) {
    std::fprintf(stderr, "error: no tests matched the filter\n");
    return 2;
  }
  return failed == 0 ? 0 : 1;
}
