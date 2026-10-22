cmake_minimum_required(VERSION 3.16)
project(koreader_sync_pages_test CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(KOReaderSyncPagesTest KOReaderSyncPagesTest.cpp)
target_include_directories(KOReaderSyncPagesTest BEFORE PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
  "${REPO_ROOT}"
  "${REPO_ROOT}/lib"
  "${REPO_ROOT}/lib/KOReaderSync"
  "${REPO_ROOT}/src"
)
