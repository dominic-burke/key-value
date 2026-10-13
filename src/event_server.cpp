#include <server.hpp>
#include <command_parser.hpp>
#include <store.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <format>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    class Socket
    {
    public:
        explicit Socket(int descriptor = -1) noexcept : descriptor_{descriptor} {}
        ~Socket() { if (descriptor_ != -1) close(descriptor_); }
        Socket(const Socket &) = delete;
        auto operator=(const Socket &) -> Socket & = delete;
        Socket(Socket &&other) noexcept
            : descriptor_{std::exchange(other.descriptor_, -1)} {}
        auto operator=(Socket &&other) noexcept -> Socket &
        {
            if (this != &other)
            {
                if (descriptor_ != -1) close(descriptor_);
                descriptor_ = std::exchange(other.descriptor_, -1);
            }
            return *this;
        }
        auto get() const noexcept -> int { return descriptor_; }
    private:
        int descriptor_;
    };

    void log_client_action(
        std::string_view client,
        std::string_view action,
        std::string_view key,
        std::string_view value = {})
    {
        static std::mutex log_mutex;
        const std::lock_guard lock{log_mutex};
        if (action == "SET")
            std::fprintf(stdout, "Client %.*s: SET %.*s = %.*s\n",
                         static_cast<int>(client.size()), client.data(),
                         static_cast<int>(key.size()), key.data(),
                         static_cast<int>(value.size()), value.data());
        else if (action == "GET")
            std::fprintf(stdout, "Client %.*s: GET %.*s -> %.*s\n",
                         static_cast<int>(client.size()), client.data(),
                         static_cast<int>(key.size()), key.data(),
                         static_cast<int>(value.size()), value.data());
        else
            std::fprintf(stdout, "Client %.*s: DEL %.*s\n",
                         static_cast<int>(client.size()), client.data(),
                         static_cast<int>(key.size()), key.data());
        std::fflush(stdout);
    }

    auto execute_commands(
        std::string_view line,
        Istore &store,
        std::string_view client) -> std::string
    {
        try
        {
            const auto *bytes = reinterpret_cast<const std::byte *>(line.data());
            const auto commands = parse_commands(
                std::span<const std::byte>{bytes, line.size()});
            std::string response;
            for (const auto &command : commands)
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
                            log_client_action(client, "GET", parsed.key,
                                              value ? *value : "NOT_FOUND");
                            return value ? "VALUE " + *value + "\n" : "NOT_FOUND\n";
                        }
                        else
                        {
                            const bool erased = store.erase(parsed.key);
                            log_client_action(client, "DEL", parsed.key);
                            return erased ? "OK\n" : "NOT_FOUND\n";
                        }
                    }, command);
            return response;
        }
        catch (const CommandParseError &error)
        {
            return std::format("ERR {}\n", error.what());
        }
    }

    struct WorkerJob
    {
        std::uint64_t connection_id;
        std::string line;
        std::string client;
    };

    struct WorkerResult
    {
        std::uint64_t connection_id;
        std::string response;
    };

    class WorkerPool
    {
    public:
        WorkerPool(Istore &store, int completion_fd)
            : store_{store}, completion_fd_{completion_fd}
        {
            const auto worker_count = std::clamp(
                std::thread::hardware_concurrency(), 1u, 8u);
            workers_.reserve(worker_count);
            for (unsigned int i = 0; i < worker_count; ++i)
                workers_.emplace_back(&WorkerPool::run, this);
        }

        ~WorkerPool()
        {
            {
                const std::lock_guard lock{mutex_};
                stopping_ = true;
            }
            condition_.notify_all();
            for (auto &worker : workers_)
                worker.join();
        }

        WorkerPool(const WorkerPool &) = delete;
        auto operator=(const WorkerPool &) -> WorkerPool & = delete;

        auto submit(WorkerJob job) -> bool
        {
            const std::lock_guard lock{mutex_};
            if (stopping_ || jobs_.size() >= MAX_QUEUED_JOBS)
                return false;
            jobs_.push_back(std::move(job));
            condition_.notify_one();
            return true;
        }

        auto take_results() -> std::vector<WorkerResult>
        {
            const std::lock_guard lock{results_mutex_};
            std::vector<WorkerResult> results;
            results.reserve(results_.size());
            while (!results_.empty())
            {
                results.push_back(std::move(results_.front()));
                results_.pop_front();
            }
            return results;
        }

    private:
        static constexpr std::size_t MAX_QUEUED_JOBS = 1024;

        void run()
        {
            for (;;)
            {
                WorkerJob job;
                {
                    std::unique_lock lock{mutex_};
                    condition_.wait(lock, [this] {
                        return stopping_ || !jobs_.empty();
                    });
                    if (stopping_ && jobs_.empty())
                        return;
                    job = std::move(jobs_.front());
                    jobs_.pop_front();
                }

                std::string response;
                try
                {
                    response = execute_commands(job.line, store_, job.client);
                }
                catch (const std::exception &error)
                {
                    std::fprintf(stderr, "Command execution failed: %s\n", error.what());
                    response = "ERR Internal server error\n";
                }
                catch (...)
                {
                    std::fprintf(stderr, "Command execution failed: unknown error\n");
                    response = "ERR Internal server error\n";
                }
                {
                    const std::lock_guard lock{results_mutex_};
                    results_.push_back({job.connection_id, std::move(response)});
                }
                notify_event_loop();
            }
        }

        void notify_event_loop() const noexcept
        {
            constexpr std::uint64_t notification = 1;
            for (;;)
            {
                if (write(completion_fd_, &notification, sizeof(notification)) == 0)
                    return;
                if (errno == EINTR)
                    continue;
                if (errno == EAGAIN)
                    return;
                std::fprintf(stderr, "Worker notification failed: %s\n", std::strerror(errno));
                return;
            }
        }

        Istore &store_;
        int completion_fd_;
        std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<WorkerJob> jobs_;
        bool stopping_ = false;
        std::vector<std::thread> workers_;
        std::mutex results_mutex_;
        std::deque<WorkerResult> results_;
    };

    struct Client
    {
        int socket;
        std::uint64_t id;
        std::string address;
        std::string input;
        std::deque<std::string> commands;
        std::string output;
        bool discard_oversized_line = false;
        bool in_flight = false;
        bool peer_closed = false;
        bool close_after_write = false;
    };

    auto client_details(int client_socket) -> std::string
    {
        sockaddr_storage address{};
        socklen_t address_size = sizeof(address);
        if (getpeername(
                client_socket,
                reinterpret_cast<sockaddr *>(&address),
                &address_size)
            == -1)
            throw std::runtime_error(
                std::format("getpeername failed: {}", std::strerror(errno)));
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

    void log_client_served()
    {
        static std::mutex log_mutex;
        const std::lock_guard lock{log_mutex};
        std::fprintf(stdout, "Client served\n");
        std::fflush(stdout);
    }
}

