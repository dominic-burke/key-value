cmake_minimum_required(VERSION 3.20)
project(key_value LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

add_library(kv_store src/store.cpp)
target_include_directories(kv_store PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(kv_store PUBLIC cxx_std_20)

add_executable(key-value main.cpp)
target_link_libraries(key-value PRIVATE kv_store)

if(BUILD_TESTING)
    add_executable(kv_store_tests tests/store_tests.cpp)
    target_link_libraries(kv_store_tests PRIVATE kv_store)
    add_test(NAME kv_store_tests COMMAND kv_store_tests)
endif()
