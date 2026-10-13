#pragma once

#include <logging.hpp>
#include <mutex>
#include <shared_mutex>
#include <store.hpp>

class PersistentStore final : public Istore
{
public:
    explicit PersistentStore(const std::filesystem::path &path);

    void set(const std::string &key, const std::string &value) override;

    auto get(const std::string &key) const -> std::optional<std::string> override;

    auto erase(const std::string &key) -> bool override;

    auto size() const noexcept -> std::size_t override;

private:
    mutable std::shared_mutex mutex_;
    AppendOnlyLog log_;
    ConcurrentStore store_;
};