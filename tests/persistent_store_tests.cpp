#include <persistent_store.hpp>

#include <chrono>
#include <cstdlib>
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

    int simulate_crash_after_log_append(const std::filesystem::path &path)
    {
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error)
        {
            std::cerr << "Failed to remove old crash-recovery log: "
                      << error.message() << '\n';
            return 1;
        }

        AppendOnlyLog log{path};
        log.append_set("crash-key", "recovered-value");
        std::_Exit(0);
    }

    int verify_crash_recovery(const std::filesystem::path &path)
    {
        bool passed = false;
        try
        {
            PersistentStore recovered{path};
            passed = recovered.get("crash-key") == "recovered-value"
                && recovered.size() == 1;
        }
        catch (const std::exception &error)
        {
            std::cerr << "Recovery after simulated process crash failed: "
                      << error.what() << '\n';
        }

        std::error_code error;
        std::filesystem::remove(path, error);
        if (error)
        {
            std::cerr << "Failed to remove crash-recovery log: "
                      << error.message() << '\n';
            passed = false;
        }

        if (!passed)
            std::cerr << "FAILED: replay restores the flushed record after restart\n";
        return passed ? 0 : 1;
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc == 3 && std::string_view{argv[1]} == "--crash-after-log-append")
            return simulate_crash_after_log_append(argv[2]);
        if (argc == 3 && std::string_view{argv[1]} == "--verify-crash-recovery")
            return verify_crash_recovery(argv[2]);

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
