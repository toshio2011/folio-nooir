add_executable(font_selection_compatibility_test FontSelectionCompatibilityTest.cpp SdCardFontRegistryHost.cpp)
target_include_directories(font_selection_compatibility_test PRIVATE
                           ${CMAKE_SOURCE_DIR}/../lib/EpdFont
                           ${CMAKE_SOURCE_DIR}/../lib/hal
                           ${CMAKE_SOURCE_DIR}/../lib/Logging)
target_link_libraries(font_selection_compatibility_test PRIVATE GTest::gtest_main crosspoint_test_common)

gtest_discover_tests(font_selection_compatibility_test)
