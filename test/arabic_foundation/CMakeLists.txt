add_executable(ArabicFoundationTest
  ArabicFoundationTest.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(ArabicFoundationTest PRIVATE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Utf8
  ${CMAKE_CURRENT_SOURCE_DIR}/../minibidi_arabic/stubs
)

target_link_libraries(ArabicFoundationTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ArabicFoundationTest)
