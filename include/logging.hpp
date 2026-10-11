#include <optional>
#include <cstdint>
#include <array>
#include <ostream>
#include <istream>
#include <exception>
#include <filesystem>
#include <utility>
#include <include/store.hpp>

constexpr std::array<char, 4> MAGIC{
    // key value log file
    'K', 'V', 'L', 'F'};

constexpr std::uint8_t VERSION = 1;

constexpr std::uint32_t MAX_KEY_SIZE = 64 * 1024;

constexpr std::uint32_t MAX_VALUE_SIZE = MAX_KEY_SIZE;

enum class OpCode : std::uint8_t
{
    Set = 1,
    Delete = 2,
};

struct LogRecord
{
    OpCode opcode;
    std::string key;
    std::string value;
};

class TruncatedRecord : public std::exception
{
};

class CorruptLog : public std::exception
{
public:
    explicit CorruptLog()
        : reason_("No reason provided")
    {
    }
    explicit CorruptLog(std::string reason)
        : reason_{reason}
    {
    }

private:
    std::string reason_;
};

void write_u32(std::ostream &out, std::uint32_t value);

auto read_u32(std::istream &in) -> std::uint32_t;

void write_record(std::ostream &out, const LogRecord &logrecord);

auto read_record(std::istream &in) -> std::optional<LogRecord>;

class AppendOnlyLog
{
public:
    explicit AppendOnlyLog(std::filesystem::path path)
        : out_(path, std::ios::binary | std::ios::app), path_{path}
    {
        if (!out_)
            throw std::runtime_error("AppendOnlyLog failed to open its file path");
        
    }
    void append_set(std::string_view key, std::string_view value);

    void append_delete(std::string_view key);

    void replay(kv::Istore &istore);

private:
    std::ofstream out_;
    std::filesystem::path path_;
};