add_library(OpdsTortureExpat STATIC
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)
target_include_directories(OpdsTortureExpat PRIVATE
  ${REPO_ROOT}/lib/expat
)
target_compile_definitions(OpdsTortureExpat PUBLIC
  XML_GE=0
  XML_CONTEXT_BYTES=1024
  XML_STATIC
)

add_executable(OpdsTortureTest
  OpdsHttpDownloaderTest.cpp
  OpdsParserTest.cpp
  OpdsUrlTest.cpp
  ${REPO_ROOT}/lib/OpdsParser/OpdsParser.cpp
  ${REPO_ROOT}/src/network/HttpDownloader.cpp
  ${REPO_ROOT}/src/util/OpdsFilename.cpp
  ${REPO_ROOT}/src/util/StringUtils.cpp
  ${REPO_ROOT}/src/util/UrlUtils.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(OpdsTortureTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(OpdsTortureTest PRIVATE
  ${REPO_ROOT}/lib/OpdsParser
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/src/network
  ${REPO_ROOT}/src/util
  ${REPO_ROOT}/lib/Utf8
)
target_compile_definitions(OpdsTortureTest PRIVATE
  FREEINK_NET_WOLFSSL=1
  XML_GE=0
  XML_CONTEXT_BYTES=1024
  USE_UTF8_LONG_NAMES=1
  CROSSPOINT_VERSION="host-test"
)
target_link_libraries(OpdsTortureTest PRIVATE
  OpdsTortureExpat
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OpdsTortureTest)
