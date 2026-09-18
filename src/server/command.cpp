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
    // --- List commands ---
    if (cmd == "LPUSH")  return cmd_lpush(args);
    if (cmd == "RPUSH")  return cmd_rpush(args);
    if (cmd == "LRANGE") return cmd_lrange(args);
    if (cmd == "LLEN")   return cmd_llen(args);
    if (cmd == "LPOP")   return cmd_lpop(args);
    if (cmd == "RPOP")   return cmd_rpop(args);
    if (cmd == "LINDEX") return cmd_lindex(args);
    // --- Hash commands ---
    if (cmd == "HSET")    return cmd_hset(args);
    if (cmd == "HGET")    return cmd_hget(args);
    if (cmd == "HDEL")    return cmd_hdel(args);
    if (cmd == "HEXISTS") return cmd_hexists(args);
    if (cmd == "HLEN")    return cmd_hlen(args);
    if (cmd == "HGETALL") return cmd_hgetall(args);
    if (cmd == "HKEYS")   return cmd_hkeys(args);
    if (cmd == "HVALS")   return cmd_hvals(args);

    return resp::make_error("ERR unknown command '" + cmd + "'");
}

// ---------- GET ----------

RespValue CommandDispatcher::cmd_get(const std::vector<RespValue>& args) const {
    if (args.size() != 2) {
        return resp::make_error("ERR wrong number of arguments for 'get'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'get'");

    std::string t = store_.type(*key);
    if (t != "string" && t != "none") {
        return resp::make_error("WRONGTYPE Operation against a key holding the wrong kind of value");
    }

    auto value = store_.get_string(*key);
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

        store_.set_string(std::string(*key), std::string(*val), ttl);
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

    return resp::make_simple_string(store_.type(*key));
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

// ---------- Lists ----------

namespace {

constexpr const char* WRONGTYPE_MSG =
    "WRONGTYPE Operation against a key holding the wrong kind of value";

/// Извлечь целое число из RESP-значения. nullopt при неудаче.
std::optional<std::int64_t> as_int(const RespValue& v) {
    auto sv = as_string_view(v);
    if (!sv) return std::nullopt;
    try {
        return std::stoll(std::string(*sv));
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

RespValue CommandDispatcher::cmd_lpush(const std::vector<RespValue>& args) const {
    if (args.size() < 3) {
        return resp::make_error("ERR wrong number of arguments for 'lpush'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'lpush'");

    std::vector<std::string> values;
    values.reserve(args.size() - 2);
    for (std::size_t i = 2; i < args.size(); ++i) {
        auto v = as_string_view(args[i]);
        if (!v) return resp::make_error("ERR wrong number of arguments for 'lpush'");
        values.emplace_back(*v);
    }

    auto len = store_.list_push_left(*key, std::move(values));
    if (!len) return resp::make_error(WRONGTYPE_MSG);
    return resp::make_integer(static_cast<std::int64_t>(*len));
}

RespValue CommandDispatcher::cmd_rpush(const std::vector<RespValue>& args) const {
    if (args.size() < 3) {
        return resp::make_error("ERR wrong number of arguments for 'rpush'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'rpush'");

    std::vector<std::string> values;
    values.reserve(args.size() - 2);
    for (std::size_t i = 2; i < args.size(); ++i) {
        auto v = as_string_view(args[i]);
        if (!v) return resp::make_error("ERR wrong number of arguments for 'rpush'");
        values.emplace_back(*v);
    }

    auto len = store_.list_push_right(*key, std::move(values));
    if (!len) return resp::make_error(WRONGTYPE_MSG);
    return resp::make_integer(static_cast<std::int64_t>(*len));
}

RespValue CommandDispatcher::cmd_lrange(const std::vector<RespValue>& args) const {
    if (args.size() != 4) {
        return resp::make_error("ERR wrong number of arguments for 'lrange'");
    }
    auto key = as_string_view(args[1]);
    auto start_opt = as_int(args[2]);
    auto stop_opt  = as_int(args[3]);
    if (!key || !start_opt || !stop_opt) {
        return resp::make_error("ERR value is not an integer or out of range");
    }

    auto items = store_.list_range(*key, *start_opt, *stop_opt);
    if (!items) return resp::make_error(WRONGTYPE_MSG);

    std::vector<RespValue> out;
    out.reserve(items->size());
    for (auto& s : *items) {
        out.push_back(resp::make_bulk_string(std::move(s)));
    }
    return resp::make_array(std::move(out));
}

RespValue CommandDispatcher::cmd_llen(const std::vector<RespValue>& args) const {
    if (args.size() != 2) {
        return resp::make_error("ERR wrong number of arguments for 'llen'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'llen'");

    auto len = store_.list_length(*key);
    if (!len) return resp::make_error(WRONGTYPE_MSG);
    return resp::make_integer(static_cast<std::int64_t>(*len));
}

RespValue CommandDispatcher::cmd_lpop(const std::vector<RespValue>& args) const {
    if (args.size() != 2) {
        return resp::make_error("ERR wrong number of arguments for 'lpop'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'lpop'");

    std::string t = store_.type(*key);
    if (t != "list" && t != "none") return resp::make_error(WRONGTYPE_MSG);

    auto v = store_.list_pop_left(*key);
    if (!v) return resp::make_null_bulk();
    return resp::make_bulk_string(std::move(*v));
}

RespValue CommandDispatcher::cmd_rpop(const std::vector<RespValue>& args) const {
    if (args.size() != 2) {
        return resp::make_error("ERR wrong number of arguments for 'rpop'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'rpop'");

    std::string t = store_.type(*key);
    if (t != "list" && t != "none") return resp::make_error(WRONGTYPE_MSG);

    auto v = store_.list_pop_right(*key);
    if (!v) return resp::make_null_bulk();
    return resp::make_bulk_string(std::move(*v));
}

RespValue CommandDispatcher::cmd_lindex(const std::vector<RespValue>& args) const {
    if (args.size() != 3) {
        return resp::make_error("ERR wrong number of arguments for 'lindex'");
    }
    auto key = as_string_view(args[1]);
    auto idx = as_int(args[2]);
    if (!key || !idx) {
        return resp::make_error("ERR value is not an integer or out of range");
    }

    std::string t = store_.type(*key);
    if (t != "list" && t != "none") return resp::make_error(WRONGTYPE_MSG);

    auto v = store_.list_index(*key, *idx);
    if (!v) return resp::make_null_bulk();
    return resp::make_bulk_string(std::move(*v));
}

// ---------- Hashes ----------

RespValue CommandDispatcher::cmd_hset(const std::vector<RespValue>& args) const {
    if (args.size() < 4 || (args.size() % 2) != 0) {
        return resp::make_error("ERR wrong number of arguments for 'hset'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'hset'");

    std::vector<std::pair<std::string, std::string>> fields;
    for (std::size_t i = 2; i + 1 < args.size(); i += 2) {
        auto f = as_string_view(args[i]);
        auto v = as_string_view(args[i + 1]);
        if (!f || !v) return resp::make_error("ERR wrong number of arguments for 'hset'");
        fields.emplace_back(std::string(*f), std::string(*v));
    }

    auto added = store_.hash_set(*key, std::move(fields));
    if (!added) return resp::make_error(WRONGTYPE_MSG);
    return resp::make_integer(static_cast<std::int64_t>(*added));
}

RespValue CommandDispatcher::cmd_hget(const std::vector<RespValue>& args) const {
    if (args.size() != 3) {
        return resp::make_error("ERR wrong number of arguments for 'hget'");
    }
    auto key   = as_string_view(args[1]);
    auto field = as_string_view(args[2]);
    if (!key || !field) return resp::make_error("ERR wrong number of arguments for 'hget'");

    std::string t = store_.type(*key);
    if (t != "hash" && t != "none") return resp::make_error(WRONGTYPE_MSG);

    auto v = store_.hash_get(*key, *field);
    if (!v) return resp::make_null_bulk();
    return resp::make_bulk_string(std::move(*v));
}

RespValue CommandDispatcher::cmd_hdel(const std::vector<RespValue>& args) const {
    if (args.size() < 3) {
        return resp::make_error("ERR wrong number of arguments for 'hdel'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'hdel'");

    std::vector<std::string> fields;
    for (std::size_t i = 2; i < args.size(); ++i) {
        auto f = as_string_view(args[i]);
        if (!f) return resp::make_error("ERR wrong number of arguments for 'hdel'");
        fields.emplace_back(*f);
    }

    auto removed = store_.hash_del(*key, std::move(fields));
    if (!removed) return resp::make_error(WRONGTYPE_MSG);
    return resp::make_integer(static_cast<std::int64_t>(*removed));
}

RespValue CommandDispatcher::cmd_hexists(const std::vector<RespValue>& args) const {
    if (args.size() != 3) {
        return resp::make_error("ERR wrong number of arguments for 'hexists'");
    }
    auto key   = as_string_view(args[1]);
    auto field = as_string_view(args[2]);
    if (!key || !field) return resp::make_error("ERR wrong number of arguments for 'hexists'");

    auto exists = store_.hash_exists(*key, *field);
    if (!exists) return resp::make_error(WRONGTYPE_MSG);
    return resp::make_integer(*exists ? 1 : 0);
}

RespValue CommandDispatcher::cmd_hlen(const std::vector<RespValue>& args) const {
    if (args.size() != 2) {
        return resp::make_error("ERR wrong number of arguments for 'hlen'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'hlen'");

    auto len = store_.hash_length(*key);
    if (!len) return resp::make_error(WRONGTYPE_MSG);
    return resp::make_integer(static_cast<std::int64_t>(*len));
}

RespValue CommandDispatcher::cmd_hgetall(const std::vector<RespValue>& args) const {
    if (args.size() != 2) {
        return resp::make_error("ERR wrong number of arguments for 'hgetall'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'hgetall'");

    auto pairs = store_.hash_get_all(*key);
    if (!pairs) return resp::make_error(WRONGTYPE_MSG);

    std::vector<RespValue> out;
    out.reserve(pairs->size() * 2);
    for (auto& [f, v] : *pairs) {
        out.push_back(resp::make_bulk_string(std::move(f)));
        out.push_back(resp::make_bulk_string(std::move(v)));
    }
    return resp::make_array(std::move(out));
}

RespValue CommandDispatcher::cmd_hkeys(const std::vector<RespValue>& args) const {
    if (args.size() != 2) {
        return resp::make_error("ERR wrong number of arguments for 'hkeys'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'hkeys'");

    auto fields = store_.hash_keys(*key);
    if (!fields) return resp::make_error(WRONGTYPE_MSG);

    std::vector<RespValue> out;
    out.reserve(fields->size());
    for (auto& f : *fields) out.push_back(resp::make_bulk_string(std::move(f)));
    return resp::make_array(std::move(out));
}

RespValue CommandDispatcher::cmd_hvals(const std::vector<RespValue>& args) const {
    if (args.size() != 2) {
        return resp::make_error("ERR wrong number of arguments for 'hvals'");
    }
    auto key = as_string_view(args[1]);
    if (!key) return resp::make_error("ERR wrong number of arguments for 'hvals'");

    auto values = store_.hash_values(*key);
    if (!values) return resp::make_error(WRONGTYPE_MSG);

    std::vector<RespValue> out;
    out.reserve(values->size());
    for (auto& v : *values) out.push_back(resp::make_bulk_string(std::move(v)));
    return resp::make_array(std::move(out));
}

} // namespace miniredis::server