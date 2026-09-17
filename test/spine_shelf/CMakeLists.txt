add_executable(spine_shelf_test SpineShelfPlannerTest.cpp ../../src/util/SpineShelfPlanner.cpp)
target_link_libraries(spine_shelf_test PRIVATE GTest::gtest_main crosspoint_test_common)

include(GoogleTest)
gtest_discover_tests(spine_shelf_test)
