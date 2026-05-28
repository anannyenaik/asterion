cmake_minimum_required(VERSION 3.20)

project(
  asterion
  VERSION 0.1.0
  DESCRIPTION "Deterministic low-latency trading systems lab"
  LANGUAGES CXX)

option(ASTERION_BUILD_TESTS "Build Asterion tests" ON)
option(ASTERION_BUILD_BENCHMARKS "Build Asterion benchmark runner" ON)
option(ASTERION_USE_GOOGLE_BENCHMARK "Build optional Google Benchmark target" OFF)
option(ASTERION_ENABLE_WARNINGS "Enable strict compiler warnings" ON)
option(ASTERION_ENABLE_SANITIZERS "Enable Address/Undefined sanitizers" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(ASTERION_GIT_COMMIT "unknown")
find_package(Git QUIET)
if(Git_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE ASTERION_GIT_COMMIT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

if(CMAKE_BUILD_TYPE)
  set(ASTERION_CONFIG_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
  set(ASTERION_CONFIG_BUILD_TYPE "unknown")
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" ASTERION_BUILD_TYPE_UPPER)
set(ASTERION_CONFIG_COMPILER_FLAGS
    "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${ASTERION_BUILD_TYPE_UPPER}}")
string(STRIP "${ASTERION_CONFIG_COMPILER_FLAGS}" ASTERION_CONFIG_COMPILER_FLAGS)

if(ASTERION_BUILD_TESTS OR (ASTERION_BUILD_BENCHMARKS AND ASTERION_USE_GOOGLE_BENCHMARK))
  include(FetchContent)
endif()

add_library(
  asterion
  cpp/src/core/checksum.cpp
  cpp/src/core/clock.cpp
  cpp/src/market_data/event.cpp
  cpp/src/market_data/event_log.cpp
  cpp/src/market_data/replay.cpp
  cpp/src/market_data/synthetic_generator.cpp
  cpp/src/book/order_book.cpp
  cpp/src/matching/execution_report.cpp
  cpp/src/matching/matching_engine.cpp
  cpp/src/risk/kill_switch.cpp
  cpp/src/risk/risk_gateway.cpp
  cpp/src/strategy/imbalance_strategy.cpp
  cpp/src/strategy/market_maker.cpp
  cpp/src/inference/feature_extractor.cpp
  cpp/src/inference/linear_model.cpp
  cpp/src/telemetry/latency_histogram.cpp
  cpp/src/telemetry/metrics.cpp)

target_include_directories(asterion PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpp/include)

add_library(asterion_allocation_tracker STATIC cpp/src/core/allocation_tracker.cpp)
target_include_directories(asterion_allocation_tracker
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpp/include)

add_executable(asterion_replay cpp/tools/replay_main.cpp)
target_link_libraries(asterion_replay PRIVATE asterion)

if(ASTERION_ENABLE_WARNINGS)
  if(MSVC)
    target_compile_options(asterion PRIVATE /W4 /permissive-)
    target_compile_options(asterion_allocation_tracker PRIVATE /W4 /permissive-)
    target_compile_options(asterion_replay PRIVATE /W4 /permissive-)
  else()
    target_compile_options(asterion PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(asterion_allocation_tracker PRIVATE -Wall -Wextra -Wpedantic
                                                               -Wconversion -Wshadow)
    target_compile_options(asterion_replay PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  endif()
endif()

if(ASTERION_ENABLE_SANITIZERS)
  if(NOT MSVC)
    target_compile_options(asterion PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(asterion PUBLIC -fsanitize=address,undefined)
    target_compile_options(asterion_allocation_tracker PUBLIC -fsanitize=address,undefined
                                                              -fno-omit-frame-pointer)
    target_link_options(asterion_allocation_tracker PUBLIC -fsanitize=address,undefined)
    target_compile_options(asterion_replay PUBLIC -fsanitize=address,undefined
                                                  -fno-omit-frame-pointer)
    target_link_options(asterion_replay PUBLIC -fsanitize=address,undefined)
  endif()
endif()

if(ASTERION_BUILD_TESTS)
  include(CTest)

  find_package(Catch2 3 QUIET)
  if(NOT Catch2_FOUND)
    FetchContent_Declare(
      Catch2
      URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.6.0.tar.gz)
    FetchContent_MakeAvailable(Catch2)
  endif()

  add_executable(
    asterion_tests
    tests/unit/test_order_book.cpp
    tests/unit/test_event_log_replay.cpp
    tests/unit/test_risk_gateway.cpp
    tests/unit/test_telemetry_inference.cpp
    tests/unit/test_allocation_tracking.cpp
    tests/golden/test_golden_traces.cpp
    tests/property/test_book_properties.cpp)
  target_link_libraries(asterion_tests PRIVATE asterion asterion_allocation_tracker
                                               Catch2::Catch2WithMain)
  target_compile_definitions(asterion_tests PRIVATE ASTERION_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

  if(ASTERION_ENABLE_WARNINGS)
    if(MSVC)
      target_compile_options(asterion_tests PRIVATE /W4 /permissive-)
    else()
      target_compile_options(asterion_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
  endif()

  add_test(NAME asterion_tests COMMAND asterion_tests)
endif()

if(ASTERION_BUILD_BENCHMARKS)
  add_executable(asterion_benchmarks benchmarks/benchmark_main.cpp)
  target_link_libraries(asterion_benchmarks PRIVATE asterion asterion_allocation_tracker)
  target_compile_definitions(
    asterion_benchmarks
    PRIVATE ASTERION_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            ASTERION_GIT_COMMIT="${ASTERION_GIT_COMMIT}"
            ASTERION_BUILD_TYPE="${ASTERION_CONFIG_BUILD_TYPE}"
            ASTERION_COMPILER_FLAGS="${ASTERION_CONFIG_COMPILER_FLAGS}")

  if(ASTERION_USE_GOOGLE_BENCHMARK)
    find_package(benchmark QUIET)
    if(NOT benchmark_FOUND)
      set(BENCHMARK_ENABLE_TESTING
          OFF
          CACHE BOOL "" FORCE)
      set(BENCHMARK_ENABLE_INSTALL
          OFF
          CACHE BOOL "" FORCE)
      set(BENCHMARK_ENABLE_GTEST_TESTS
          OFF
          CACHE BOOL "" FORCE)
      FetchContent_Declare(
        googlebenchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.9.4.tar.gz)
      FetchContent_MakeAvailable(googlebenchmark)
    endif()

    add_executable(asterion_google_benchmarks benchmarks/google_benchmark_main.cpp)
    target_link_libraries(asterion_google_benchmarks PRIVATE asterion
                                                             asterion_allocation_tracker
                                                             benchmark::benchmark)
    target_compile_definitions(
      asterion_google_benchmarks PRIVATE ASTERION_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
endif()
