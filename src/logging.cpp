#include <logging.hpp>

#include <format>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <store.hpp>

CorruptLog::CorruptLog()
    : reason_{"No reason provided"}
{
}

CorruptLog::CorruptLog(std::string reason)
    : reason_{std::move(reason)}
{
}

auto parse_opcode(std::uint8_t val) -> std::optional<OpCode>
{
    switch (val)
    {
    case 1U:
        return OpCode::Set;
    case 2U:
        return OpCode::Delete;
    default:
        return std::nullopt;
    }
    return std::nullopt;
}

auto ingest_byte(std::istream &in) -> std::uint8_t
{
    const int value = in.get();
    if (value == EOF)
        throw TruncatedRecord{};
    return static_cast<std::uint8_t>(value);
}

void write_u32(std::ostream &out, std::uint32_t value)
{
    std::array<unsigned char, 4> bytes{
        static_cast<unsigned char>((value >> 24) & 0xff),
        static_cast<unsigned char>((value >> 16) & 0xff),
        static_cast<unsigned char>((value >> 8) & 0xff),
        static_cast<unsigned char>(value & 0xff),
    };

    out.write(
        reinterpret_cast<const char *>(bytes.data()),
        bytes.size());
}

auto read_u32(std::istream &in) -> std::uint32_t
{
    std::array<unsigned char, 4> bytes{};

    in.read(
        reinterpret_cast<char *>(bytes.data()),
        bytes.size());

    if (!in)
        throw std::runtime_error("Failed to read u32");

    return (
        static_cast<std::uint32_t>(bytes[0] << 24) |
        static_cast<std::uint32_t>(bytes[1] << 16) |
        static_cast<std::uint32_t>(bytes[2] << 8) |
        static_cast<std::uint32_t>(bytes[3]));
}

void write_record(std::ostream &out, const LogRecord &record)
{
    out.write(MAGIC.data(), MAGIC.size());

    out.put(
        static_cast<char>(VERSION));

    out.put(
        static_cast<char>(record.opcode));

    write_u32(
        out,
        static_cast<std::uint32_t>(
            record.key.size()));

    write_u32(
        out,
        static_cast<std::uint32_t>(
            record.value.size()));

    out.write(
        record.key.data(),
        record.key.size());

    out.write(
        record.value.data(),
        record.value.size());

    if (!out)
        throw std::runtime_error("Failed to write log record");

    return;
}

auto read_record(std::istream &in) -> std::optional<LogRecord>
{
    std::array<char, 4> magic{};

    in.read(magic.data(), magic.size());

    if (in.eof() && in.gcount() == 0)
        return std::nullopt;

    if (in.gcount() != 4)
        throw TruncatedRecord{};

    if (magic != MAGIC)
        throw CorruptLog{
            "Invalid record magic"};

    int version = ingest_byte(in);
    if (static_cast<std::uint8_t>(version) != VERSION)
        throw CorruptLog{std::string{"Unsupported log version"}};

    int raw_opcode = ingest_byte(in);
    auto opcode = parse_opcode(raw_opcode);
    // check std::nullopt
    if (!opcode)
        throw CorruptLog{std::format("Opcode not in list: {}", raw_opcode)};

    std::uint32_t key_size = read_u32(in);
    std::uint32_t value_size = read_u32(in);

    if (key_size > MAX_KEY_SIZE)
        throw CorruptLog{std::format("Key size too large: {}", key_size)};
    if (value_size > MAX_VALUE_SIZE)
        throw CorruptLog{std::format("Value size too large: {}", value_size)};

    if (key_size == 0)
        throw CorruptLog{std::string{"Key size equal to 0"}};

    if (*opcode == OpCode::Set && value_size == 0)
        throw CorruptLog{std::string{"Set record has 0 value size"}};

    if (*opcode == OpCode::Delete && value_size != 0)
        throw CorruptLog{std::string{"Delete record does not have 0 value size"}};

    std::string key(key_size, '\0');
    if (!key.empty())
        in.read(key.data(), static_cast<std::streamsize>(key.size()));
    if (!in)
        throw TruncatedRecord{};

    std::string value(value_size, '\0');
    if (!value.empty())
        in.read(value.data(), static_cast<std::streamsize>(value.size()));
    if (!in)
        throw TruncatedRecord{};

    return LogRecord{
        .opcode = *opcode,
        .key = key,
        .value = value};
}

AppendOnlyLog::AppendOnlyLog(std::filesystem::path path)
    : out_(path, std::ios::binary | std::ios::app), path_{std::move(path)}
{
    if (!out_)
        throw std::runtime_error("AppendOnlyLog failed to open its file path");
}

void AppendOnlyLog::append_set(std::string_view key, std::string_view value)
{
    if (value.length() == 0)
        throw std::runtime_error("Attempt to write 0-length value");
    write_record(out_, LogRecord{OpCode::Set, std::string{key}, std::string{value}});
}

void AppendOnlyLog::append_delete(std::string_view key)
{
    write_record(out_, LogRecord{OpCode::Delete, std::string{key}});
}

void AppendOnlyLog::replay(Istore &store)
{
    out_.flush();
    if (!out_) throw std::runtime_error("Failed to flush out_");

    std::ifstream in(path_, std::ios::binary);
    if (!in) return;

    for (;;)
    {
        auto record = read_record(in);

        if (!record.has_value())
        {
            break;
        }

        switch (record->opcode)
        {
            case OpCode::Set:
                store.set(
                    record->key,
                    record->value
                );
                break;
            case OpCode::Delete:
                store.erase(
                    record->key
                );
                break;
        }
    }
}