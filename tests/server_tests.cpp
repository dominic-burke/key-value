#include <server.hpp>

#include <store.hpp>

#include <array>
#include <cerrno>
#include <iostream>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

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
}

int main()
{
    return test_commands_over_socket() && test_oversized_line_does_not_break_connection()
               ? 0
               : 1;
}