void run_event_loop(Istore &store, std::uint16_t port)
{
    const std::string service = std::to_string(port);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo *raw_addresses = nullptr;
    const int lookup_result = getaddrinfo(nullptr, service.c_str(), &hints, &raw_addresses);
    if (lookup_result != 0)
        throw std::runtime_error(
            std::format("getaddrinfo failed: {}", gai_strerror(lookup_result)));
    const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses{
        raw_addresses, freeaddrinfo};

    Socket listener;
    for (auto *address = addresses.get(); address != nullptr; address = address->ai_next)
    {
        Socket candidate{socket(
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol)};
        if (candidate.get() == -1)
            continue;

        int reuse_address = 1;
        if (setsockopt(
                candidate.get(),
                SOL_SOCKET,
                SO_REUSEADDR,
                &reuse_address,
                sizeof(reuse_address)) == -1)
        {
            throw std::runtime_error(
                std::format("setsockopt failed: {}", std::strerror(errno)));
        }

        if (bind(candidate.get(), address->ai_addr, address->ai_addrlen) == 0)
        {
            listener = std::move(candidate);
            break;
        }
    }

    if (listener.get() == -1)
        throw std::runtime_error("Could not bind TCP listening socket");
    if (listen(listener.get(), 64) == -1)
        throw std::runtime_error(
            std::format("listen failed: {}", std::strerror(errno)));

    const auto set_nonblocking = [](int descriptor)
    {
        const int flags = fcntl(descriptor, F_GETFL, 0);
        if (flags == -1 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == -1)
            throw std::runtime_error(
                std::format("fcntl failed: {}", std::strerror(errno)));
    };
    set_nonblocking(listener.get());

    Socket completion_event{
        eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)};
    if (completion_event.get() == -1)
        throw std::runtime_error(
            std::format("eventfd failed: {}", std::strerror(errno)));

    Socket epoll_descriptor{epoll_create1(EPOLL_CLOEXEC)};
    if (epoll_descriptor.get() == -1)
        throw std::runtime_error(
            std::format("epoll_create1 failed: {}", std::strerror(errno)));

    auto add_to_epoll = [](int epoll_fd, int descriptor, std::uint32_t events)
    {
        epoll_event event{};
        event.events = events;
        event.data.fd = descriptor;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, descriptor, &event) == -1)
            throw std::runtime_error(
                std::format("epoll_ctl add failed: {}", std::strerror(errno)));
    };
    add_to_epoll(epoll_descriptor.get(), listener.get(), EPOLLIN);
    add_to_epoll(epoll_descriptor.get(), completion_event.get(), EPOLLIN);


    std::uint64_t next_connection_id = 1;
    std::unordered_map<int, Client> clients;
    std::unordered_map<std::uint64_t, int> socket_by_connection;
    WorkerPool workers{store, completion_event.get()};
    std::array<epoll_event, 64> events{};
    const auto update_client_interest =
        [&epoll_descriptor](const Client &client)
    {
        epoll_event interest{};
        interest.events = EPOLLIN | (client.output.empty() ? 0 : EPOLLOUT);
        interest.data.fd = client.socket;
        if (epoll_ctl(
                epoll_descriptor.get(),
                EPOLL_CTL_MOD,
                client.socket,
                &interest) == -1)
            throw std::runtime_error(
                std::format("epoll_ctl modify failed: {}", std::strerror(errno)));
    };

    for (;;)
    {
        const int event_count = epoll_wait(
            epoll_descriptor.get(), events.data(), events.size(), -1);
        if (event_count == -1)
        {
            if (errno == EINTR)
                continue;
            throw std::runtime_error(
                std::format("epoll_wait failed: {}", std::strerror(errno)));
        }

        for (int event_index = 0; event_index < event_count; ++event_index)
        {
            const auto event = events[static_cast<std::size_t>(event_index)];
            if (event.data.fd == completion_event.get())
            {
                std::uint64_t notifications = 0;
                while (read(
                           completion_event.get(),
                           &notifications,
                           sizeof(notifications))
                       == sizeof(notifications))
                {
                }
                for (auto result : workers.take_results())
                {
                    const auto socket_it =
                        socket_by_connection.find(result.connection_id);
                    if (socket_it == socket_by_connection.end())
                        continue;
                    auto client_it = clients.find(socket_it->second);
                    if (client_it == clients.end())
                        continue;
                    auto &client = client_it->second;
                    client.output += std::move(result.response);
                    client.in_flight = false;
                    if (!client.commands.empty())
                    {
                        auto command = std::move(client.commands.front());
                        client.commands.pop_front();
                        if (!workers.submit(
                                WorkerJob{client.id, std::move(command), client.address}))
                        {
                            client.output += "ERR Server busy\n";
                            client.in_flight = false;
                        }
                        else
                            client.in_flight = true;
                    }
                    else if (client.peer_closed)
                        client.close_after_write = true;
                    update_client_interest(client);
                }
            }
            else if (event.data.fd == listener.get())
            {
                for (;;)
                {
                    const int client_socket = accept(listener.get(), nullptr, nullptr);
                    if (client_socket == -1)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        if (errno == EINTR)
                            continue;
                        throw std::runtime_error(
                            std::format("accept failed: {}", std::strerror(errno)));
                    }
                    try
                    {
                        set_nonblocking(client_socket);
                        Client client{
                            client_socket,
                            next_connection_id++,
                            client_details(client_socket)};
                        const auto connection_id = client.id;
                        add_to_epoll(epoll_descriptor.get(), client_socket, EPOLLIN);
                        clients.emplace(client_socket, std::move(client));
                        socket_by_connection.emplace(connection_id, client_socket);
                    }
                    catch (...)
                    {
                        close(client_socket);
                        throw;
                    }
                }
            }
            else
            {
                auto client_it = clients.find(event.data.fd);
                if (client_it == clients.end())
                    continue;
                auto &client = client_it->second;
                bool close_client = (event.events & (EPOLLERR | EPOLLHUP)) != 0;

                if ((event.events & EPOLLIN) != 0 && !close_client)
                {
                    std::array<char, 4096> received{};
                    for (;;)
                    {
                        const auto count = recv(
                            client.socket, received.data(), received.size(), 0);
                        if (count == 0)
                        {
                            client.peer_closed = true;
                            if (!client.input.empty() && !client.discard_oversized_line)
                            {
                                client.output +=
                                    "ERR Incomplete command: missing newline\n";
                                client.input.clear();
                            }
                            if (!client.in_flight && client.commands.empty())
                                client.close_after_write = true;
                            break;
                        }
                        if (count < 0)
                        {
                            if (errno == EINTR)
                                continue;
                            if (errno == EAGAIN || errno == EWOULDBLOCK)
                                break;
                            close_client = true;
                            break;
                        }
                        for (std::size_t i = 0;
                             i < static_cast<std::size_t>(count);
                             ++i)
                        {
                            const char byte = received[i];
                            if (client.discard_oversized_line)
                            {
                                if (byte == '\n')
                                    client.discard_oversized_line = false;
                                continue;
                            }
                            if (byte == '\n')
                            {
                                if (client.in_flight)
                                {
                                    if (client.commands.size() >= 128)
                                        client.output += "ERR Server busy\n";
                                    else
                                        client.commands.push_back(std::move(client.input));
                                }
                                else if (!workers.submit(WorkerJob{
                                             client.id,
                                             std::move(client.input),
                                             client.address}))
                                    client.output += "ERR Server busy\n";
                                else
                                    client.in_flight = true;
                                client.input.clear();
                                continue;
                            }
                            if (client.input.size() == MAX_COMMAND_LINE_SIZE)
                            {
                                client.output += "ERR Command line too long\n";
                                client.input.clear();
                                client.discard_oversized_line = true;
                                continue;
                            }
                            client.input.push_back(byte);
                        }
                    }
                }

                if ((event.events & EPOLLOUT) != 0 && !close_client)
                {
                    while (!client.output.empty())
                    {
#ifdef MSG_NOSIGNAL
                        constexpr int flags = MSG_NOSIGNAL;
#else
                        constexpr int flags = 0;
#endif
                        const auto sent = send(
                            client.socket,
                            client.output.data(),
                            client.output.size(),
                            flags);
                        if (sent > 0)
                            client.output.erase(0, static_cast<std::size_t>(sent));
                        else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                            break;
                        else if (sent < 0 && errno == EINTR)
                            continue;
                        else
                        {
                            close_client = true;
                            break;
                        }
                    }
                }

                if (close_client || (client.close_after_write && client.output.empty()))
                {
                    epoll_ctl(
                        epoll_descriptor.get(),
                        EPOLL_CTL_DEL,
                        client.socket,
                        nullptr);
                    close(client.socket);
                    log_client_served();
                    socket_by_connection.erase(client.id);
                    clients.erase(client_it);
                    continue;
                }

                update_client_interest(client);
            }
        }
    }
}
