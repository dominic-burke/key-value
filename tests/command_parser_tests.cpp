#include <command_parser.hpp>

#include <iostream>
#include <string>
#include <string_view>

namespace
{
    bool check(bool condition, std::string_view message)
    {
        if (!condition)
            std::cerr << "FAILED: " << message << '\n';
        return condition;
    }

    template <typename Function>
    bool throws_parse_error(Function &&function)
    {
        try
        {
            function();
        }
        catch (const CommandParseError &)
        {
            return true;
        }
        catch (...)
        {
        }
        return false;
    }

    auto parse(std::string_view text) -> std::vector<Command>
    {
        const auto *bytes = reinterpret_cast<const std::byte *>(text.data());
        return parse_commands(std::span<const std::byte>{bytes, text.size()});
    }

    bool test_parses_all_commands()
    {
        const auto commands = parse(
            "SET color blue\n"
            "GET color\n"
            "DEL color");
        if (!check(commands.size() == 3, "parses multiple newline-delimited commands"))
            return false;

        const auto *set = std::get_if<SetCommand>(&commands[0]);
        const auto *get = std::get_if<GetCommand>(&commands[1]);
        const auto *del = std::get_if<DelCommand>(&commands[2]);
        return check(set && set->key == "color" && set->value == "blue",
                     "parses SET key and value")
            && check(get && get->key == "color", "parses GET key")
            && check(del && del->key == "color", "parses DEL key");
    }

    bool test_handles_whitespace_and_blank_lines()
    {
        const auto commands = parse(" \t\r\nSET\tkey\tvalue\r\n\nGET key\n");
        return check(commands.size() == 2, "ignores blank lines")
            && check(std::get<SetCommand>(commands[0]).value == "value",
                     "accepts tabs and CRLF line endings");
    }

    bool test_rejects_bad_command_shapes()
    {
        return check(throws_parse_error([] { parse("SET key\n"); }),
                     "rejects SET without value")
            && check(throws_parse_error([] { parse("GET key extra\n"); }),
                     "rejects extra GET field")
            && check(throws_parse_error([] { parse("DEL\n"); }),
                     "rejects DEL without key")
            && check(throws_parse_error([] { parse("SET key value extra\n"); }),
                     "rejects extra SET field")
            && check(throws_parse_error([] { parse("UNKNOWN key\n"); }),
                     "rejects unknown command")
            && check(throws_parse_error([] { parse("get key\n"); }),
                     "rejects lowercase command");
    }

    bool test_rejects_control_bytes()
    {
        const std::string text{"GET key\0extra\n", 14};
        return check(throws_parse_error([&] { parse(text); }),
                     "rejects embedded control bytes");
    }
}

int main()
{
    return test_parses_all_commands()
        && test_handles_whitespace_and_blank_lines()
        && test_rejects_bad_command_shapes()
        && test_rejects_control_bytes()
        ? 0
        : 1;
}
