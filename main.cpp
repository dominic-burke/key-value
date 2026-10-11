#include <iostream>
#include <persistent_store.hpp>
#include <server.hpp>

int main()
{
    try
    {
        PersistentStore store{"store.log"};
        std::cout << "Listening for TCP commands on port "
                  << DEFAULT_SERVER_PORT << '\n';
        start_server(store);
    }
    catch (const std::exception &error)
    {
        std::cerr << "Server failed: " << error.what() << '\n';
        return 1;
    }
}