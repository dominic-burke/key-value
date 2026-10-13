#include <server.hpp>
#include <event_server.hpp>

#include <command_parser.hpp>
#include <store.hpp>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <type_traits>
#include <utility>
#include <variant>

namespace
{
    class Socket
    {
    public:
        explicit Socket(int descriptor = -1) noexcept
            : descriptor_{descriptor}
        {
        }

        ~Socket()
        {
            if (descriptor_ != -1)
                close(descriptor_);
        }

        Socket(const Socket &) = delete;
        auto operator=(const Socket &) -> Socket & = delete;

        Socket(Socket &&other) noexcept
            : descriptor_{std::exchange(other.descriptor_, -1)}
        {
        }

        auto operator=(Socket &&other) noexcept -> Socket &
        {
            if (this != &other)
            {
                if (descriptor_ != -1)
                    close(descriptor_);
                descriptor_ = std::exchange(other.descriptor_, -1);
            }
            return *this;
        }

        auto get() const noexcept -> int
        {
            return descriptor_;
        }

    private:
        int descriptor_;
    };

    void send_all(int socket, std::string_view response)
    {
        std::size_t sent = 0;
        while (sent < response.size())
        {
#ifdef MSG_NOSIGNAL
            constexpr int flags = MSG_NOSIGNAL;
#else
            constexpr int flags = 0;
#endif
            const auto result = send(
                socket,
                response.data() + sent,
                response.size() - sent,
                flags);
            if (result == -1)
            {
                if (errno == EINTR)
                    continue;
                throw std::runtime_error(
                    std::format("send failed: {}", std::strerror(errno)));
            }
            if (result == 0)
                throw std::runtime_error("send returned zero bytes");
            sent += static_cast<std::size_t>(result);
        }
    }

    auto client_details(int client_socket) -> std::string
    {
        sockaddr_storage address{};
        socklen_t address_size = sizeof(address);
        if (getpeername(
                client_socket,
                reinterpret_cast<sockaddr *>(&address),
                &address_size) == -1)
        {
            throw std::runtime_error(
                std::format("getpeername failed: {}", std::strerror(errno)));
        }
        if (address.ss_family == AF_UNIX)
            return "local";

        char host[NI_MAXHOST]{};
        char service[NI_MAXSERV]{};
        const int result = getnameinfo(
            reinterpret_cast<const sockaddr *>(&address),
            address_size,
            host,
            sizeof(host),
            service,
            sizeof(service),
            NI_NUMERICHOST | NI_NUMERICSERV);
        if (result != 0)
            throw std::runtime_error(
                std::format("getnameinfo failed: {}", gai_strerror(result)));
        return std::format("{}:{}", host, service);
    }

    void log_client_action(
        std::string_view client,
        std::string_view action,
        std::string_view key,
        std::string_view value = {})
    {
        static std::mutex log_mutex;
        const std::lock_guard lock{log_mutex};
        if (action == "SET")
        {
            std::fprintf(
                stdout,
                "Client %.*s: SET %.*s = %.*s\n",
                static_cast<int>(client.size()),
                client.data(),
                static_cast<int>(key.size()),
                key.data(),
                static_cast<int>(value.size()),
                value.data());
        }
        else if (action == "GET")
        {
            std::fprintf(
                stdout,
                "Client %.*s: GET %.*s -> %.*s\n",
                static_cast<int>(client.size()),
                client.data(),
                static_cast<int>(key.size()),
                key.data(),
                static_cast<int>(value.size()),
                value.data());
        }
        else
        {
            std::fprintf(
                stdout,
                "Client %.*s: DEL %.*s\n",
                static_cast<int>(client.size()),
                client.data(),
                static_cast<int>(key.size()),
                key.data());
        }
        std::fflush(stdout);
    }

    auto execute_commands(
        std::string_view line,
        Istore &store,
        std::string_view client)
    {
        try
        {
            const auto *bytes = reinterpret_cast<const std::byte *>(line.data());
            const auto commands = parse_commands(
                std::span<const std::byte>{bytes, line.size()});
            std::string response;
            for (const auto &command : commands)
            {
                response += std::visit(
                    [&store, client](const auto &parsed) -> std::string
                    {
                        using T = std::decay_t<decltype(parsed)>;
                        if constexpr (std::is_same_v<T, SetCommand>)
                        {
                            store.set(parsed.key, parsed.value);
                            log_client_action(client, "SET", parsed.key, parsed.value);
                            return "OK\n";
                        }
                        else if constexpr (std::is_same_v<T, GetCommand>)
                        {
                            const auto value = store.get(parsed.key);
                            log_client_action(
                                client,
                                "GET",
                                parsed.key,
                                value ? *value : "NOT_FOUND");
                            return value ? "VALUE " + *value + "\n" : "NOT_FOUND\n";
                        }
                        else
                        {
                            const bool erased = store.erase(parsed.key);
                            log_client_action(client, "DEL", parsed.key);
                            return erased ? "OK\n" : "NOT_FOUND\n";
                        }
                    },
                    command);
            }
            return response;
        }
        catch (const CommandParseError &error)
        {
            return std::format("ERR {}\n", error.what());
        }
    }

    void execute_line(
        std::string_view line,
        Istore &store,
        int client_socket,
        std::string_view client)
    {
        send_all(client_socket, execute_commands(line, store, client));
    }

}

void serve_client(int client_socket, Istore &store)
{
    const std::string client = client_details(client_socket);
    std::array<char, 4096> received{};
    std::string pending;
    bool discard_oversized_line = false;

    for (;;)
    {
        const auto count = recv(client_socket, received.data(), received.size(), 0);
        if (count == 0)
        {
            if (!pending.empty() && !discard_oversized_line)
                send_all(client_socket, "ERR Incomplete command: missing newline\n");
            return;
        }
        if (count < 0)
        {
            if (errno == EINTR)
                continue;
            throw std::runtime_error(
                std::format("recv failed: {}", std::strerror(errno)));
        }

        for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i)
        {
            const char byte = received[i];
            if (discard_oversized_line)
            {
                if (byte == '\n')
                    discard_oversized_line = false;
                continue;
            }

            if (byte == '\n')
            {
                execute_line(pending, store, client_socket, client);
                pending.clear();
                continue;
            }

            if (pending.size() == MAX_COMMAND_LINE_SIZE)
            {
                send_all(client_socket, "ERR Command line too long\n");
                pending.clear();
                discard_oversized_line = true;
                continue;
            }
            pending.push_back(byte);
        }
    }
}


void start_server(Istore &store, std::uint16_t port)
{
    run_event_loop(store, port);
}
