cmake_minimum_required(VERSION 3.16)
project(crosspoint_reader_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(crosspoint_test_common INTERFACE)
target_include_directories(crosspoint_test_common INTERFACE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/src
)
if(MSVC)
  target_compile_options(crosspoint_test_common INTERFACE /W4)
else()
  target_compile_options(crosspoint_test_common INTERFACE
    -Wall
    -Wextra
    -pedantic
  )
endif()

enable_testing()
include(GoogleTest)

add_subdirectory(streaming_json_parser)
add_subdirectory(release_json_parser)
add_subdirectory(differential_rounding)
add_subdirectory(hyphenation_eval)
add_subdirectory(utf8_compose)
add_subdirectory(opds_filename)
add_subdirectory(opds_torture)
add_subdirectory(epub_parser)
add_subdirectory(epub_inflate_scratch)
add_subdirectory(css_typography)
add_subdirectory(css_cascade)
add_subdirectory(minibidi_arabic)
add_subdirectory(arabic_foundation)
add_subdirectory(combining_marks)
add_subdirectory(gfx_line_clipping)
add_subdirectory(gfx_bitmap_scaling)
add_subdirectory(gfx_space_advance)
add_subdirectory(carousel_geometry)
add_subdirectory(ligature_guard)
add_subdirectory(dictionary_sources)
add_subdirectory(spine_shelf)
add_subdirectory(book_format)
add_subdirectory(statistics_date)
add_subdirectory(keyboard_utf8_boundaries)
add_subdirectory(font_selection_compatibility)
add_subdirectory(section_cache)
add_subdirectory(epub_page_turn_queue)
add_subdirectory(quick_actions)
add_subdirectory(status_progress)
add_subdirectory(ble_memory_policy)
add_subdirectory(ble_session_lifecycle)
add_subdirectory(xtc_render_memory)
add_subdirectory(xtc_thumbnail_stream)
add_subdirectory(xtc_metadata)
add_subdirectory(koreader_sync_pages)
add_subdirectory(ble_input_mapping)
add_subdirectory(home_snapshot_policy)
add_subdirectory(home_theme_policy)
add_subdirectory(folio_lite_layout)
add_subdirectory(thumbnail_miss_memo)
add_subdirectory(book_rating_geometry)
add_subdirectory(sleep_composer)
add_subdirectory(random_sleep_pool)
add_subdirectory(web_clippings_api)
add_subdirectory(web_stability)
add_subdirectory(settings_persistence)
add_subdirectory(recent_books_store)
add_subdirectory(recent_books_view_policy)
add_subdirectory(progress_file)
add_subdirectory(book_cache_utils)
