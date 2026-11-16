cmake_minimum_required(VERSION 3.20)
project(vectra VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VECTRA_BUILD_TESTS "Build the VECTRA test suite" ON)
option(VECTRA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /Zc:__cplusplus /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  if(VECTRA_WARNINGS_AS_ERRORS)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(VECTRA_WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
  endif()
endif()

# ---------------------------------------------------------------------------
# Core library (every component except the CLI entry point)
# ---------------------------------------------------------------------------
add_library(vectra_core STATIC
  src/market/market_event.cpp
  src/market/validator.cpp
  src/market/csv_reader.cpp
  src/behavior/behavior_engine.cpp
  src/behavior/exhaustion.cpp
  src/prediction/scoring_predictor.cpp
  src/risk/risk_engine.cpp
  src/execution/order.cpp
  src/execution/simulated_broker.cpp
  src/backtest/replay_engine.cpp
  src/backtest/report.cpp
  src/config/json.cpp
  src/config/settings.cpp
)

target_include_directories(vectra_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# Command line replay tool
# ---------------------------------------------------------------------------
add_executable(vectra src/main.cpp)
target_link_libraries(vectra PRIVATE vectra_core)

# ---------------------------------------------------------------------------
# Tests (self-contained harness; no external test framework required)
# ---------------------------------------------------------------------------
if(VECTRA_BUILD_TESTS)
  enable_testing()

  add_executable(vectra_tests
    tests/test_main.cpp
    tests/test_market.cpp
    tests/test_behavior.cpp
    tests/test_prediction.cpp
    tests/test_risk.cpp
    tests/test_execution.cpp
    tests/test_backtest.cpp
    tests/test_telemetry.cpp
    tests/test_config.cpp
  )
  target_link_libraries(vectra_tests PRIVATE vectra_core)
  target_include_directories(vectra_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

  # Run unit tests from the source tree so config/default.json is reachable.
  add_test(NAME vectra_unit_tests COMMAND vectra_tests
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME vectra_sample_replay
           COMMAND vectra --config ${CMAKE_CURRENT_SOURCE_DIR}/config/default.json
                   --data ${CMAKE_CURRENT_SOURCE_DIR}/examples/sample_ticks.csv
                   --report ${CMAKE_CURRENT_BINARY_DIR}/sample_report.md
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME vectra_cli_scale_invariance
           COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_cli_scale_invariance.py
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
