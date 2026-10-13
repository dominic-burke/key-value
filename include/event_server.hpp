#pragma once

#include <cstdint>

class Istore;

void run_event_loop(Istore &store, std::uint16_t port);
