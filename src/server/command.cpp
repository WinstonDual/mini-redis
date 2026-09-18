#include "server/command.hpp"

#include <algorithm>
#include <cctype>

namespace miniredis::server {

using resp::RespValue;
using resp::Array;
using resp::BulkString;
using resp::SimpleString;

namespace {

std::string to_upper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

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

/// Простой matcher для KEYS pattern.
/// Поддерживает только '*' (звёздочка = любая последовательность).
/// Настоящий Redis умеет glob-паттерны сложнее — пока ограничимся.
bool match_pattern(std::string_view key, std::string_view pattern) {
    // Без звёздочек — точное совпадение.
    if (pattern.find('*') == std::string_view::npos) {
        return key == pattern;
    }

    // С одной '*' в паттерне — split и проверка префикса/суффикса.
    auto star = pattern.find('*');
    std::string_view prefix = pattern.substr(0, star);
    std::string_view suffix = pattern.substr(star + 1);

    if (key.size() < prefix.size() + suffix.size()) return false;
    if (key.substr(0, prefix.size()) != prefix) return false;
    if (key.substr(key.size() - suffix.size()) != suffix) return false;
    return true;
}

} // namespace

// ---------- dispatch ----------

RespValue CommandDispatcher::dispatch(const RespValue& input) const {
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

    // --- Connection commands ---
    if (cmd == "PING") {
        if (args.size() == 1) return resp::make_simple_string("PONG");
        if (args.size() == 2) {
            auto msg = as_string_view(args[1]);
            if (!msg) return resp::make_error("ERR wrong number of arguments for 'ping'");
            return resp::make_bulk_string(std::string(*msg));
        }
        return resp::make_error("ERR wrong number of arguments for 'ping'");
    }

    if (cmd == "ECHO") {
        if (args.size() != 2) return resp::make_error("ERR wrong number of arguments for 'echo'");
        auto msg = as_string_view(args[1]);
        if (!msg) return resp::make_error("ERR wrong number of arguments for 'echo'");
        return resp::make_bulk_string(std::string(*msg));
    }

    if (cmd == "COMMAND") {
        return resp::make_array({});
    }

    // --- Key/value commands ---
    if (cmd == "GET")     return cmd_get(args);
    if (cmd == "SET")     return cmd_set(args);
    if (cmd == "DEL")     return cmd_del(args);
    if (cmd == "EXISTS")  return cmd_exists(args);
    if (cmd == "KEYS")    return cmd_keys(args);
    if (cmd == "TYPE")    return cmd_type(args);
    if (cmd == "DBSIZE")  return cmd_dbsize(args);

    // --- TTL commands ---
    if (cmd == "EXPIRE")  return cmd_expire(args);
    if (cmd == "TTL")     return cmd_ttl(args);
    if (cmd == "PERSIST") return cmd_persist(args);

    return resp::make_error("ERR unknown command '" + cmd + "'");
}

// ---------- GET ----------

RespValue CommandDispatcher::cmd_get(const std::vector<RespValue>& args) const {
    if (args.size() != 2) {
        return resp::make_error("ERR wrong number of arguments for 'get'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'get'");

    auto value = store_.get(*key);
    if (!value) return resp::make_null_bulk();
    return resp::make_bulk_string(std::move(*value));
}

// ---------- SET ----------

RespValue CommandDispatcher::cmd_set(const std::vector<RespValue>& args) const {
    // Формы:
    //   SET key value
    //   SET key value EX seconds
    if (args.size() != 3 && args.size() != 5) {
        return resp::make_error("ERR wrong number of arguments for 'set'");
    }
    auto key = as_string_view(args[1]);
    auto val = as_string_view(args[2]);
    if (!key || !val) return resp::make_error("ERR wrong number of arguments for 'set'");

    std::optional<std::int64_t> ttl;

    if (args.size() == 5) {
        auto opt = as_string_view(args[3]);
        if (!opt) return resp::make_error("ERR syntax error");
        std::string opt_upper = to_upper(*opt);
        if (opt_upper != "EX") {
            return resp::make_error("ERR syntax error");
        }
        auto secs_sv = as_string_view(args[4]);
        if (!secs_sv) return resp::make_error("ERR value is not an integer or out of range");
        try {
            std::int64_t secs = std::stoll(std::string(*secs_sv));
            if (secs <= 0) {
                return resp::make_error("ERR invalid expire time in 'set' command");
            }
            ttl = secs;
        } catch (...) {
            return resp::make_error("ERR value is not an integer or out of range");
        }
    }

    store_.set(std::string(*key), std::string(*val), ttl);
    return resp::make_simple_string("OK");
}

// ---------- DEL ----------

RespValue CommandDispatcher::cmd_del(const std::vector<RespValue>& args) const {
    if (args.size() < 2) {
        return resp::make_error("ERR wrong number of arguments for 'del'");
    }
    std::int64_t removed = 0;
    for (std::size_t i = 1; i < args.size(); ++i) {
        auto key = as_string_view(args[i]);
        if (!key) continue;
        if (store_.del(*key)) ++removed;
    }
    return resp::make_integer(removed);
}

// ---------- EXISTS ----------

RespValue CommandDispatcher::cmd_exists(const std::vector<RespValue>& args) const {
    if (args.size() < 2) {
        return resp::make_error("ERR wrong number of arguments for 'exists'");
    }
    std::int64_t count = 0;
    for (std::size_t i = 1; i < args.size(); ++i) {
        auto key = as_string_view(args[i]);
        if (!key) continue;
        if (store_.exists(*key)) ++count;
    }
    return resp::make_integer(count);
}

// ---------- KEYS ----------

RespValue CommandDispatcher::cmd_keys(const std::vector<RespValue>& args) const {
    if (args.size() != 2) {
        return resp::make_error("ERR wrong number of arguments for 'keys'");
    }
    auto pattern = as_string_view(args[1]);
    if (!pattern) return resp::make_error("ERR wrong number of arguments for 'keys'");

    std::vector<RespValue> out;
    for (const auto& k : store_.keys()) {
        if (match_pattern(k, *pattern)) {
            out.push_back(resp::make_bulk_string(k));
        }
    }
    return resp::make_array(std::move(out));
}

// ---------- TYPE ----------

RespValue CommandDispatcher::cmd_type(const std::vector<RespValue>& args) const {
    if (args.size() != 2) {
        return resp::make_error("ERR wrong number of arguments for 'type'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'type'");

    if (store_.exists(*key)) {
        return resp::make_simple_string("string");
    }
    return resp::make_simple_string("none");
}

// ---------- DBSIZE ----------

RespValue CommandDispatcher::cmd_dbsize(const std::vector<RespValue>& args) const {
    if (args.size() != 1) {
        return resp::make_error("ERR wrong number of arguments for 'dbsize'");
    }
    return resp::make_integer(static_cast<std::int64_t>(store_.size()));
}

// ---------- EXPIRE ----------

RespValue CommandDispatcher::cmd_expire(const std::vector<RespValue>& args) const {
    if (args.size() != 3) {
        return resp::make_error("ERR wrong number of arguments for 'expire'");
    }
    auto key = as_string_view(args[1]);
    auto secs_sv = as_string_view(args[2]);
    if (!key || !secs_sv) {
        return resp::make_error("ERR wrong number of arguments for 'expire'");
    }

    std::int64_t secs;
    try {
        secs = std::stoll(std::string(*secs_sv));
    } catch (...) {
        return resp::make_error("ERR value is not an integer or out of range");
    }

    bool ok = store_.expire(*key, secs);
    return resp::make_integer(ok ? 1 : 0);
}

// ---------- TTL ----------

RespValue CommandDispatcher::cmd_ttl(const std::vector<RespValue>& args) const {
    if (args.size() != 2) {
        return resp::make_error("ERR wrong number of arguments for 'ttl'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'ttl'");
    return resp::make_integer(store_.ttl(*key));
}

// ---------- PERSIST ----------

RespValue CommandDispatcher::cmd_persist(const std::vector<RespValue>& args) const {
    if (args.size() != 2) {
        return resp::make_error("ERR wrong number of arguments for 'persist'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'persist'");
    bool ok = store_.persist(*key);
    return resp::make_integer(ok ? 1 : 0);
}

} // namespace miniredis::server