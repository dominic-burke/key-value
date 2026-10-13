#include <server.hpp>

#include <store.hpp>

#include <array>
#include <atomic>
#include <cerrno>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{
    bool check(bool condition, std::string_view message)
    {
        if (!condition)
            std::cerr << "FAILED: " << message << '\n';
        return condition;
    }

    auto send_all(int socket, std::string_view bytes) -> bool
    {
        std::size_t sent = 0;
        while (sent < bytes.size())
        {
            const auto result = send(socket, bytes.data() + sent, bytes.size() - sent, 0);
            if (result < 0)
            {
                if (errno == EINTR)
                    continue;
                return false;
            }
            sent += static_cast<std::size_t>(result);
        }
        return true;
    }

    auto read_to_end(int socket) -> std::string
    {
        std::array<char, 128> buffer{};
        std::string response;
        for (;;)
        {
            const auto count = recv(socket, buffer.data(), buffer.size(), 0);
            if (count == 0)
                return response;
            if (count < 0)
            {
                if (errno == EINTR)
                    continue;
                throw std::runtime_error("test client recv failed");
            }
            response.append(buffer.data(), static_cast<std::size_t>(count));
        }
    }

    bool test_commands_over_socket()
    {
        int sockets[2]{};
        if (!check(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
                   "create connected test sockets"))
        {
            return false;
        }

        HashStore store;
        std::thread server_thread{[&]
                                  {
                                      try
                                      {
                                          serve_client(sockets[1], store);
                                      }
                                      catch (const std::exception &error)
                                      {
                                          std::cerr << "Server handler failed: " << error.what() << '\n';
                                      }
                                      close(sockets[1]);
                                  }};

        const bool sent_first = send_all(sockets[0], "SET al");
        const bool sent_rest = send_all(
            sockets[0],
            "pha beta\nGET alpha\nGET missing\nDEL alpha\nGET alpha\nUNKNOWN x\n");
        shutdown(sockets[0], SHUT_WR);
        const std::string response = read_to_end(sockets[0]);
        close(sockets[0]);
        server_thread.join();

        const std::string expected =
            "OK\n"
            "VALUE beta\n"
            "NOT_FOUND\n"
            "OK\n"
            "NOT_FOUND\n"
            "ERR Unknown command on line 1\n";
        return check(sent_first && sent_rest, "send fragmented and complete requests") && check(response == expected,
                                                                                                "server parses commands and sends their responses");
    }

    bool test_oversized_line_does_not_break_connection()
    {
        int sockets[2]{};
        if (!check(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
                   "create sockets for oversized request test"))
        {
            return false;
        }

        HashStore store;
        std::thread server_thread{[&]
                                  {
                                      try
                                      {
                                          serve_client(sockets[1], store);
                                      }
                                      catch (const std::exception &error)
                                      {
                                          std::cerr << "Server handler failed: " << error.what() << '\n';
                                      }
                                      close(sockets[1]);
                                  }};

        const std::string oversized(MAX_COMMAND_LINE_SIZE + 20, 'x');
        const bool sent = send_all(sockets[0], oversized + "\nGET missing\n");
        shutdown(sockets[0], SHUT_WR);
        const std::string response = read_to_end(sockets[0]);
        close(sockets[0]);
        server_thread.join();

        return check(sent, "send oversized request and following valid request") && check(
                                                                                        response == "ERR Command line too long\nNOT_FOUND\n",
                                                                                        "rejects oversized line and recovers at next newline");
    }

    bool test_multiple_tcp_clients()
    {
        constexpr int client_count = 2048;
        const int listener = socket(AF_INET, SOCK_STREAM, 0);
        if (!check(listener != -1, "create TCP listener"))
            return false;

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (!check(bind(
                       listener,
                       reinterpret_cast<const sockaddr *>(&address),
                       sizeof(address))
                       == 0,
                   "bind TCP listener"))
        {
            close(listener);
            return false;
        }
        if (!check(listen(listener, client_count) == 0, "listen for TCP clients"))
        {
            close(listener);
            return false;
        }

        socklen_t address_size = sizeof(address);
        if (!check(getsockname(
                       listener,
                       reinterpret_cast<sockaddr *>(&address),
                       &address_size)
                       == 0,
                   "get TCP listener port"))
        {
            close(listener);
            return false;
        }

        ConcurrentStore store;
        std::atomic<bool> clients_passed{true};
        std::vector<std::thread> clients;
        clients.reserve(client_count);
        for (int index = 0; index < client_count; ++index)
        {
            clients.emplace_back(
                [&, index]
                {
                    const int client = socket(AF_INET, SOCK_STREAM, 0);
                    if (client == -1
                        || connect(
                               client,
                               reinterpret_cast<const sockaddr *>(&address),
                               sizeof(address))
                               != 0)
                    {
                        clients_passed = false;
                        if (client != -1)
                            close(client);
                        return;
                    }

                    const std::string key = "client-" + std::to_string(index);
                    const std::string value = "value-" + std::to_string(index);
                    const std::string request =
                        "SET " + key + " " + value + "\nGET " + key + "\n";
                    if (!send_all(client, request))
                        clients_passed = false;
                    shutdown(client, SHUT_WR);
                    const auto response = read_to_end(client);
                    if (response != "OK\nVALUE " + value + "\n")
                        clients_passed = false;
                    close(client);
                });
        }

        std::vector<std::thread> handlers;
        handlers.reserve(client_count);
        for (int index = 0; index < client_count; ++index)
        {
            const int client = accept(listener, nullptr, nullptr);
            if (!check(client != -1, "accept concurrent TCP client"))
            {
                clients_passed = false;
                break;
            }
            handlers.emplace_back(
                [client, &store]
                {
                    try
                    {
                        serve_client(client, store);
                    }
                    catch (...)
                    {
                    }
                    close(client);
                });
        }
        close(listener);

        for (auto &client : clients)
            client.join();
        for (auto &handler : handlers)
            handler.join();

        return check(
            clients_passed && store.size() == client_count,
            "multiple TCP clients are served concurrently");
    }
}

int main()
{
    return test_commands_over_socket()
                && test_oversized_line_does_not_break_connection()
                && test_multiple_tcp_clients()
               ? 0
               : 1;
}
