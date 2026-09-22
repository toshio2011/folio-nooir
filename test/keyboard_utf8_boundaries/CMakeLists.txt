add_executable(KeyboardUtf8BoundaryTest
  KeyboardUtf8BoundaryTest.cpp
)

target_include_directories(KeyboardUtf8BoundaryTest PRIVATE
  ${REPO_ROOT}/src/activities/util
)

target_link_libraries(KeyboardUtf8BoundaryTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KeyboardUtf8BoundaryTest)
