#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace miniredis::server {

using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// ---------- Типы значений ----------

/// Строка (значение по умолчанию для SET/GET).
struct StringValue {
    std::string data;
};

/// Список строк. std::deque даёт O(1) вставку/удаление с обоих концов.
struct ListValue {
    std::deque<std::string> items;
};

/// Хэш: field → value.
struct HashValue {
    std::unordered_map<std::string, std::string> fields;
};

/// Одно из поддерживаемых типов значений.
using Value = std::variant<StringValue, ListValue, HashValue>;
/// Потокобезопасное in-memory key-value хранилище с поддержкой TTL и типов.
class Store {
public:
    Store() = default;
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    // ---- Общее ----

    /// Удалить ключ. true — был и удалён, false — не было.
    bool del(std::string_view key);

    /// Существует ли ключ (учитывая TTL).
    bool exists(std::string_view key);

    /// Тип значения: "string", "list", "none".
    std::string type(std::string_view key);

    /// Все живые ключи.
    std::vector<std::string> keys();

    /// Количество живых ключей.
    std::size_t size();

    /// Удалить всё.
    void clear();

    /// Удалить все истёкшие ключи. Возвращает их число.
    std::size_t sweep_expired();

    // ---- Строки ----

    /// true — создан, false — перезаписан.
    bool set_string(std::string key, std::string value,
                    std::optional<std::int64_t> ttl_seconds = std::nullopt);

    /// Значение по ключу, если это строка.
    /// nullopt — если ключа нет или тип не строка.
    /// (Различие проверяется через type() — WRONGTYPE обрабатывается в командах.)
    std::optional<std::string> get_string(std::string_view key);

    // ---- Списки ----

    /// Вставить в начало (слева). Возвращает новую длину или
    /// std::nullopt если ключ существует и он не список (WRONGTYPE).
    std::optional<std::size_t> list_push_left(std::string_view key,
                                              std::vector<std::string> values);

    /// Вставить в конец (справа).
    std::optional<std::size_t> list_push_right(std::string_view key,
                                               std::vector<std::string> values);

    /// Длина списка. nullopt — ключа нет или не список.
    std::optional<std::size_t> list_length(std::string_view key);

    /// Элементы [start, stop] включительно, с поддержкой отрицательных индексов.
    /// Возвращает список строк. nullopt — ключа нет или не список.
    std::optional<std::vector<std::string>>
    list_range(std::string_view key, std::int64_t start, std::int64_t stop);

    /// Удалить и вернуть элемент с начала. nullopt — если пусто/нет/не список.
    std::optional<std::string> list_pop_left(std::string_view key);
    std::optional<std::string> list_pop_right(std::string_view key);

    /// Элемент по индексу (с поддержкой отрицательных). nullopt — нет.
        /// Элемент по индексу (с поддержкой отрицательных). nullopt — нет.
    std::optional<std::string> list_index(std::string_view key, std::int64_t index);

    // ---- Хэши ----

    /// HSET: установить поля. Возвращает число НОВЫХ полей.
    std::optional<std::size_t>
    hash_set(std::string_view key,
             std::vector<std::pair<std::string, std::string>> fields);

    std::optional<std::string>
    hash_get(std::string_view key, std::string_view field);

    std::optional<std::size_t>
    hash_del(std::string_view key, std::vector<std::string> fields);

    std::optional<bool>
    hash_exists(std::string_view key, std::string_view field);

    std::optional<std::size_t> hash_length(std::string_view key);

    std::optional<std::vector<std::pair<std::string, std::string>>>
    hash_get_all(std::string_view key);

    std::optional<std::vector<std::string>> hash_keys(std::string_view key);
    std::optional<std::vector<std::string>> hash_values(std::string_view key);

    // ---- TTL ----

    bool expire(std::string_view key, std::int64_t ttl_seconds);
    bool persist(std::string_view key);
    std::int64_t ttl(std::string_view key);

private:
    struct Entry {
        Value value;
        std::optional<TimePoint> expire_at;
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Entry> data_;
};

} // namespace miniredis::server