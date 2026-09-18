#include "resp/serializer.hpp"

#include <variant>

namespace miniredis::resp {

namespace {

/// Visitor для std::visit — компилятор сам вызовет нужную перегрузку.
struct Serializer {
    std::string& out;

    void operator()(const SimpleString& s) const {
        out += '+';
        out += s.value;
        out += "\r\n";
    }

    void operator()(const Error& e) const {
        out += '-';
        out += e.value;
        out += "\r\n";
    }

    void operator()(const Integer& i) const {
        out += ':';
        out += std::to_string(i.value);
        out += "\r\n";
    }

    void operator()(const BulkString& b) const {
        if (!b.value.has_value()) {
            out += "$-1\r\n";
            return;
        }
        out += '$';
        out += std::to_string(b.value->size());
        out += "\r\n";
        out += *b.value;
        out += "\r\n";
    }

    void operator()(const Array& arr) const {
        out += '*';
        out += std::to_string(arr.items.size());
        out += "\r\n";
        for (const auto& item : arr.items) {
            std::visit(*this, item);
        }
    }
};

} // namespace

std::string serialize(const RespValue& value) {
    std::string out;
    out.reserve(64);
    std::visit(Serializer{out}, value);
    return out;
}

} // namespace miniredis::resp