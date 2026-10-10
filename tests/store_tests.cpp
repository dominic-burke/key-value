#include <store.hpp>

#include <iostream>
#include <vector>
#include <thread>
#include <string_view>

namespace
{
    bool check(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            return false;
        }
        return true;
    }
}

int test_basic(void)
{
    kv::HashStore store{};

    if (!check(store.size() == 0, "new store is empty") || !check(!store.get("missing").has_value(), "missing key has no value"))
    {
        return 1;
    }

    store.set("color", "blue");
    if (!check(store.get("color") == "blue", "set stores a value") || !check(store.size() == 1, "inserting a key updates the size"))
    {
        return 1;
    }

    store.set("color", "green");
    if (!check(store.get("color") == "green", "set replaces an existing value") || !check(store.size() == 1, "replacing a value does not change the size"))
    {
        return 1;
    }

    if (!check(store.erase("color"), "erase reports success for an existing key") || !check(!store.get("color").has_value(), "erased key is absent") || !check(store.size() == 0, "erasing a key updates the size") || !check(!store.erase("color"), "erase reports failure for a missing key"))
    {
        return 1;
    }

    return 0;
}

int concurrent_storage_test(void)
{
    std::vector<std::thread> threads{};
    kv::ConcurrentStore store{};

    for (auto i = 0; i < 8; i++)
    {
        threads.emplace_back([&, i]
                             {
            for (auto j = 0; j < 10000; j++) {
                store.set(std::to_string(i) + ":" + std::to_string(j), "value");
            } });
    }

    for (auto &thread : threads)
    {
        thread.join();
    }

    if (!check(store.size() == 80000, "concurrent writes preserve every entry"))
    {
        return 1;
    }

    if (!check(store.get("0:0") == "value", "concurrent store can read written values")
        || !check(store.get("7:9999") == "value", "concurrent store retains the final written value"))
    {
        return 1;
    }

    return 0;
}

int main(void)
{
    if (test_basic() != 0)
        return 1;

    if (concurrent_storage_test() != 0)
        return 1;

    return 0;
}
