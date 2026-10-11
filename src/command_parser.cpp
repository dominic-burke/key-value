#include <command_parser.hpp>

#include <optional>
#include <string_view>
#include <utility>

namespace
{
    auto is_separator(char value) -> bool
    {
        return value == ' ' || value == '\t' || value == '\r';
    }

    auto parse_line(std::string_view line, std::size_t line_number) -> std::optional<Command>
    {
        std::vector<std::string_view> fields;
        std::size_t position = 0;

        while (position < line.size())
        {
            while (position < line.size() && is_separator(line[position]))
                ++position;
            if (position == line.size())
                break;

            const std::size_t start = position;
            while (position < line.size() && !is_separator(line[position]))
            {
                const auto byte = static_cast<unsigned char>(line[position]);
                if (byte < 0x21 || byte > 0x7e)
                    throw CommandParseError(
                        "Invalid non-printable byte on line "
                        + std::to_string(line_number));
                ++position;
            }
            fields.push_back(line.substr(start, position - start));
        }

        if (fields.empty())
            return std::nullopt;

        const auto fail = [line_number](std::string_view reason) {
            throw CommandParseError(
                std::string{reason} + " on line " + std::to_string(line_number));
        };

        if (fields[0] == "SET")
        {
            if (fields.size() != 3)
                fail("SET requires a key and value");
            return SetCommand{std::string{fields[1]}, std::string{fields[2]}};
        }
        if (fields[0] == "GET")
        {
            if (fields.size() != 2)
                fail("GET requires a key");
            return GetCommand{std::string{fields[1]}};
        }
        if (fields[0] == "DEL")
        {
            if (fields.size() != 2)
                fail("DEL requires a key");
            return DelCommand{std::string{fields[1]}};
        }

        fail("Unknown command");
        return std::nullopt;
    }
}

auto parse_commands(std::span<const std::byte> input) -> std::vector<Command>
{
    std::vector<Command> commands;
    if (input.empty())
        return commands;

    const auto *data = reinterpret_cast<const char *>(input.data());
    const std::string_view text{data, input.size()};
    std::size_t line_number = 1;
    std::size_t line_start = 0;

    while (line_start < text.size())
    {
        const auto newline = text.find('\n', line_start);
        const auto line_end = newline == std::string_view::npos
            ? text.size()
            : newline;
        if (auto command = parse_line(
                text.substr(line_start, line_end - line_start),
                line_number))
        {
            commands.push_back(std::move(*command));
        }

        if (newline == std::string_view::npos)
            break;
        line_start = newline + 1;
        ++line_number;
    }

    return commands;
}
