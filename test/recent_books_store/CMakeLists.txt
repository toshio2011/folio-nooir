file(GLOB ARDUINOJSON_INCLUDE_CANDIDATES
  "${REPO_ROOT}/.pio/libdeps/*/ArduinoJson/src"
)
if(NOT ARDUINOJSON_INCLUDE_CANDIDATES)
  message(FATAL_ERROR "ArduinoJson is not installed yet; run the firmware dependency setup before host tests")
endif()
list(GET ARDUINOJSON_INCLUDE_CANDIDATES 0 ARDUINOJSON_INCLUDE_DIR)

add_executable(RecentBooksStoreTest
  RecentBooksStoreTest.cpp
  ${REPO_ROOT}/src/RecentBooksStore.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
)
target_include_directories(RecentBooksStoreTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${ARDUINOJSON_INCLUDE_DIR}
  ${REPO_ROOT}/lib/Serialization
)
target_compile_definitions(RecentBooksStoreTest PRIVATE
  ARDUINOJSON_ENABLE_ARDUINO_STRING=1
  ARDUINOJSON_ENABLE_ARDUINO_STREAM=0
  RECENT_BOOKS_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/truncated_recent.json"
)
target_link_libraries(RecentBooksStoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RecentBooksStoreTest)
