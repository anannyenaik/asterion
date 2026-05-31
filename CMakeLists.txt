cmake_minimum_required(VERSION 3.20)

project(
  asterion
  VERSION 0.1.0
  DESCRIPTION "Deterministic low-latency trading systems lab"
  LANGUAGES CXX)

option(ASTERION_BUILD_TESTS "Build Asterion tests" ON)
option(ASTERION_BUILD_BENCHMARKS "Build Asterion benchmark runner" ON)
option(ASTERION_BUILD_PYTHON "Build Python bindings" OFF)
option(ASTERION_USE_GOOGLE_BENCHMARK "Build optional Google Benchmark target" OFF)
option(ASTERION_USE_ONNXRUNTIME "Build with the optional ONNX Runtime inference backend" OFF)
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

if(ASTERION_BUILD_TESTS OR ASTERION_BUILD_PYTHON
   OR (ASTERION_BUILD_BENCHMARKS AND ASTERION_USE_GOOGLE_BENCHMARK))
  include(FetchContent)
endif()

add_library(
  asterion
  cpp/src/core/checksum.cpp
  cpp/src/core/clock.cpp
  cpp/src/core/hmac_sha256.cpp
  cpp/src/market_data/event.cpp
  cpp/src/market_data/event_log.cpp
  cpp/src/market_data/replay.cpp
  cpp/src/market_data/replay_aggregate.cpp
  cpp/src/market_data/synthetic_generator.cpp
  cpp/src/book/order_book.cpp
  cpp/src/book/pooled_order_book.cpp
  cpp/src/matching/execution_report.cpp
  cpp/src/matching/matching_engine.cpp
  cpp/src/risk/kill_switch.cpp
  cpp/src/risk/risk_audit.cpp
  cpp/src/risk/audit_manifest.cpp
  cpp/src/risk/portfolio_risk.cpp
  cpp/src/risk/risk_gateway.cpp
  cpp/src/session/broker_session.cpp
  cpp/src/strategy/imbalance_strategy.cpp
  cpp/src/strategy/market_maker.cpp
  cpp/src/inference/feature_extractor.cpp
  cpp/src/inference/inference.cpp
  cpp/src/inference/linear_model.cpp
  cpp/src/inference/torchscript_model.cpp
  cpp/src/inference/backend.cpp
  cpp/src/inference/onnx_model.cpp
  cpp/src/telemetry/latency_budget.cpp
  cpp/src/telemetry/latency_histogram.cpp
  cpp/src/telemetry/metrics.cpp)

target_include_directories(asterion PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpp/include)

# Optional real inference backend. When requested and ONNX Runtime is found, the
# library is built with the ONNX backend; otherwise the build still succeeds with
# the deterministic LinearModel fallback. The dependency is never required in CI.
#
# Two discovery paths are supported so the opt-in CI lane is reliable:
#   1. find_package(onnxruntime CONFIG) for distributions that ship a CMake package.
#   2. A manual header/library search rooted at ONNXRUNTIME_ROOT (or the environment
#      variable of the same name) for the official prebuilt archives, which do not
#      include a CMake config file.
if(ASTERION_USE_ONNXRUNTIME)
  find_package(onnxruntime CONFIG QUIET)
  if(onnxruntime_FOUND)
    target_compile_definitions(asterion PUBLIC ASTERION_HAVE_ONNXRUNTIME)
    target_link_libraries(asterion PUBLIC onnxruntime::onnxruntime)
    message(STATUS "Asterion: ONNX Runtime inference backend enabled (CMake package)")
  else()
    find_path(
      ASTERION_ONNXRUNTIME_INCLUDE_DIR
      NAMES onnxruntime_cxx_api.h
      HINTS ${ONNXRUNTIME_ROOT} $ENV{ONNXRUNTIME_ROOT}
      PATH_SUFFIXES include include/onnxruntime include/onnxruntime/core/session)
    find_library(
      ASTERION_ONNXRUNTIME_LIBRARY
      NAMES onnxruntime
      HINTS ${ONNXRUNTIME_ROOT} $ENV{ONNXRUNTIME_ROOT}
      PATH_SUFFIXES lib lib64)
    if(ASTERION_ONNXRUNTIME_INCLUDE_DIR AND ASTERION_ONNXRUNTIME_LIBRARY)
      target_compile_definitions(asterion PUBLIC ASTERION_HAVE_ONNXRUNTIME)
      target_include_directories(asterion PUBLIC ${ASTERION_ONNXRUNTIME_INCLUDE_DIR})
      target_link_libraries(asterion PUBLIC ${ASTERION_ONNXRUNTIME_LIBRARY})
      message(STATUS "Asterion: ONNX Runtime inference backend enabled (manual: "
                     "${ASTERION_ONNXRUNTIME_LIBRARY})")
    else()
      message(STATUS "Asterion: ONNX Runtime requested but not found; "
                     "using the deterministic LinearModel fallback")
    endif()
  endif()
