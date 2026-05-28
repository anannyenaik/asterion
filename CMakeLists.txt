cmake_minimum_required(VERSION 3.20)

project(
  asterion
  VERSION 0.1.0
  DESCRIPTION "Deterministic low-latency trading systems lab"
  LANGUAGES CXX)

option(ASTERION_BUILD_TESTS "Build Asterion tests" ON)
option(ASTERION_BUILD_BENCHMARKS "Build Asterion benchmark runner" ON)
option(ASTERION_ENABLE_WARNINGS "Enable strict compiler warnings" ON)
option(ASTERION_ENABLE_SANITIZERS "Enable Address/Undefined sanitizers" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(
  asterion
  cpp/src/core/checksum.cpp
  cpp/src/core/clock.cpp
  cpp/src/market_data/event.cpp
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

if(ASTERION_ENABLE_WARNINGS)
  if(MSVC)
    target_compile_options(asterion PRIVATE /W4 /permissive-)
  else()
    target_compile_options(asterion PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  endif()
endif()

if(ASTERION_ENABLE_SANITIZERS)
  if(NOT MSVC)
    target_compile_options(asterion PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(asterion PUBLIC -fsanitize=address,undefined)
  endif()
endif()

if(ASTERION_BUILD_TESTS)
  include(CTest)
  include(FetchContent)

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
    tests/unit/test_risk_gateway.cpp
    tests/unit/test_telemetry_inference.cpp
    tests/golden/test_golden_traces.cpp
    tests/property/test_book_properties.cpp)
  target_link_libraries(asterion_tests PRIVATE asterion Catch2::Catch2WithMain)

  if(ASTERION_ENABLE_WARNINGS)
    if(MSVC)
      target_compile_options(asterion_tests PRIVATE /W4 /permissive-)
    else()
      target_compile_options(asterion_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
  endif()

  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)
  catch_discover_tests(asterion_tests)
endif()

if(ASTERION_BUILD_BENCHMARKS)
  add_executable(asterion_benchmarks benchmarks/benchmark_main.cpp)
  target_link_libraries(asterion_benchmarks PRIVATE asterion)
  target_compile_definitions(asterion_benchmarks PRIVATE ASTERION_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()
