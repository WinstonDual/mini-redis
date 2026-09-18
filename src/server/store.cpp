#include "server/store.hpp"

#include <mutex>

namespace miniredis::server {

std::optional<std::string> Store::get(std::string_view key) const {
    std::shared_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool Store::set(std::string key, std::string value) {
    std::unique_lock lock(mutex_);
    auto [it, inserted] = data_.insert_or_assign(std::move(key), std::move(value));
    (void)it;
    return inserted;
}

bool Store::del(std::string_view key) {
    std::unique_lock lock(mutex_);
    return data_.erase(std::string(key)) > 0;
}

bool Store::exists(std::string_view key) const {
    std::shared_lock lock(mutex_);
    return data_.find(std::string(key)) != data_.end();
}

std::vector<std::string> Store::keys() const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> out;
    out.reserve(data_.size());
    for (const auto& [k, _] : data_) {
        out.push_back(k);
    }
    return out;
}

std::size_t Store::size() const {
    std::shared_lock lock(mutex_);
    return data_.size();
}

void Store::clear() {
    std::unique_lock lock(mutex_);
    data_.clear();
}

} // namespace miniredis::server