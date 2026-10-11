#include <persistent_store.hpp>

#include <chrono>
#include <filesystem>
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
}

int main()
{
    try
    {
        return test_persistent_store_round_trip() ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAILED: persistent store integration test threw: "
                  << error.what() << '\n';
        return 1;
    }
}
