#pragma once

#include "resp/value.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace miniredis::resp {

/// Исключение, возникающее при некорректном RESP-сообщении.
/// В реальной реализации (Шаг 2.3) мы будем превращать её в RESP-ошибку.
struct ProtocolError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// Инкрементальный парсер RESP.
///
/// Использование:
///     RespParser parser;
///     parser.feed(bytes_from_socket);
///     while (auto value = parser.try_parse()) {
///         handle(*value);
///     }
class RespParser {
public:
    RespParser() = default;

    /// Добавить сырые байты, полученные из сокета.
    void feed(std::string_view data);

    /// Попытаться вытащить одно сообщение из внутреннего буфера.
    /// - Возвращает значение, если сообщение полностью получено.
    /// - Возвращает std::nullopt, если данных не хватает (partial read).
    /// - Бросает ProtocolError при некорректном формате.
    std::optional<RespValue> try_parse();

    /// Есть ли ещё данные в буфере (потенциально можно парсить дальше).
    bool has_pending() const noexcept { return !buffer_.empty(); }

    /// Размер внутреннего буфера.
    std::size_t buffered_size() const noexcept { return buffer_.size(); }

private:
    std::string buffer_;
};

} // namespace miniredis::resp