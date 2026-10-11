#include <persistent_store.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <system_error>

namespace
{
    class TemporaryLog
    {
    public:
        TemporaryLog()
            : path_{
                  std::filesystem::temp_directory_path()
                  / ("key_value_persistent_store_"
                     + std::to_string(
                         std::chrono::steady_clock::now()
                             .time_since_epoch()
                             .count())
                     + ".log")}
        {
        }

        ~TemporaryLog()
        {
            std::error_code error;
            std::filesystem::remove(path_, error);
        }

        auto path() const -> const std::filesystem::path &
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };

    bool check(bool condition, std::string_view message)
    {
        if (!condition)
            std::cerr << "FAILED: " << message << '\n';
        return condition;
    }

    bool test_persistent_store_round_trip()
    {
        TemporaryLog log;

        {
            PersistentStore store{log.path()};
            if (!check(store.size() == 0, "new persistent store starts empty"))
                return false;

            store.set("keep", "first");
            store.set("keep", "updated");
            store.set("remove", "temporary");

            if (!check(store.get("keep") == "updated", "set replaces the value")
                || !check(store.erase("remove"), "erase reports an existing key")
                || !check(!store.erase("missing"), "erase reports a missing key")
                || !check(!store.get("remove"), "erased key is absent in memory")
                || !check(store.size() == 1, "size reflects the current state"))
            {
                return false;
            }
        }

        {
            PersistentStore restored{log.path()};
            if (!check(restored.get("keep") == "updated",
                       "replay restores the latest value")
                || !check(!restored.get("remove"),
                          "replay preserves the deletion")
                || !check(restored.size() == 1,
                          "replay restores the correct number of entries"))
            {
                return false;
            }
        }

        return true;
    }

    bool test_replay_rejects_checksum_mismatch()
    {
        TemporaryLog log;
        {
            PersistentStore store{log.path()};
            store.set("key", "value");
        }

        {
            std::fstream file{
                log.path(),
                std::ios::in | std::ios::out | std::ios::binary};
            if (!check(static_cast<bool>(file), "open log to corrupt checksum"))
                return false;

            file.seekg(-1, std::ios::end);
            char last_byte{};
            file.get(last_byte);
            if (!check(static_cast<bool>(file), "read final checksum byte"))
                return false;

            last_byte ^= 1;
            file.seekp(-1, std::ios::end);
            file.put(last_byte);
            if (!check(static_cast<bool>(file), "corrupt final checksum byte"))
                return false;
        }

        try
        {
            PersistentStore corrupted{log.path()};
        }
        catch (const CorruptLog &)
        {
            return true;
        }

        return check(false, "replay rejects a checksum mismatch");
    }
}

int main()
{
    try
    {
        if (!test_persistent_store_round_trip())
            return 1;
        return test_replay_rejects_checksum_mismatch() ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAILED: persistent store integration test threw: "
                  << error.what() << '\n';
        return 1;
    }
}
