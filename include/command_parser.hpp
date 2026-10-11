#pragma once

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

struct SetCommand
{
    std::string key;
    std::string value;
};

struct GetCommand
{
    std::string key;
};

struct DelCommand
{
    std::string key;
};

using Command = std::variant<SetCommand, GetCommand, DelCommand>;

class CommandParseError : public std::invalid_argument
{
public:
    using std::invalid_argument::invalid_argument;
};

auto parse_commands(std::span<const std::byte> input) -> std::vector<Command>;
