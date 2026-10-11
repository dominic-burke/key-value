cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Configure with: cmake -S . -B build")
endif()

project(key_value LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_library(kv_store src/store.cpp)
add_library(kv_logging src/logging.cpp)
target_include_directories(kv_store PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(kv_store PUBLIC cxx_std_20)

add_executable(key-value main.cpp)
target_link_libraries(key-value PRIVATE kv_store)

if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_executable(kv_store_tests tests/store_tests.cpp)
    target_link_libraries(kv_store_tests PRIVATE kv_store Threads::Threads)
    add_test(NAME kv_store_tests COMMAND kv_store_tests)
endif()
