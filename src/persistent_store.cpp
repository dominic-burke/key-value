#include <persistent_store.hpp>

PersistentStore::PersistentStore(const std::filesystem::path &path)
    : log_{path}
{
    log_.replay(store_);
}

void PersistentStore::set(const std::string &key, const std::string &value)
{
    store_.set(key, value);
    log_.append_set(key, value);
}

auto PersistentStore::get(const std::string &key) const -> std::optional<std::string>
{
    return store_.get(key);
}

auto PersistentStore::erase(const std::string &key) -> bool
{
    if (!store_.get(key))
        return false;
    log_.append_delete(key);
    return store_.erase(key);
}

auto PersistentStore::size() const noexcept -> std::size_t
{
    return store_.size();
}
