#include "server/store.hpp"

#include <algorithm>
#include <mutex>

namespace miniredis::server {

namespace {

TimePoint now() { return Clock::now(); }

bool is_expired_at(const std::optional<TimePoint>& t, TimePoint cur) {
    return t.has_value() && *t <= cur;
}

} // namespace

// ---------- общие операции ----------

bool Store::del(std::string_view key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return false;

    if (is_expired_at(it->second.expire_at, now())) {
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

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return false;
    }
    return true;
}

std::string Store::type(std::string_view key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return "none";

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return "none";
    }

    if (std::holds_alternative<StringValue>(it->second.value)) return "string";
    if (std::holds_alternative<ListValue>(it->second.value))   return "list";
    if (std::holds_alternative<HashValue>(it->second.value))   return "hash";
    return "none";
}

std::vector<std::string> Store::keys() {
    std::unique_lock lock(mutex_);
    auto t = now();
    std::vector<std::string> out;
    out.reserve(data_.size());

    for (auto it = data_.begin(); it != data_.end(); ) {
        if (is_expired_at(it->second.expire_at, t)) {
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
        if (is_expired_at(it->second.expire_at, t)) {
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
        if (is_expired_at(it->second.expire_at, t)) {
            it = data_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

// ---------- строки ----------

bool Store::set_string(std::string key, std::string value,
                       std::optional<std::int64_t> ttl_seconds) {
    std::unique_lock lock(mutex_);

    Entry e;
    e.value = StringValue{std::move(value)};
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

std::optional<std::string> Store::get_string(std::string_view key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return std::nullopt;

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return std::nullopt;
    }

    if (auto* sv = std::get_if<StringValue>(&it->second.value)) {
        return sv->data;
    }
    return std::nullopt; // не строка (WRONGTYPE разбирается в командах)
}

// ---------- списки ----------

namespace {

/// Вспомогательный шаблон: применить операцию к списку,
/// проверив что ключ — действительно список.
/// Если ключа нет — создать новый ListValue.
/// Если ключ есть, но не список — вернуть nullopt (WRONGTYPE).

// Так как это в .cpp, и мы работаем с std::variant и std::deque,
// сделаем это вручную в каждой функции — так нагляднее.

} // namespace

std::optional<std::size_t>
Store::list_push_left(std::string_view key,
                      std::vector<std::string> values) {
    std::unique_lock lock(mutex_);
    std::string k(key);

    auto it = data_.find(k);
    if (it != data_.end() && is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        it = data_.end();
    }

    if (it == data_.end()) {
        Entry e;
        e.value = ListValue{};
        it = data_.emplace(std::move(k), std::move(e)).first;
    }

    auto* lv = std::get_if<ListValue>(&it->second.value);
    if (!lv) return std::nullopt;

    // LPUSH k a b c → список становится [c, b, a]
    for (const auto& v : values) {
        lv->items.push_front(v);
    }
    return lv->items.size();
}

std::optional<std::size_t>
Store::list_push_right(std::string_view key,
                       std::vector<std::string> values) {
    std::unique_lock lock(mutex_);
    std::string k(key);

    auto it = data_.find(k);
    if (it != data_.end() && is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        it = data_.end();
    }

    if (it == data_.end()) {
        Entry e;
        e.value = ListValue{};
        it = data_.emplace(std::move(k), std::move(e)).first;
    }

    auto* lv = std::get_if<ListValue>(&it->second.value);
    if (!lv) return std::nullopt;

    for (const auto& v : values) {
        lv->items.push_back(v);
    }
    return lv->items.size();
}

std::optional<std::size_t>
Store::list_length(std::string_view key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return std::size_t{0};

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return std::size_t{0};
    }

    auto* lv = std::get_if<ListValue>(&it->second.value);
    if (!lv) return std::nullopt; // не список
    return lv->items.size();
}

std::optional<std::vector<std::string>>
Store::list_range(std::string_view key, std::int64_t start, std::int64_t stop) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return std::vector<std::string>{};

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return std::vector<std::string>{};
    }

    auto* lv = std::get_if<ListValue>(&it->second.value);
    if (!lv) return std::nullopt;

    const auto n = static_cast<std::int64_t>(lv->items.size());

    // Приведение отрицательных индексов
    if (start < 0) start = n + start;
    if (stop  < 0) stop  = n + stop;
    if (start < 0) start = 0;
    if (start >= n) return std::vector<std::string>{};
    if (stop >= n)  stop = n - 1;
    if (stop < start) return std::vector<std::string>{};

    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(stop - start + 1));
    for (std::int64_t i = start; i <= stop; ++i) {
        out.push_back(lv->items[static_cast<std::size_t>(i)]);
    }
    return out;
}

std::optional<std::string> Store::list_pop_left(std::string_view key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return std::nullopt;

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return std::nullopt;
    }

    auto* lv = std::get_if<ListValue>(&it->second.value);
    if (!lv) return std::nullopt;
    if (lv->items.empty()) return std::nullopt;

    std::string v = std::move(lv->items.front());
    lv->items.pop_front();

    // Redis удаляет пустые списки
    if (lv->items.empty()) data_.erase(it);
    return v;
}

