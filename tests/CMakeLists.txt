include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
  GIT_SHALLOW    TRUE
  SYSTEM)
FetchContent_MakeAvailable(googletest)

# RapidCheck publishes no releases, so pin a commit. It gives us automatic
# shrinking: a failing 200-operation sequence gets reduced to the smallest one
# that still fails, which is the whole reason to prefer it over a fuzz loop.
set(RC_ENABLE_GTEST ON CACHE BOOL "" FORCE)
FetchContent_Declare(rapidcheck
  GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
  GIT_TAG        ff6af6fc683159deb51c543b065eba14dfcf329b
  SYSTEM)
FetchContent_MakeAvailable(rapidcheck)

add_executable(orderbook_tests
  test_matching.cpp
  test_cancel.cpp
  test_id_index.cpp
  test_feed_replay.cpp
  prop_invariants.cpp
  prop_differential.cpp)

target_link_libraries(orderbook_tests PRIVATE
  orderbook
  orderbook_baseline
  orderbook_flags
  GTest::gtest_main
  rapidcheck
  rapidcheck_gtest)

include(GoogleTest)
gtest_discover_tests(orderbook_tests
  DISCOVERY_TIMEOUT 30
  PROPERTIES TIMEOUT 300)
