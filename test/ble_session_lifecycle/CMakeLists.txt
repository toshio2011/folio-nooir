add_executable(BleSessionLifecycleTest BleSessionLifecycleTest.cpp)

target_include_directories(BleSessionLifecycleTest PRIVATE
  ${REPO_ROOT}/freeink-sdk/libs/network/BleKeyboardHost/include
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

target_link_libraries(BleSessionLifecycleTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BleSessionLifecycleTest)
