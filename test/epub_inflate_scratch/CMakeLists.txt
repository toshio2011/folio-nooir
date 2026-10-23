add_executable(EpubInflateScratchTest
  InflateScratchTest.cpp
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
)

target_include_directories(EpubInflateScratchTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/miniz/src
)

target_link_libraries(EpubInflateScratchTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(EpubInflateScratchTest)
