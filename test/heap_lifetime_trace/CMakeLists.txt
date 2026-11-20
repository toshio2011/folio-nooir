add_executable(HeapLifetimeTraceTest HeapLifetimeTraceTest.cpp)

target_link_libraries(HeapLifetimeTraceTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HeapLifetimeTraceTest)
