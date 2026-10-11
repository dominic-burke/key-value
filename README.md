# Key-value store

A small C++20 key-value store with in-memory and append-only file-backed
implementations.

## Store implementations

The public store interface is `Istore` in `include/store.hpp`. It provides:

- `set(key, value)` to insert or replace a value.
- `get(key)` to return an `std::optional<std::string>`.
- `erase(key)` to remove a key and report whether it existed.
- `size()` to return the number of stored keys.

Available implementations:

- `HashStore`: an in-memory store.
- `ConcurrentStore`: an in-memory store protected for concurrent access.
- `PersistentStore`: stores values in memory and appends changes to a log file.
  It flushes each appended record before applying the change to memory. On
  construction, it replays the log to restore the stored values.

Example:

```cpp
#include <persistent_store.hpp>

int main()
{
    PersistentStore store{"store.log"};
    store.set("color", "blue");

    if (const auto value = store.get("color"))
        return *value == "blue" ? 0 : 1;

    return 1;
}
```

The log format currently supports non-empty keys and non-empty `Set` values,
with a maximum size of 64 KiB for each. `Delete` records contain a key and no
value. Log records have a magic header, format version, opcode, lengths, and a
CRC-32 checksum trailer. Replay rejects invalid records and checksum mismatches;
an incomplete final record is truncated back to the end of the last verified
record so earlier records can be recovered safely. The checksum format is
version 2.

## Command parser

`include/command_parser.hpp` exposes `parse_commands`, which parses a byte
span into command values without executing them. Commands are newline-delimited
and use uppercase names with whitespace-separated, printable ASCII tokens:

```text
SET key value
GET key
DEL key
```

`SET` requires exactly a key and value; `GET` and `DEL` each require exactly
one key. Blank lines are ignored, and the final command does not need a
trailing newline. Tokens cannot contain spaces or quotes. Malformed input
throws `CommandParseError`.

For example:

```cpp
#include <command_parser.hpp>

#include <span>
#include <string_view>

constexpr std::string_view input = "SET color blue\nGET color\n";
const auto commands = parse_commands(
    std::as_bytes(std::span{input.data(), input.size()}));
```

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
build system during configuration. In-source builds are rejected.

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

## Build

Build all configured targets:

```sh
cmake --build build
```

The project defines these main targets:

- `kv_store`: in-memory store implementations.
- `kv_logging`: append-only log record serialization and replay.
- `kv_persistent_store`: the persistent store, linked with the store and
  logging libraries.
- `kv_command_parser`: newline-delimited command parsing from byte spans.
- `key-value`: a small demonstration executable using `PersistentStore`.
- `kv_store_tests`, `kv_logging_tests`, `kv_command_parser_tests`, and
  `kv_persistent_store_tests`: test executables, included when `BUILD_TESTING`
  is enabled.

Executables and libraries are written to the selected build directory. For
multi-configuration generators, CMake may place them in a configuration
subdirectory such as `build/Debug/`.

## Run

After building, run the `key-value` executable. For example, on Linux:

```sh
./build/key-value
```

The demonstration stores and prints a sample value using `PersistentStore`.
It creates or appends to `store.log` in the executable's working directory.

## Tests

`include(CTest)` enables CTest and sets `BUILD_TESTING` on by default. Build and
run the registered tests with:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests cover in-memory store behavior and concurrency, command parsing,
log record parsing (including malformed records and size boundaries), and a
persistent-store round trip that reopens the log and verifies replayed sets
and deletes. A separate two-process test exits immediately after flushing a
log record but before applying it in memory, then verifies that a new process
recovers that record from the log. The tests also check recovery and truncation
of an incomplete final record. Stream flushing covers process termination,
but does not guarantee persistence through power loss or an operating-system
crash.
Run a specific test by name, for example:

```sh
ctest --test-dir build --output-on-failure -R kv_persistent_store_tests
```

To configure without the test executables:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
```

CTest only runs tests that were built and registered by the current
configuration. Reconfigure with `-DBUILD_TESTING=ON` (or omit the option) to
enable the test targets again.

## Generated files

Keep generated files in the build directory rather than the project root.
Build directories such as `build/` are ignored by Git. CMake generates its
cache, build files, and CTest metadata there; remove the build directory when
you want a clean build:

```sh
rm -rf build
```

This project currently defines no install, packaging, or CMake preset targets.
