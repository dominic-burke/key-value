#pragma once

#include <cstddef>
#include <cstdint>

class Istore;

inline constexpr std::uint16_t DEFAULT_SERVER_PORT = 3490;
inline constexpr std::size_t MAX_COMMAND_LINE_SIZE = 1024;

void serve_client(int client_socket, Istore &store);
void start_server(Istore &store, std::uint16_t port = DEFAULT_SERVER_PORT);
