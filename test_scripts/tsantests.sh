cmake -S . -B build-tsan -DKV_ENABLE_TSAN_TESTS=ON
cmake --build build-tsan --target kv_server_tsan_tests
ctest --test-dir build-tsan -R '^kv_server_tsan_tests$' --output-on-failure