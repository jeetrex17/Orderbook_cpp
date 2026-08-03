cmake_minimum_required(VERSION 3.24)

project(orderbook_cpp
  VERSION 0.1.0
  DESCRIPTION "A limit-order matching engine"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(ORDERBOOK_WERROR      "Treat compiler warnings as errors" OFF)
option(ORDERBOOK_BUILD_TESTS "Build the test suite"              ON)
option(ORDERBOOK_BUILD_BENCH "Build the benchmark"               ON)
option(ORDERBOOK_HARDENED    "Enable standard-library hardening" OFF)
set(ORDERBOOK_SANITIZERS "" CACHE STRING
  "Semicolon-separated sanitizers to enable, e.g. address;undefined")

# ---------------------------------------------------------------------------
# The engine itself: header-only, so an INTERFACE library is the whole target.
# ---------------------------------------------------------------------------
add_library(orderbook INTERFACE)
add_library(orderbook::orderbook ALIAS orderbook)
target_include_directories(orderbook INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(orderbook INTERFACE cxx_std_20)

# The naive reference implementation, used by both the benchmark and the
# differential tests.
add_library(orderbook_baseline INTERFACE)
target_include_directories(orderbook_baseline INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/benchmark>)
target_link_libraries(orderbook_baseline INTERFACE orderbook)

# ---------------------------------------------------------------------------
# Warnings and instrumentation, carried as an INTERFACE target so they apply to
# our code only -- never to FetchContent'd dependencies.
# ---------------------------------------------------------------------------
add_library(orderbook_flags INTERFACE)

target_compile_options(orderbook_flags INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual
  -Wdouble-promotion -Wformat=2)

if(ORDERBOOK_WERROR)
  target_compile_options(orderbook_flags INTERFACE -Werror)
endif()

# Standard-library hardening turns silent container UB (bad iterators,
# out-of-range indexing) into a hard trap. Complements the sanitizers rather
# than duplicating them: this catches library-level misuse, ASan catches
# memory-level misuse.
if(ORDERBOOK_HARDENED)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_FLAGS "-std=c++20 -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG")
  check_cxx_source_compiles("
    #include <vector>
    #if !defined(_LIBCPP_VERSION)
    #error not libc++
    #endif
    int main() { std::vector<int> v{1}; return v[0] - 1; }"
    ORDERBOOK_HAS_LIBCPP_HARDENING)
  unset(CMAKE_REQUIRED_FLAGS)

  if(ORDERBOOK_HAS_LIBCPP_HARDENING)
    target_compile_definitions(orderbook_flags INTERFACE
      _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG)
    message(STATUS "Hardening: libc++ debug mode")
  else()
    # libstdc++ equivalent.
    target_compile_definitions(orderbook_flags INTERFACE _GLIBCXX_ASSERTIONS)
    message(STATUS "Hardening: libstdc++ assertions")
  endif()
endif()

if(ORDERBOOK_SANITIZERS)
  list(JOIN ORDERBOOK_SANITIZERS "," _san_list)
  message(STATUS "Sanitizers: ${_san_list}")
  # -fno-sanitize-recover makes UBSan findings abort instead of printing and
  # carrying on, which is the only way CI can fail on them.
  target_compile_options(orderbook_flags INTERFACE
    -fsanitize=${_san_list} -fno-sanitize-recover=all
    -fno-omit-frame-pointer -g)
  target_link_options(orderbook_flags INTERFACE -fsanitize=${_san_list})
endif()

# ---------------------------------------------------------------------------
# Executables
# ---------------------------------------------------------------------------
add_executable(orderbook_demo main.cpp)
target_link_libraries(orderbook_demo PRIVATE orderbook orderbook_flags)

if(ORDERBOOK_BUILD_BENCH)
  add_executable(orderbook_bench benchmark/Benchmark.cpp)
  target_link_libraries(orderbook_bench PRIVATE orderbook orderbook_baseline orderbook_flags)
endif()

if(ORDERBOOK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
