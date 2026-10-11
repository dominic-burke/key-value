#include <iostream>
#include <persistent_store.hpp>

int main()
{
    PersistentStore store{"store.log"};

    store.set("color", "blue");

    const auto value = store.get("color");

    if (value)
    {
        std::cout << "Value for color: " << *value << '\n';
    }
    else
    {
        std::cout << "Key not found\n";
    }
}