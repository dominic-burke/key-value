#include <store.hpp>

#include <iostream>
#include <string_view>

namespace {
bool check(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}
}

int main()
{
    kv::Store store;

    if (!check(store.size() == 0, "new store is empty")
        || !check(!store.get("missing").has_value(), "missing key has no value")) {
        return 1;
    }

    store.set("color", "blue");
    if (!check(store.get("color") == "blue", "set stores a value")
        || !check(store.size() == 1, "inserting a key updates the size")) {
        return 1;
    }

    store.set("color", "green");
    if (!check(store.get("color") == "green", "set replaces an existing value")
        || !check(store.size() == 1, "replacing a value does not change the size")) {
        return 1;
    }

    if (!check(store.erase("color"), "erase reports success for an existing key")
        || !check(!store.get("color").has_value(), "erased key is absent")
        || !check(store.size() == 0, "erasing a key updates the size")
        || !check(!store.erase("color"), "erase reports failure for a missing key")) {
        return 1;
    }

    return 0;
}
