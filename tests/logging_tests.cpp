#include <logging.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    bool check(bool condition, std::string_view message)
    {
        if (!condition)
            std::cerr << "FAILED: " << message << '\n';
        return condition;
    }

    template <typename Exception, typename Function>
    bool throws(Function &&function)
    {
        try
        {
            std::forward<Function>(function)();
        }
        catch (const Exception &)
        {
            return true;
        }
        catch (...)
        {
        }
        return false;
    }

    template <typename Function>
    bool run_test(std::string_view name, Function &&function)
    {
        try
        {
            return function();
        }
        catch (const std::exception &error)
        {
            std::cerr << "FAILED: " << name << " threw unexpectedly: "
                      << error.what() << '\n';
        }
        catch (...)
        {
            std::cerr << "FAILED: " << name << " threw an unknown exception\n";
        }
        return false;
    }

    auto make_record_bytes(
        std::uint8_t opcode,
        std::uint32_t key_size,
        std::uint32_t value_size,
        std::string_view key = {},
        std::string_view value = {}) -> std::string
    {
        std::ostringstream out{std::ios::out | std::ios::binary};
        out.write(MAGIC.data(), static_cast<std::streamsize>(MAGIC.size()));
        out.put(static_cast<char>(VERSION));
        out.put(static_cast<char>(opcode));
        write_u32(out, key_size);
        write_u32(out, value_size);
        out.write(key.data(), static_cast<std::streamsize>(key.size()));
        out.write(value.data(), static_cast<std::streamsize>(value.size()));
        return out.str();
    }

    auto read_bytes(const std::string &bytes) -> std::optional<LogRecord>
    {
        std::istringstream in{bytes, std::ios::in | std::ios::binary};
        return read_record(in);
    }

    bool test_empty_key()
    {
        const auto bytes = make_record_bytes(
            static_cast<std::uint8_t>(OpCode::Set), 0, 1, {}, "v");
        return check(
            throws<CorruptLog>([&] { read_bytes(bytes); }),
            "empty key is rejected");
    }

    bool test_empty_value()
    {
        const auto bytes = make_record_bytes(
            static_cast<std::uint8_t>(OpCode::Set), 1, 0, "k");
        return check(
            throws<CorruptLog>([&] { read_bytes(bytes); }),
            "empty set value is rejected");
    }

    bool test_maximum_key()
    {
        const std::string key(MAX_KEY_SIZE, 'k');
        const auto bytes = make_record_bytes(
            static_cast<std::uint8_t>(OpCode::Set),
            MAX_KEY_SIZE,
            1,
            key,
            "v");
        const auto record = read_bytes(bytes);
        return check(
            record && record->key == key && record->value == "v",
            "maximum allowed key is accepted");
    }

    bool test_maximum_value()
    {
        const std::string value(MAX_VALUE_SIZE, 'v');
        const auto bytes = make_record_bytes(
            static_cast<std::uint8_t>(OpCode::Set),
            1,
            MAX_VALUE_SIZE,
            "k",
            value);
        const auto record = read_bytes(bytes);
        return check(
            record && record->key == "k" && record->value == value,
            "maximum allowed value is accepted");
    }

    bool test_delete_record()
    {
        const auto bytes = make_record_bytes(
            static_cast<std::uint8_t>(OpCode::Delete), 1, 0, "k");
        const auto record = read_bytes(bytes);
        return check(
            record && record->opcode == OpCode::Delete
                && record->key == "k" && record->value.empty(),
            "delete record with empty value is accepted");
    }

    bool test_unknown_opcode()
    {
        const auto bytes = make_record_bytes(0x7f, 1, 1, "k", "v");
        return check(
            throws<CorruptLog>([&] { read_bytes(bytes); }),
            "unknown opcode is rejected");
    }

    bool test_bad_magic()
    {
        auto bytes = make_record_bytes(
            static_cast<std::uint8_t>(OpCode::Set), 1, 1, "k", "v");
        bytes[0] = 'X';
        return check(
            throws<CorruptLog>([&] { read_bytes(bytes); }),
            "bad magic is rejected");
    }

    bool test_unsupported_version()
    {
        auto bytes = make_record_bytes(
            static_cast<std::uint8_t>(OpCode::Set), 1, 1, "k", "v");
        bytes[4] = static_cast<char>(VERSION + 1);
        return check(
            throws<CorruptLog>([&] { read_bytes(bytes); }),
            "unsupported version is rejected");
    }

    bool test_oversized_key_length()
    {
        const auto bytes = make_record_bytes(
            static_cast<std::uint8_t>(OpCode::Set), MAX_KEY_SIZE + 1, 1);
        return check(
            throws<CorruptLog>([&] { read_bytes(bytes); }),
            "oversized key length is rejected");
    }

    bool test_oversized_value_length()
    {
        const auto bytes = make_record_bytes(
            static_cast<std::uint8_t>(OpCode::Set), 1, MAX_VALUE_SIZE + 1);
        return check(
            throws<CorruptLog>([&] { read_bytes(bytes); }),
            "oversized value length is rejected");
    }
}

int main()
{
    bool passed = true;
    passed &= run_test("empty key", test_empty_key);
    passed &= run_test("empty value", test_empty_value);
    passed &= run_test("maximum key", test_maximum_key);
    passed &= run_test("maximum value", test_maximum_value);
    passed &= run_test("delete record", test_delete_record);
    passed &= run_test("unknown opcode", test_unknown_opcode);
    passed &= run_test("bad magic", test_bad_magic);
    passed &= run_test("unsupported version", test_unsupported_version);
    passed &= run_test("oversized key length", test_oversized_key_length);
    passed &= run_test("oversized value length", test_oversized_value_length);
    return passed ? 0 : 1;
}
