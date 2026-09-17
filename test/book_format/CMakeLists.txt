add_executable(BookFormatTest BookFormatTest.cpp)

target_link_libraries(BookFormatTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookFormatTest)
