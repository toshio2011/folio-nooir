add_executable(StatisticsDateTest
  StatisticsDateTest.cpp
  ${REPO_ROOT}/src/util/StatisticsDate.cpp
)

target_link_libraries(StatisticsDateTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(StatisticsDateTest)
