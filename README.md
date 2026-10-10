# Key-value store

A small persistent-in-memory key-value store written in C++20.

## Requirements

- CMake 3.20 or newer
- A C++ compiler with C++20 support
- A build tool supported by CMake, such as Ninja or Make

## Configure

Run these commands from the project root:

```sh
cmake -S . -B build
```

`-S .` selects the project source directory and `-B build` keeps generated
files in a separate build directory. CMake detects the available compiler and
build system during configuration.

To choose a generator explicitly, pass `-G`. For example:

```sh
cmake -S . -B build -G Ninja
```

To configure a release build with a single-configuration generator (such as
Ninja or Unix Makefiles):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

For multi-configuration generators, such as Visual Studio, select the
configuration when building instead:

```sh
cmake -S . -B build
cmake --build build --config Release
```

Re-run the configure command with the same build directory to change options.
To start over with a fresh configuration, remove that build directory and
configure again.

## Build

Build all configured targets:

```sh
cmake --build build
```

Build a single target:

```sh
cmake --build build --target key-value
cmake --build build --target kv_store_tests
```

The project defines these targets:

- `kv_store`: the library implementation in `src/store.cpp`, with public
  headers from `include/` and the C++20 requirement.
- `key-value`: the demonstration executable in `main.cpp`, linked to
  `kv_store`.
- `kv_store_tests`: the test executable in `tests/store_tests.cpp`. It is
  included when `BUILD_TESTING` is enabled.

## Run

After building, run the `key-value` executable from the build directory. For
example, on Linux:

```sh
./build/key-value
```

The executable stores a sample value and prints it.

## Test with CTest

`include(CTest)` enables CTest and defines the `BUILD_TESTING` option, which is
on by default. Build and run the registered tests with:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

The current test is registered as `kv_store_tests`. Run only that test with:

```sh
ctest --test-dir build --output-on-failure -R kv_store_tests
```

To configure without building the test executable:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
```

CTest only runs tests that have been built and registered by the current
configuration. Reconfigure with `-DBUILD_TESTING=ON` (or omit the option) to
enable the test target again.

## Generated files

Keep generated files in the build directory rather than the project root.
Build directories such as `build/` are ignored by Git. CMake generates its
cache, build files, and CTest metadata there; remove the build directory when
you want to clean the build:

```sh
rm -rf build
```

This project currently defines no install, packaging, or CMake preset targets.
