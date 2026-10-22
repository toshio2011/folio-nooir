add_executable(section_cache_validation_test
  SectionCacheValidationTest.cpp
  CbzPageCacheRecoveryTest.cpp
  ${CMAKE_SOURCE_DIR}/../src/util/CbzPageCache.cpp
)
target_include_directories(section_cache_validation_test BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
target_include_directories(section_cache_validation_test PRIVATE ${CMAKE_SOURCE_DIR}/../lib/Epub)
target_link_libraries(section_cache_validation_test PRIVATE GTest::gtest_main crosspoint_test_common)

gtest_discover_tests(section_cache_validation_test)
