#include <store.hpp>

#include <mutex>
#include <utility>

namespace kv
{
    void HashStore::set(const std::string &key, const std::string &value)
    {
        // move here to stop copies
        data_.insert_or_assign(std::move(key), std::move(value));
    }

    void ConcurrentStore::set(const std::string &key, const std::string &value)
    {
        std::unique_lock lock(mutex_);
        data_.insert_or_assign(key, value);
    }

    auto HashStore::get(const std::string &key) const -> std::optional<std::string>
    {
        auto it = data_.find(key);
        if (it == data_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    auto ConcurrentStore::get(const std::string &key) const -> std::optional<std::string>
    {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);

        if (it == data_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    bool HashStore::erase(const std::string &key)
    {
        // return true if key is found and erased
        return data_.erase(key) > 0;
    }

    bool ConcurrentStore::erase(const std::string &key)
    {
        std::unique_lock lock(mutex_);

        return data_.erase(key) > 0;
    }

    std::size_t HashStore::size() const noexcept
    {
        return data_.size();
    }

    std::size_t ConcurrentStore::size() const noexcept
    {
        std::shared_lock lock(mutex_);
        return data_.size();
    }

}