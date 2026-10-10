#include <iostream>
#include <store.hpp>

int main(void) {
    kv::HashStore store{};

    store.set("key1", "a1");

    auto value = store.get("key1");

    if (value) {
        std::cout << "Value for key1: " << *value << std::endl;
    }
    else {
        std::cout << "Key not found" << std::endl;
    }
}