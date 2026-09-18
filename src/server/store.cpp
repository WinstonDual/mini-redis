#include "server/store.hpp"

#include <mutex>

namespace miniredis::server {

namespace {

TimePoint now() { return Clock::now(); }

// Проверка «истёк ли ключ».
template <typename EntryT>
bool is_expired(const EntryT& e, TimePoint t) {
    return e.expire_at.has_value() && *e.expire_at <= t;
}

} // namespace

// ---------- основные операции ----------

std::optional<std::string> Store::get(std::string_view key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return std::nullopt;

    if (is_expired(it->second, now())) {
        data_.erase(it);
        return std::nullopt;
    }
    return it->second.value;
}

bool Store::set(std::string key, std::string value,
                std::optional<std::int64_t> ttl_seconds) {
    std::unique_lock lock(mutex_);

    Entry e;
    e.value = std::move(value);
    if (ttl_seconds.has_value() && *ttl_seconds > 0) {
        e.expire_at = now() + std::chrono::seconds(*ttl_seconds);
    }

    auto it = data_.find(key);
    if (it == data_.end()) {
        data_.emplace(std::move(key), std::move(e));
        return true;
    }
    it->second = std::move(e);
    return false;
}

bool Store::del(std::string_view key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return false;

    if (is_expired(it->second, now())) {
        data_.erase(it);
        return false;
    }
    data_.erase(it);
    return true;
}

bool Store::exists(std::string_view key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return false;

    if (is_expired(it->second, now())) {
        data_.erase(it);
        return false;
    }
    return true;
}

// ---------- TTL ----------

bool Store::expire(std::string_view key, std::int64_t ttl_seconds) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return false;

    if (is_expired(it->second, now())) {
        data_.erase(it);
        return false;
    }

    if (ttl_seconds <= 0) {
        data_.erase(it);
        return true;
    }

    it->second.expire_at = now() + std::chrono::seconds(ttl_seconds);
    return true;
}

bool Store::persist(std::string_view key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return false;

    if (is_expired(it->second, now())) {
        data_.erase(it);
        return false;
    }

    if (!it->second.expire_at.has_value()) return false;
    it->second.expire_at.reset();
    return true;
}

std::int64_t Store::ttl(std::string_view key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return -2;

    if (is_expired(it->second, now())) {
        data_.erase(it);
        return -2;
    }

    if (!it->second.expire_at.has_value()) return -1;

    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        *it->second.expire_at - now()).count();
    if (remaining < 0) remaining = 0;
    return remaining;
}

// ---------- служебные ----------

std::vector<std::string> Store::keys() {
    std::unique_lock lock(mutex_);
    auto t = now();
    std::vector<std::string> out;
    out.reserve(data_.size());

    for (auto it = data_.begin(); it != data_.end(); ) {
        if (is_expired(it->second, t)) {
            it = data_.erase(it);
        } else {
            out.push_back(it->first);
            ++it;
        }
    }
    return out;
}

std::size_t Store::size() {
    std::unique_lock lock(mutex_);
    auto t = now();
    std::size_t live = 0;

    for (auto it = data_.begin(); it != data_.end(); ) {
        if (is_expired(it->second, t)) {
            it = data_.erase(it);
        } else {
            ++live;
            ++it;
        }
    }
    return live;
}

void Store::clear() {
    std::unique_lock lock(mutex_);
    data_.clear();
}

std::size_t Store::sweep_expired() {
    std::unique_lock lock(mutex_);
    auto t = now();
    std::size_t removed = 0;

    for (auto it = data_.begin(); it != data_.end(); ) {
        if (is_expired(it->second, t)) {
            it = data_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

} // namespace miniredis::server