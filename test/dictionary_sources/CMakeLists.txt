add_executable(DictionarySourcesTest
  DictionarySourcesTest.cpp
)

target_link_libraries(DictionarySourcesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DictionarySourcesTest)
