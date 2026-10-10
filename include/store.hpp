#pragma once

#include <unordered_map>
#include <optional>
#include <string>

namespace kv {
class Store {
public:
    void set(const std::string& key, const std::string& value);

    auto get(const std::string& key) const -> std::optional<std::string>;

    auto erase(const std::string& key) -> bool;

    auto size() const noexcept -> std::size_t;

private:
    std::unordered_map<std::string, std::string> data_;
};

}
