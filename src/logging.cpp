#include <logging.hpp>

#include <format>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <store.hpp>

namespace
{
    auto crc32(std::string_view bytes) -> std::uint32_t
    {
        std::uint32_t crc = 0xffffffffU;
        for (const unsigned char byte : bytes)
        {
            crc ^= byte;
            for (int bit = 0; bit < 8; ++bit)
            {
                const std::uint32_t mask = 0U - (crc & 1U);
                crc = (crc >> 1) ^ (0xedb88320U & mask);
            }
        }
        return crc ^ 0xffffffffU;
    }

    void flush_log(std::ofstream &out)
    {
        out.flush();
        if (!out)
            throw std::runtime_error("Failed to flush log record");
    }
}

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
        throw TruncatedRecord{};

    return (
        static_cast<std::uint32_t>(bytes[0] << 24) |
        static_cast<std::uint32_t>(bytes[1] << 16) |
        static_cast<std::uint32_t>(bytes[2] << 8) |
        static_cast<std::uint32_t>(bytes[3]));
}

void write_record(std::ostream &out, const LogRecord &record)
{
    std::ostringstream body{std::ios::out | std::ios::binary};
    body.write(MAGIC.data(), static_cast<std::streamsize>(MAGIC.size()));

    body.put(static_cast<char>(VERSION));

    body.put(static_cast<char>(record.opcode));

    write_u32(
        body,
        static_cast<std::uint32_t>(record.key.size()));

    write_u32(
        body,
        static_cast<std::uint32_t>(record.value.size()));

    body.write(
        record.key.data(),
        static_cast<std::streamsize>(record.key.size()));

    body.write(
        record.value.data(),
        static_cast<std::streamsize>(record.value.size()));

    if (!body)
        throw std::runtime_error("Failed to serialize log record");

    const std::string bytes = body.str();
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    write_u32(out, crc32(bytes));

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

    std::ostringstream checksum_data{std::ios::out | std::ios::binary};
    checksum_data.write(magic.data(), static_cast<std::streamsize>(magic.size()));

    int version = ingest_byte(in);
    if (static_cast<std::uint8_t>(version) != VERSION)
        throw CorruptLog{std::string{"Unsupported log version"}};
    checksum_data.put(static_cast<char>(version));

    int raw_opcode = ingest_byte(in);
    auto opcode = parse_opcode(raw_opcode);
    // check std::nullopt
    if (!opcode)
        throw CorruptLog{std::format("Opcode not in list: {}", raw_opcode)};
    checksum_data.put(static_cast<char>(raw_opcode));

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

    write_u32(checksum_data, key_size);
    write_u32(checksum_data, value_size);

    std::string key(key_size, '\0');
    if (!key.empty())
        in.read(key.data(), static_cast<std::streamsize>(key.size()));
    if (!in)
        throw TruncatedRecord{};
    checksum_data.write(key.data(), static_cast<std::streamsize>(key.size()));

    std::string value(value_size, '\0');
    if (!value.empty())
        in.read(value.data(), static_cast<std::streamsize>(value.size()));
    if (!in)
        throw TruncatedRecord{};
    checksum_data.write(value.data(), static_cast<std::streamsize>(value.size()));

    const std::uint32_t expected_checksum = read_u32(in);
    if (expected_checksum != crc32(checksum_data.str()))
        throw CorruptLog{"Record checksum mismatch"};

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
    flush_log(out_);
}

void AppendOnlyLog::append_delete(std::string_view key)
{
    write_record(out_, LogRecord{OpCode::Delete, std::string{key}});
    flush_log(out_);
}

void AppendOnlyLog::replay(Istore &store)
{
    out_.flush();
    if (!out_) throw std::runtime_error("Failed to flush out_");

    std::ifstream in(path_, std::ios::binary);
    if (!in) return;

    std::streamoff last_complete_record_end = 0;
    for (;;)
    {
        std::optional<LogRecord> record;
        try
        {
            record = read_record(in);
        }
        catch (const TruncatedRecord &)
        {
            in.close();

            std::error_code error;
            std::filesystem::resize_file(
                path_,
                static_cast<std::uintmax_t>(last_complete_record_end),
                error);
            if (error)
                throw std::runtime_error(
                    "Failed to remove incomplete log tail: " + error.message());
            break;
        }

        if (!record.has_value())
        {
            break;
        }

        const auto record_end = in.tellg();
        if (record_end == std::ifstream::pos_type(-1))
            throw std::runtime_error("Failed to determine log record boundary");
        last_complete_record_end = static_cast<std::streamoff>(record_end);

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