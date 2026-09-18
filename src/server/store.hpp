#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace miniredis::server {

using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

/// Потокобезопасное in-memory key-value хранилище с поддержкой TTL.
class Store {
public:
    Store() = default;

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    // --- Основные операции ---

    /// Вернуть значение по ключу (учитывая TTL).
    std::optional<std::string> get(std::string_view key);

    /// Установить значение. Возвращает true, если ключ был создан,
    /// false — если перезаписан.
    /// Если ttl_seconds > 0 — устанавливает TTL.
    bool set(std::string key, std::string value,
             std::optional<std::int64_t> ttl_seconds = std::nullopt);

    /// Удалить ключ. Возвращает true, если ключ существовал и был удалён.
    bool del(std::string_view key);

    /// Проверить существование ключа (учитывая TTL).
    bool exists(std::string_view key);

    // --- TTL ---

    /// Установить TTL. true — установлен, false — ключа нет.
    /// Если ttl_seconds <= 0 — ключ удаляется немедленно.
    bool expire(std::string_view key, std::int64_t ttl_seconds);

    /// Снять TTL. true — снят, false — ключа нет или TTL не было.
    bool persist(std::string_view key);

    /// Сколько секунд осталось:
    ///   N >= 0 — осталось
    ///   -1     — нет TTL
    ///   -2     — нет ключа
    std::int64_t ttl(std::string_view key);

    // --- Служебные ---

    std::vector<std::string> keys();
    std::size_t size();
    void clear();
    std::size_t sweep_expired();

private:
    struct Entry {
        std::string value;
        std::optional<TimePoint> expire_at;
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Entry> data_;
};

} // namespace miniredis::server