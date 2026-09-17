add_executable(CombiningMarkAnchorTest
  CombiningMarkAnchorTest.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(CombiningMarkAnchorTest PRIVATE
  ${REPO_ROOT}/lib/EpdFont
)

target_link_libraries(CombiningMarkAnchorTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CombiningMarkAnchorTest)
