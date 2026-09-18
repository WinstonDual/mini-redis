#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace miniredis::resp {

// ---------- Простые типы ----------

/// Простая строка: "+OK\r\n"
struct SimpleString { std::string value; };

/// Ошибка: "-ERR unknown\r\n"
struct Error { std::string value; };

/// Целое число: ":42\r\n"
struct Integer { std::int64_t value; };

/// Bulk-строка: "$5\r\nhello\r\n"
/// null bulk: "$-1\r\n" -> value == std::nullopt
struct BulkString { std::optional<std::string> value; };

// ---------- Рекурсивный тип ----------

/// Forward declaration: Array содержит вектор RespValue.
struct Array;

/// RESP-значение: один из 5 типов.
using RespValue = std::variant<
    SimpleString,
    Error,
    Integer,
    BulkString,
    Array
>;

/// Массив: обёртка над вектором RespValue.
/// Нужна как отдельный тип, потому что std::variant не умеет
/// напрямую содержать std::vector<себя самого>.
struct Array {
    std::vector<RespValue> items;
};

// ---------- Удобные фабрики ----------

inline RespValue make_simple_string(std::string s) { return SimpleString{std::move(s)}; }
inline RespValue make_error(std::string s)         { return Error{std::move(s)}; }
inline RespValue make_integer(std::int64_t n)      { return Integer{n}; }
inline RespValue make_bulk_string(std::string s)   { return BulkString{std::move(s)}; }
inline RespValue make_null_bulk()                  { return BulkString{std::nullopt}; }
inline RespValue make_array(std::vector<RespValue> v) { return Array{std::move(v)}; }

} // namespace miniredis::resp