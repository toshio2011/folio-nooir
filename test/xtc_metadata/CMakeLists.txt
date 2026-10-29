add_executable(XtcMetadataTest
  XtcMetadataTest.cpp
  ${REPO_ROOT}/lib/Xtc/XtcMetadataCache.cpp
  ${REPO_ROOT}/lib/Xtc/Xtc/XtcParser.cpp
)
target_include_directories(XtcMetadataTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Xtc
)
target_link_libraries(XtcMetadataTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(XtcMetadataTest)