std::optional<std::string> Store::list_pop_right(std::string_view key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return std::nullopt;

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return std::nullopt;
    }

    auto* lv = std::get_if<ListValue>(&it->second.value);
    if (!lv) return std::nullopt;
    if (lv->items.empty()) return std::nullopt;

    std::string v = std::move(lv->items.back());
    lv->items.pop_back();

    if (lv->items.empty()) data_.erase(it);
    return v;
}

std::optional<std::string> Store::list_index(std::string_view key,
                                             std::int64_t index) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return std::nullopt;

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return std::nullopt;
    }

    auto* lv = std::get_if<ListValue>(&it->second.value);
    if (!lv) return std::nullopt;

    const auto n = static_cast<std::int64_t>(lv->items.size());
    if (index < 0) index = n + index;
    if (index < 0 || index >= n) return std::nullopt;

    return lv->items[static_cast<std::size_t>(index)];
}

// ---------- TTL ----------

bool Store::expire(std::string_view key, std::int64_t ttl_seconds) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return false;

    if (is_expired_at(it->second.expire_at, now())) {
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

    if (is_expired_at(it->second.expire_at, now())) {
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

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return -2;
    }

    if (!it->second.expire_at.has_value()) return -1;

    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        *it->second.expire_at - now()).count();
    if (remaining < 0) remaining = 0;
    return remaining;
}

// ---------- хэши ----------

std::optional<std::size_t>
Store::hash_set(std::string_view key,
                std::vector<std::pair<std::string, std::string>> fields) {
    std::unique_lock lock(mutex_);
    std::string k(key);

    auto it = data_.find(k);
    if (it != data_.end() && is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        it = data_.end();
    }

    if (it == data_.end()) {
        Entry e;
        e.value = HashValue{};
        it = data_.emplace(std::move(k), std::move(e)).first;
    }

    auto* hv = std::get_if<HashValue>(&it->second.value);
    if (!hv) return std::nullopt;

    std::size_t added = 0;
    for (auto& [f, v] : fields) {
        auto [fit, inserted] = hv->fields.insert_or_assign(std::move(f), std::move(v));
        (void)fit;
        if (inserted) ++added;
    }
    return added;
}

std::optional<std::string>
Store::hash_get(std::string_view key, std::string_view field) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return std::nullopt;

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return std::nullopt;
    }

    auto* hv = std::get_if<HashValue>(&it->second.value);
    if (!hv) return std::nullopt;

    auto fit = hv->fields.find(std::string(field));
    if (fit == hv->fields.end()) return std::nullopt;
    return fit->second;
}

std::optional<std::size_t>
Store::hash_del(std::string_view key, std::vector<std::string> fields) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return std::size_t{0};

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return std::size_t{0};
    }

    auto* hv = std::get_if<HashValue>(&it->second.value);
    if (!hv) return std::nullopt;

    std::size_t removed = 0;
    for (auto& f : fields) {
        removed += hv->fields.erase(f);
    }

    if (hv->fields.empty()) data_.erase(it);
    return removed;
}

std::optional<bool>
Store::hash_exists(std::string_view key, std::string_view field) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return false;

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return false;
    }

    auto* hv = std::get_if<HashValue>(&it->second.value);
    if (!hv) return std::nullopt;

    return hv->fields.find(std::string(field)) != hv->fields.end();
}

std::optional<std::size_t> Store::hash_length(std::string_view key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return std::size_t{0};

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return std::size_t{0};
    }

    auto* hv = std::get_if<HashValue>(&it->second.value);
    if (!hv) return std::nullopt;

    return hv->fields.size();
}

std::optional<std::vector<std::pair<std::string, std::string>>>
Store::hash_get_all(std::string_view key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return std::vector<std::pair<std::string, std::string>>{};

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return std::vector<std::pair<std::string, std::string>>{};
    }

    auto* hv = std::get_if<HashValue>(&it->second.value);
    if (!hv) return std::nullopt;

    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(hv->fields.size());
    for (const auto& [f, v] : hv->fields) {
        out.emplace_back(f, v);
    }
    return out;
}

std::optional<std::vector<std::string>> Store::hash_keys(std::string_view key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return std::vector<std::string>{};

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return std::vector<std::string>{};
    }

    auto* hv = std::get_if<HashValue>(&it->second.value);
    if (!hv) return std::nullopt;

    std::vector<std::string> out;
    out.reserve(hv->fields.size());
    for (const auto& [f, _] : hv->fields) out.push_back(f);
    return out;
}

std::optional<std::vector<std::string>> Store::hash_values(std::string_view key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(std::string(key));
    if (it == data_.end()) return std::vector<std::string>{};

    if (is_expired_at(it->second.expire_at, now())) {
        data_.erase(it);
        return std::vector<std::string>{};
    }

    auto* hv = std::get_if<HashValue>(&it->second.value);
    if (!hv) return std::nullopt;

    std::vector<std::string> out;
    out.reserve(hv->fields.size());
    for (const auto& [_, v] : hv->fields) out.push_back(v);
    return out;
}

} // namespace miniredis::server