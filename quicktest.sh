#!/usr/bin/env sh
set -eu

cmake --build build
ctest --test-dir build --output-on-failure