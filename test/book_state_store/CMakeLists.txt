file(GLOB ARDUINOJSON_INCLUDE_CANDIDATES
  "${REPO_ROOT}/.pio/libdeps/*/ArduinoJson/src"
)
if(NOT ARDUINOJSON_INCLUDE_CANDIDATES)
  message(FATAL_ERROR "ArduinoJson is not installed yet; run the firmware dependency setup before host tests")
endif()
list(GET ARDUINOJSON_INCLUDE_CANDIDATES 0 ARDUINOJSON_INCLUDE_DIR)

add_executable(BookStateStoreTest
  BookStateStoreTest.cpp
  ${REPO_ROOT}/src/BookStateStore.cpp
  ${REPO_ROOT}/src/util/StatisticsDate.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
)
target_include_directories(BookStateStoreTest BEFORE PRIVATE
  ${REPO_ROOT}/test/recent_books_store/stubs
  ${ARDUINOJSON_INCLUDE_DIR}
  ${REPO_ROOT}/lib/Serialization
)
target_compile_definitions(BookStateStoreTest PRIVATE
  ARDUINOJSON_ENABLE_ARDUINO_STRING=1
  ARDUINOJSON_ENABLE_ARDUINO_STREAM=0
)
target_link_libraries(BookStateStoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookStateStoreTest)
