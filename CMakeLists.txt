cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Configure with: cmake -S . -B build")
endif()

project(key_value LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
option(KV_ENABLE_TSAN_TESTS "Build and register ThreadSanitizer tests" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_library(kv_store src/store.cpp)
add_library(kv_logging src/logging.cpp)
add_library(kv_persistent_store src/persistent_store.cpp)
add_library(kv_command_parser src/command_parser.cpp)
add_library(kv_server src/server.cpp src/event_server.cpp)
find_package(Threads REQUIRED)
target_include_directories(kv_store PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(kv_logging PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(kv_persistent_store PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(kv_command_parser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(kv_server PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(kv_store PUBLIC cxx_std_20)
target_compile_features(kv_logging PUBLIC cxx_std_20)
target_compile_features(kv_persistent_store PUBLIC cxx_std_20)
target_compile_features(kv_command_parser PUBLIC cxx_std_20)
target_compile_features(kv_server PUBLIC cxx_std_20)
target_link_libraries(kv_persistent_store PUBLIC kv_store kv_logging)
target_link_libraries(kv_server PUBLIC kv_command_parser Threads::Threads)

add_executable(key-value main.cpp)
target_link_libraries(key-value PRIVATE kv_persistent_store kv_server)

if(BUILD_TESTING)
    add_executable(kv_store_tests tests/store_tests.cpp)
    target_link_libraries(kv_store_tests PRIVATE kv_store Threads::Threads)
    add_test(NAME kv_store_tests COMMAND kv_store_tests)

    add_executable(kv_logging_tests tests/logging_tests.cpp)
    target_link_libraries(kv_logging_tests PRIVATE kv_logging)
    add_test(NAME kv_logging_tests COMMAND kv_logging_tests)

    add_executable(kv_command_parser_tests tests/command_parser_tests.cpp)
    target_link_libraries(kv_command_parser_tests PRIVATE kv_command_parser)
    add_test(NAME kv_command_parser_tests COMMAND kv_command_parser_tests)

    add_executable(kv_server_tests tests/server_tests.cpp)
    target_link_libraries(kv_server_tests PRIVATE kv_server kv_store Threads::Threads)
    add_test(NAME kv_server_tests COMMAND kv_server_tests)

    if(KV_ENABLE_TSAN_TESTS)
        set(KV_TSAN_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
        foreach(target kv_store kv_command_parser kv_server)
            target_compile_options(${target} PRIVATE ${KV_TSAN_FLAGS})
        endforeach()

        add_executable(kv_server_tsan_tests tests/server_tests.cpp)
        target_compile_options(kv_server_tsan_tests PRIVATE ${KV_TSAN_FLAGS})
        target_link_options(kv_server_tsan_tests PRIVATE ${KV_TSAN_FLAGS})
        target_link_libraries(
            kv_server_tsan_tests
            PRIVATE
                kv_server
                kv_store
                Threads::Threads)
        add_test(NAME kv_server_tsan_tests COMMAND kv_server_tsan_tests)
    endif()

    add_executable(kv_persistent_store_tests tests/persistent_store_tests.cpp)
    target_link_libraries(kv_persistent_store_tests PRIVATE kv_persistent_store)
    add_test(NAME kv_persistent_store_tests COMMAND kv_persistent_store_tests)

    set(PERSISTENT_STORE_CRASH_LOG
        "${CMAKE_CURRENT_BINARY_DIR}/persistent_store_crash_recovery.log")
    add_test(
        NAME kv_persistent_store_crash_write
        COMMAND kv_persistent_store_tests
            --crash-after-log-append "${PERSISTENT_STORE_CRASH_LOG}")
    set_tests_properties(kv_persistent_store_crash_write PROPERTIES
        FIXTURES_SETUP persistent_store_crash_recovery)
    add_test(
        NAME kv_persistent_store_crash_recovery
        COMMAND kv_persistent_store_tests
            --verify-crash-recovery "${PERSISTENT_STORE_CRASH_LOG}")
    set_tests_properties(kv_persistent_store_crash_recovery PROPERTIES
        FIXTURES_REQUIRED persistent_store_crash_recovery)
endif()
