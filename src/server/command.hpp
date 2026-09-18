#pragma once

#include "resp/value.hpp"

#include <string>

namespace miniredis::server {

/// Диспетчер команд: принимает RESP-значение (обычно Array от клиента)
/// и возвращает RESP-ответ для отправки обратно.
///
/// Пока команды stateless. В следующих шагах здесь появится доступ к Store.
class CommandDispatcher {
public:
    CommandDispatcher() = default;

    /// Обработать одно входящее значение и вернуть ответ.
    /// Никогда не бросает: любые ошибки превращаются в RespValue (Error).
    resp::RespValue dispatch(const resp::RespValue& input) const;

private:
    resp::RespValue handle_array(const std::vector<resp::RespValue>& args) const;
};

} // namespace miniredis::server