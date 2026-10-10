#pragma once

#include <unordered_map>
#include <optional>
#include <string>
#include <shared_mutex>

namespace kv
{

    class Istore
    {
    public:
        virtual ~Istore() = default;

        virtual void set(const std::string &key, const std::string &value) = 0;
        virtual auto get(const std::string &key) const -> std::optional<std::string> = 0;
        virtual auto erase(const std::string &key) -> bool = 0;
        virtual auto size() const noexcept -> std::size_t = 0;
    };

    class ConcurrentStore final : public Istore
    {
    public:
        void set(const std::string &key, const std::string &value) override;
        auto get(const std::string &key) const
            -> std::optional<std::string> override;
        bool erase(const std::string &key) override;
        auto size() const noexcept -> std::size_t override;

    private:
        mutable std::shared_mutex mutex_;
        std::unordered_map<std::string, std::string> data_;
    };

    class HashStore final : public Istore
    {
    public:
        void set(const std::string &key, const std::string &value) override;
        auto get(const std::string &key) const
            -> std::optional<std::string> override;
        bool erase(const std::string &key) override;
        auto size() const noexcept -> std::size_t override;

    private:
        std::unordered_map<std::string, std::string> data_;
    };

}
