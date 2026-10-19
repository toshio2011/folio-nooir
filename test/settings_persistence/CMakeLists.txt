file(GLOB ARDUINOJSON_INCLUDE_CANDIDATES
  "${REPO_ROOT}/.pio/libdeps/*/ArduinoJson/src"
)
if(NOT ARDUINOJSON_INCLUDE_CANDIDATES)
  message(FATAL_ERROR "ArduinoJson is not installed yet; run the firmware dependency setup before host tests")
endif()
list(GET ARDUINOJSON_INCLUDE_CANDIDATES 0 ARDUINOJSON_INCLUDE_DIR)

add_executable(SettingsPersistenceTest
  SettingsPersistenceTest.cpp
  ${REPO_ROOT}/lib/Serialization/PersistableStore.cpp
)
target_include_directories(SettingsPersistenceTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${ARDUINOJSON_INCLUDE_DIR}
)
target_compile_definitions(SettingsPersistenceTest PRIVATE
  ARDUINOJSON_ENABLE_ARDUINO_STRING=1
  ARDUINOJSON_ENABLE_ARDUINO_STREAM=0
)
target_link_libraries(SettingsPersistenceTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SettingsPersistenceTest)