endif()

if(ASTERION_BUILD_PYTHON)
  set_target_properties(asterion PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

add_library(asterion_allocation_tracker STATIC cpp/src/core/allocation_tracker.cpp)
target_include_directories(asterion_allocation_tracker
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpp/include)

add_executable(asterion_replay cpp/tools/replay_main.cpp)
target_link_libraries(asterion_replay PRIVATE asterion)

add_executable(asterion_latency_budget cpp/tools/latency_budget_main.cpp)
target_link_libraries(asterion_latency_budget PRIVATE asterion)
target_compile_definitions(asterion_latency_budget
                           PRIVATE ASTERION_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

if(ASTERION_ENABLE_WARNINGS)
  if(MSVC)
    target_compile_options(asterion PRIVATE /W4 /permissive-)
    target_compile_options(asterion_allocation_tracker PRIVATE /W4 /permissive-)
    target_compile_options(asterion_replay PRIVATE /W4 /permissive-)
    target_compile_options(asterion_latency_budget PRIVATE /W4 /permissive-)
  else()
    target_compile_options(asterion PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(asterion_allocation_tracker PRIVATE -Wall -Wextra -Wpedantic
                                                               -Wconversion -Wshadow)
    target_compile_options(asterion_replay PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(asterion_latency_budget PRIVATE -Wall -Wextra -Wpedantic -Wconversion
                                                           -Wshadow)
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
    target_compile_options(asterion_latency_budget PUBLIC -fsanitize=address,undefined
                                                          -fno-omit-frame-pointer)
    target_link_options(asterion_latency_budget PUBLIC -fsanitize=address,undefined)
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
    tests/unit/test_snapshot.cpp
    tests/unit/test_multi_symbol.cpp
    tests/unit/test_risk_gateway.cpp
    tests/unit/test_risk_audit.cpp
    tests/unit/test_risk_controls.cpp
    tests/unit/test_audit_manifest.cpp
    tests/unit/test_broker_session.cpp
    tests/unit/test_portfolio_risk.cpp
    tests/unit/test_latency_budget.cpp
    tests/unit/test_telemetry_inference.cpp
    tests/unit/test_inference_backend.cpp
    tests/unit/test_allocation_tracking.cpp
    tests/unit/test_pooled_order_book.cpp
    tests/unit/test_hot_path.cpp
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

  if(MINGW)
    # MinGW's dynamic libstdc++ keeps its own operator new/delete definitions in
    # the runtime DLL, which bypasses the allocation tracker replacement used by
    # the tests. Static libstdc++ keeps allocation accounting meaningful locally.
    target_link_options(asterion_tests PRIVATE -static-libstdc++ -static-libgcc)
  endif()

  add_test(NAME asterion_tests COMMAND asterion_tests)
endif()

if(ASTERION_BUILD_PYTHON)
  set(PYBIND11_FINDPYTHON
      ON
      CACHE BOOL "" FORCE)
  find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
  find_package(pybind11 CONFIG QUIET)
  if(NOT pybind11_FOUND)
    FetchContent_Declare(
      pybind11
      URL https://github.com/pybind/pybind11/archive/refs/tags/v2.13.6.tar.gz)
    FetchContent_MakeAvailable(pybind11)
  endif()

  pybind11_add_module(_native python/asterion/_native.cpp)
  target_link_libraries(_native PRIVATE asterion)
  target_include_directories(_native PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cpp/include)
  set_target_properties(_native PROPERTIES LIBRARY_OUTPUT_DIRECTORY
                                           ${CMAKE_CURRENT_BINARY_DIR}/python/asterion)

  add_custom_target(
    asterion_python_package ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/python/asterion
            ${CMAKE_CURRENT_BINARY_DIR}/python/asterion
    DEPENDS _native)
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

  if(MINGW)
    target_link_options(asterion_benchmarks PRIVATE -static-libstdc++ -static-libgcc)
    if(TARGET asterion_google_benchmarks)
      target_link_options(asterion_google_benchmarks PRIVATE -static-libstdc++ -static-libgcc)
    endif()
  endif()
endif()
