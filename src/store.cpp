#include <store.hpp>

#include <utility>

namespace kv
{
    void Store::set(const std::string &key, const std::string &value)
    {
        // move here to stop copies
        data_.insert_or_assign(std::move(key), std::move(value));
    }
    std::optional<std::string> Store::get(const std::string &key) const
    {
        auto it = data_.find(key);
        if (it == data_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    bool Store::erase(const std::string &key)
    {
        // return true if key is found and erased
        return data_.erase(key) > 0;
    }

    std::size_t Store::size() const noexcept
    {
        return data_.size();
    }

}