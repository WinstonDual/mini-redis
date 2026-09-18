#pragma once

#include <cstddef>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace miniredis::server {

/// Потокобезопасное in-memory key-value хранилище.
///
/// Модель:
///   - Много читателей одновременно (shared_lock)
///   - Писатель — эксклюзивно (unique_lock)
///
/// Все операции идемпотентны и не бросают исключений, кроме std::bad_alloc.
class Store {
public:
    Store() = default;

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    /// Вернуть значение по ключу, если есть.
    std::optional<std::string> get(std::string_view key) const;

    /// Установить значение. Возвращает true, если ключ был создан,
    /// и false, если значение было перезаписано.
    bool set(std::string key, std::string value);

    /// Удалить ключ. Возвращает true, если ключ существовал.
    bool del(std::string_view key);

    /// Проверить существование ключа.
    bool exists(std::string_view key) const;

    /// Все ключи (снимок). Порядок неопределён.
    std::vector<std::string> keys() const;

    /// Количество ключей.
    std::size_t size() const;

    /// Удалить всё.
    void clear();

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
};

} // namespace miniredis::server