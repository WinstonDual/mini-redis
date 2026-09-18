#include "server/command.hpp"

#include <algorithm>
#include <cctype>

namespace miniredis::server {

using resp::RespValue;
using resp::Array;
using resp::BulkString;
using resp::SimpleString;

namespace {

// Привести имя команды к верхнему регистру: "ping" -> "PING".
std::string to_upper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

// Извлечь строку из RESP-значения, если это BulkString или SimpleString.
// Для остальных типов вернуть std::nullopt.
std::optional<std::string_view> as_string_view(const RespValue& v) {
    if (auto* s = std::get_if<BulkString>(&v)) {
        if (s->value.has_value()) return std::string_view(*s->value);
        return std::nullopt;
    }
    if (auto* s = std::get_if<SimpleString>(&v)) {
        return std::string_view(s->value);
    }
    return std::nullopt;
}

} // namespace

RespValue CommandDispatcher::dispatch(const RespValue& input) const {
    // Клиент обязан присылать массив. Всё остальное — ошибка протокола.
    if (auto* arr = std::get_if<Array>(&input)) {
        return handle_array(arr->items);
    }
    return resp::make_error("ERR Protocol error: expected array");
}

RespValue CommandDispatcher::handle_array(const std::vector<RespValue>& args) const {
    if (args.empty()) {
        return resp::make_error("ERR Protocol error: empty array");
    }

    auto cmd_opt = as_string_view(args[0]);
    if (!cmd_opt) {
        return resp::make_error("ERR Protocol error: command name is not a string");
    }
    std::string cmd = to_upper(*cmd_opt);

    if (cmd == "PING") {
        // PING -> +PONG
        // PING msg -> $N\r\nmsg\r\n  (как в настоящем Redis)
        if (args.size() == 1) {
            return resp::make_simple_string("PONG");
        }
        if (args.size() == 2) {
            auto msg = as_string_view(args[1]);
            if (!msg) {
                return resp::make_error("ERR wrong number of arguments for 'ping'");
            }
            return resp::make_bulk_string(std::string(*msg));
        }
        return resp::make_error("ERR wrong number of arguments for 'ping'");
    }

    if (cmd == "ECHO") {
        // ECHO msg -> $N\r\nmsg\r\n
        if (args.size() != 2) {
            return resp::make_error("ERR wrong number of arguments for 'echo'");
        }
        auto msg = as_string_view(args[1]);
        if (!msg) {
            return resp::make_error("ERR wrong number of arguments for 'echo'");
        }
        return resp::make_bulk_string(std::string(*msg));
    }

    if (cmd == "COMMAND") {
        // Заглушка: настоящий Redis отвечает большим массивом с описанием команд.
        // Возвращаем пустой массив — этого достаточно для redis-cli.
        return resp::make_array({});
    }

    return resp::make_error("ERR unknown command '" + cmd + "'");
}

} // namespace miniredis::server