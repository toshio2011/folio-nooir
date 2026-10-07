add_executable(EpubPageTurnQueueTest EpubPageTurnQueueTest.cpp)

target_link_libraries(EpubPageTurnQueueTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(EpubPageTurnQueueTest)
