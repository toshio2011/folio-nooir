add_executable(BleMemoryPolicyTest BleMemoryPolicyTest.cpp)

target_link_libraries(BleMemoryPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BleMemoryPolicyTest)
