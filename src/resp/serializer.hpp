#pragma once

#include "resp/value.hpp"

#include <string>

namespace miniredis::resp {

/// Превращает RespValue в байты по протоколу RESP.
/// Результат всегда заканчивается на "\r\n" (для SimpleString/Error/Integer/Bulk)
/// либо представляет собой конкатенацию сериализованных элементов (для Array).
std::string serialize(const RespValue& value);

} // namespace miniredis::resp