#include "resp/parser.hpp"

#include <charconv>
#include <cstddef>

namespace miniredis::resp {

namespace {

// Найти CRLF начиная с позиции pos. Возвращает индекс '\r' или npos.
std::size_t find_crlf(const std::string& buf, std::size_t pos) {
    return buf.find("\r\n", pos);
}

// Распарсить целое число из диапазона [begin, end).
// Ожидаемый формат: необязательный '-', затем цифры. Никаких пробелов.
std::int64_t parse_int(const char* begin, const char* end) {
    std::int64_t value = 0;
    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        throw ProtocolError("invalid integer in RESP message");
    }
    return value;
}

// Парсинг одной строки, оканчивающейся CRLF.
// Возвращает: {позиция_после_CRLF} — либо nullopt если CRLF не найден.
// Текст строки: [buf.data()+pos, crlf_pos).
struct LineView {
    std::string_view text;   // содержимое строки без CRLF
    std::size_t next;        // индекс первого байта после CRLF
};

std::optional<LineView> read_line(const std::string& buf, std::size_t pos) {
    auto crlf = find_crlf(buf, pos);
    if (crlf == std::string::npos) {
        return std::nullopt;
    }
    return LineView{
        std::string_view(buf).substr(pos, crlf - pos),
        crlf + 2
    };
}

// Рекурсивный парсер одного значения начиная с позиции pos.
// Возвращает {значение, позиция_после_значения} либо nullopt если не хватает данных.
//
// ВАЖНО: string_view в возвращаемом значении указывают внутрь buf.
// Мы обязаны построить полную копию до того, как buffer_ будет изменён.
// Поэтому здесь мы сразу строим std::string.
std::optional<std::pair<RespValue, std::size_t>>
parse_value(const std::string& buf, std::size_t pos);

// Парсинг массива.
std::optional<std::pair<RespValue, std::size_t>>
parse_array(const std::string& buf, std::size_t pos) {
    // pos указывает на символ '*' в начале строки
    auto line = read_line(buf, pos + 1);
    if (!line) return std::nullopt;

    std::int64_t count = parse_int(line->text.data(),
                                   line->text.data() + line->text.size());
    if (count < 0) {
        throw ProtocolError("negative array length is not supported yet");
    }

    std::vector<RespValue> items;
    items.reserve(static_cast<std::size_t>(count));

    std::size_t cur = line->next;
    for (std::int64_t i = 0; i < count; ++i) {
        auto sub = parse_value(buf, cur);
        if (!sub) return std::nullopt;   // не хватает данных
        items.push_back(std::move(sub->first));
        cur = sub->second;
    }

    return std::make_pair(make_array(std::move(items)), cur);
}

// Парсинг bulk-строки.
std::optional<std::pair<RespValue, std::size_t>>
parse_bulk_string(const std::string& buf, std::size_t pos) {
    auto line = read_line(buf, pos + 1);
    if (!line) return std::nullopt;

    std::int64_t len = parse_int(line->text.data(),
                                 line->text.data() + line->text.size());

    if (len == -1) {
        return std::make_pair(make_null_bulk(), line->next);
    }
    if (len < 0) {
        throw ProtocolError("invalid bulk string length");
    }

    std::size_t data_start = line->next;
    std::size_t data_end   = data_start + static_cast<std::size_t>(len);

    // нужен ещё 2 байта под CRLF после данных
    if (buf.size() < data_end + 2) {
        return std::nullopt;
    }
    if (buf[data_end] != '\r' || buf[data_end + 1] != '\n') {
        throw ProtocolError("bulk string not terminated by CRLF");
    }

    std::string value(buf.data() + data_start, static_cast<std::size_t>(len));
    return std::make_pair(make_bulk_string(std::move(value)), data_end + 2);
}

std::optional<std::pair<RespValue, std::size_t>>
parse_value(const std::string& buf, std::size_t pos) {
    if (pos >= buf.size()) {
        return std::nullopt;
    }

    char tag = buf[pos];
    switch (tag) {
        case '+': {
            auto line = read_line(buf, pos + 1);
            if (!line) return std::nullopt;
            return std::make_pair(
                make_simple_string(std::string(line->text)),
                line->next);
        }
        case '-': {
            auto line = read_line(buf, pos + 1);
            if (!line) return std::nullopt;
            return std::make_pair(
                make_error(std::string(line->text)),
                line->next);
        }
        case ':': {
            auto line = read_line(buf, pos + 1);
            if (!line) return std::nullopt;
            std::int64_t n = parse_int(line->text.data(),
                                       line->text.data() + line->text.size());
            return std::make_pair(make_integer(n), line->next);
        }
        case '$':
            return parse_bulk_string(buf, pos);
        case '*':
            return parse_array(buf, pos);
        default:
            throw ProtocolError(
                std::string("unknown RESP type tag: '") + tag + "'");
    }
}

} // namespace

// ---------- RespParser ----------

void RespParser::feed(std::string_view data) {
    buffer_.append(data.data(), data.size());
}

std::optional<RespValue> RespParser::try_parse() {
    if (buffer_.empty()) {
        return std::nullopt;
    }

    auto result = parse_value(buffer_, 0);
    if (!result) {
        return std::nullopt;   // ждём ещё данных
    }

    auto& [value, consumed] = *result;

    // Удаляем «съеденную» часть из буфера.
    buffer_.erase(0, consumed);

    return std::move(value);
}

} // namespace miniredis::resp