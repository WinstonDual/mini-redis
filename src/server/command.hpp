#pragma once

#include "resp/value.hpp"
#include "server/store.hpp"

#include <string>

namespace miniredis::server {

/// Диспетчер команд: принимает RESP-значение (Array от клиента)
/// и возвращает RESP-ответ.
///
/// Держит ссылку на Store — общий на весь сервер.
class CommandDispatcher {
public:
    /// Store должен жить дольше, чем dispatcher.
    explicit CommandDispatcher(Store& store) : store_(store) {}

    /// Обработать одно входящее значение.
    /// Никогда не бросает: любые ошибки превращаются в RespValue (Error).
    resp::RespValue dispatch(const resp::RespValue& input) const;

private:
    resp::RespValue handle_array(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_get(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_set(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_del(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_exists(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_keys(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_type(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_dbsize(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_expire(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_ttl(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_persist(const std::vector<resp::RespValue>& args) const;
    // --- Lists ---
    resp::RespValue cmd_lpush(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_rpush(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_lrange(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_llen(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_lpop(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_rpop(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_lindex(const std::vector<resp::RespValue>& args) const;
    // --- Hashes ---
    resp::RespValue cmd_hset(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_hget(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_hdel(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_hexists(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_hlen(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_hgetall(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_hkeys(const std::vector<resp::RespValue>& args) const;
    resp::RespValue cmd_hvals(const std::vector<resp::RespValue>& args) const;

    Store& store_;
};

} // namespace miniredis::server