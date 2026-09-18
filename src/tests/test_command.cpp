#include "tests/mini_test.hpp"
#include "resp/value.hpp"
#include "resp/serializer.hpp"
#include "server/command.hpp"

#include <string>
#include <vector>

using namespace miniredis;

namespace {

// Хелпер: собрать команду как массив bulk-строк.
resp::RespValue cmd(std::initializer_list<std::string> parts) {
    std::vector<resp::RespValue> arr;
    arr.reserve(parts.size());
    for (const auto& p : parts) {
        arr.push_back(resp::make_bulk_string(p));
    }
    return resp::make_array(std::move(arr));
}

std::string run(server::CommandDispatcher& d, resp::RespValue input) {
    return resp::serialize(d.dispatch(input));
}

} // namespace

TEST(cmd_ping) {
    server::CommandDispatcher d;
    CHECK_EQ(run(d, cmd({"PING"})), std::string("+PONG\r\n"));
}

TEST(cmd_ping_with_message) {
    server::CommandDispatcher d;
    CHECK_EQ(run(d, cmd({"PING", "hello"})), std::string("$5\r\nhello\r\n"));
}

TEST(cmd_ping_lowercase) {
    server::CommandDispatcher d;
    CHECK_EQ(run(d, cmd({"ping"})), std::string("+PONG\r\n"));
}

TEST(cmd_ping_too_many_args) {
    server::CommandDispatcher d;
    auto out = run(d, cmd({"PING", "a", "b"}));
    CHECK(out.rfind("-ERR", 0) == 0); // начинается с "-ERR"
}

TEST(cmd_echo) {
    server::CommandDispatcher d;
    CHECK_EQ(run(d, cmd({"ECHO", "hello world"})),
             std::string("$11\r\nhello world\r\n"));
}

TEST(cmd_echo_no_args) {
    server::CommandDispatcher d;
    auto out = run(d, cmd({"ECHO"}));
    CHECK(out.rfind("-ERR", 0) == 0);
}

TEST(cmd_command_empty_array) {
    server::CommandDispatcher d;
    CHECK_EQ(run(d, cmd({"COMMAND"})), std::string("*0\r\n"));
}

TEST(cmd_unknown) {
    server::CommandDispatcher d;
    auto out = run(d, cmd({"FOOBAR"}));
    CHECK(out.rfind("-ERR unknown command 'FOOBAR'", 0) == 0);
}

TEST(cmd_non_array_input_is_error) {
    server::CommandDispatcher d;
    auto out = run(d, resp::make_simple_string("PING"));
    CHECK(out.rfind("-ERR", 0) == 0);
}

TEST(cmd_empty_array_is_error) {
    server::CommandDispatcher d;
    auto out = run(d, resp::make_array({}));
    CHECK(out.rfind("-ERR", 0) == 0);
}