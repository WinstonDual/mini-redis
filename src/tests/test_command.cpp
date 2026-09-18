#include "tests/mini_test.hpp"
#include "resp/value.hpp"
#include "resp/serializer.hpp"
#include "server/command.hpp"
#include "server/store.hpp"

#include <string>
#include <vector>

using namespace miniredis;

namespace {

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

// ---------- PING / ECHO / COMMAND ----------

TEST(cmd_ping) {
    server::Store s; server::CommandDispatcher d(s);
    CHECK_EQ(run(d, cmd({"PING"})), std::string("+PONG\r\n"));
}

TEST(cmd_ping_with_message) {
    server::Store s; server::CommandDispatcher d(s);
    CHECK_EQ(run(d, cmd({"PING", "hello"})), std::string("$5\r\nhello\r\n"));
}

TEST(cmd_ping_lowercase) {
    server::Store s; server::CommandDispatcher d(s);
    CHECK_EQ(run(d, cmd({"ping"})), std::string("+PONG\r\n"));
}

TEST(cmd_ping_too_many_args) {
    server::Store s; server::CommandDispatcher d(s);
    auto out = run(d, cmd({"PING", "a", "b"}));
    CHECK(out.rfind("-ERR", 0) == 0);
}

TEST(cmd_echo) {
    server::Store s; server::CommandDispatcher d(s);
    CHECK_EQ(run(d, cmd({"ECHO", "hello world"})),
             std::string("$11\r\nhello world\r\n"));
}

TEST(cmd_echo_no_args) {
    server::Store s; server::CommandDispatcher d(s);
    auto out = run(d, cmd({"ECHO"}));
    CHECK(out.rfind("-ERR", 0) == 0);
}

TEST(cmd_command_empty_array) {
    server::Store s; server::CommandDispatcher d(s);
    CHECK_EQ(run(d, cmd({"COMMAND"})), std::string("*0\r\n"));
}

TEST(cmd_unknown) {
    server::Store s; server::CommandDispatcher d(s);
    auto out = run(d, cmd({"FOOBAR"}));
    CHECK(out.rfind("-ERR unknown command 'FOOBAR'", 0) == 0);
}

TEST(cmd_non_array_input_is_error) {
    server::Store s; server::CommandDispatcher d(s);
    auto out = run(d, resp::make_simple_string("PING"));
    CHECK(out.rfind("-ERR", 0) == 0);
}

TEST(cmd_empty_array_is_error) {
    server::Store s; server::CommandDispatcher d(s);
    auto out = run(d, resp::make_array({}));
    CHECK(out.rfind("-ERR", 0) == 0);
}

// ---------- GET / SET / DEL ----------

TEST(cmd_set_then_get) {
    server::Store s; server::CommandDispatcher d(s);
    CHECK_EQ(run(d, cmd({"SET", "foo", "bar"})), std::string("+OK\r\n"));
    CHECK_EQ(run(d, cmd({"GET", "foo"})), std::string("$3\r\nbar\r\n"));
}

TEST(cmd_get_missing_returns_null_bulk) {
    server::Store s; server::CommandDispatcher d(s);
    CHECK_EQ(run(d, cmd({"GET", "nope"})), std::string("$-1\r\n"));
}

TEST(cmd_set_wrong_args) {
    server::Store s; server::CommandDispatcher d(s);
    CHECK(run(d, cmd({"SET", "foo"})).rfind("-ERR", 0) == 0);
    CHECK(run(d, cmd({"SET"})).rfind("-ERR", 0) == 0);
}

TEST(cmd_get_wrong_args) {
    server::Store s; server::CommandDispatcher d(s);
    CHECK(run(d, cmd({"GET"})).rfind("-ERR", 0) == 0);
    CHECK(run(d, cmd({"GET", "a", "b"})).rfind("-ERR", 0) == 0);
}

TEST(cmd_del_single) {
    server::Store s; server::CommandDispatcher d(s);
    run(d, cmd({"SET", "x", "1"}));
    CHECK_EQ(run(d, cmd({"DEL", "x"})), std::string(":1\r\n"));
    CHECK_EQ(run(d, cmd({"DEL", "x"})), std::string(":0\r\n"));
}

TEST(cmd_del_multiple) {
    server::Store s; server::CommandDispatcher d(s);
    run(d, cmd({"SET", "a", "1"}));
    run(d, cmd({"SET", "b", "2"}));
    CHECK_EQ(run(d, cmd({"DEL", "a", "b", "c"})), std::string(":2\r\n"));
}

TEST(cmd_exists) {
    server::Store s; server::CommandDispatcher d(s);
    run(d, cmd({"SET", "a", "1"}));
    run(d, cmd({"SET", "b", "2"}));
    CHECK_EQ(run(d, cmd({"EXISTS", "a", "b", "c"})), std::string(":2\r\n"));
}

TEST(cmd_type) {
    server::Store s; server::CommandDispatcher d(s);
    run(d, cmd({"SET", "a", "1"}));
    CHECK_EQ(run(d, cmd({"TYPE", "a"})), std::string("+string\r\n"));
    CHECK_EQ(run(d, cmd({"TYPE", "x"})), std::string("+none\r\n"));
}

TEST(cmd_dbsize) {
    server::Store s; server::CommandDispatcher d(s);
    CHECK_EQ(run(d, cmd({"DBSIZE"})), std::string(":0\r\n"));
    run(d, cmd({"SET", "a", "1"}));
    run(d, cmd({"SET", "b", "2"}));
    CHECK_EQ(run(d, cmd({"DBSIZE"})), std::string(":2\r\n"));
}

TEST(cmd_keys_all) {
    server::Store s; server::CommandDispatcher d(s);
    run(d, cmd({"SET", "user:1", "alice"}));
    run(d, cmd({"SET", "user:2", "bob"}));
    run(d, cmd({"SET", "session:1", "xyz"}));

    auto out = run(d, cmd({"KEYS", "*"}));
    // Порядок неопределён — проверим что начинается с "*3\r\n" и содержит все три
    CHECK(out.rfind("*3\r\n", 0) == 0);
    CHECK(out.find("user:1") != std::string::npos);
    CHECK(out.find("user:2") != std::string::npos);
    CHECK(out.find("session:1") != std::string::npos);
}

TEST(cmd_keys_prefix) {
    server::Store s; server::CommandDispatcher d(s);
    run(d, cmd({"SET", "user:1", "alice"}));
    run(d, cmd({"SET", "user:2", "bob"}));
    run(d, cmd({"SET", "session:1", "xyz"}));

    auto out = run(d, cmd({"KEYS", "user:*"}));
    CHECK(out.rfind("*2\r\n", 0) == 0);
    CHECK(out.find("user:1") != std::string::npos);
    CHECK(out.find("user:2") != std::string::npos);
    CHECK(out.find("session:1") == std::string::npos);
}



// ---------- TTL через команды ----------

TEST(cmd_set_with_ex) {
    server::Store s; server::CommandDispatcher d(s);
    CHECK_EQ(run(d, cmd({"SET", "k", "v", "EX", "100"})), std::string("+OK\r\n"));
    auto out = run(d, cmd({"TTL", "k"}));
    CHECK(out == ":99\r\n" || out == ":100\r\n");
}

TEST(cmd_set_invalid_ex) {
    server::Store s; server::CommandDispatcher d(s);
    CHECK(run(d, cmd({"SET", "k", "v", "EX", "abc"})).rfind("-ERR", 0) == 0);
    CHECK(run(d, cmd({"SET", "k", "v", "EX", "0"})).rfind("-ERR", 0) == 0);
    CHECK(run(d, cmd({"SET", "k", "v", "XX", "5"})).rfind("-ERR", 0) == 0);
}

TEST(cmd_expire) {
    server::Store s; server::CommandDispatcher d(s);
    run(d, cmd({"SET", "k", "v"}));
    CHECK_EQ(run(d, cmd({"EXPIRE", "k", "50"})), std::string(":1\r\n"));
    CHECK_EQ(run(d, cmd({"EXPIRE", "nope", "50"})), std::string(":0\r\n"));
}

TEST(cmd_ttl) {
    server::Store s; server::CommandDispatcher d(s);
    run(d, cmd({"SET", "k", "v"}));
    CHECK_EQ(run(d, cmd({"TTL", "k"})), std::string(":-1\r\n"));
    run(d, cmd({"SET", "k2", "v", "EX", "100"}));
    auto out = run(d, cmd({"TTL", "k2"}));
    CHECK(out == ":99\r\n" || out == ":100\r\n");
    CHECK_EQ(run(d, cmd({"TTL", "nope"})), std::string(":-2\r\n"));
}

TEST(cmd_persist) {
    server::Store s; server::CommandDispatcher d(s);
    run(d, cmd({"SET", "k", "v", "EX", "100"}));
    CHECK_EQ(run(d, cmd({"PERSIST", "k"})), std::string(":1\r\n"));
    CHECK_EQ(run(d, cmd({"TTL", "k"})), std::string(":-1\r\n"));
    CHECK_EQ(run(d, cmd({"PERSIST", "k"})), std::string(":0\r\n"));
}

// ---------- Lists ----------

TEST(cmd_lpush_rpush) {
    server::Store s; server::CommandDispatcher d(s);
    CHECK_EQ(run(d, cmd({"RPUSH", "k", "a", "b"})), std::string(":2\r\n"));
    CHECK_EQ(run(d, cmd({"RPUSH", "k", "c"})),      std::string(":3\r\n"));
    CHECK_EQ(run(d, cmd({"LPUSH", "k", "z"})),      std::string(":4\r\n"));

    auto out = run(d, cmd({"LRANGE", "k", "0", "-1"}));
    // z a b c
    CHECK(out.find("z") != std::string::npos);
    CHECK(out.find("a") != std::string::npos);
    CHECK(out.find("c") != std::string::npos);
}

TEST(cmd_llen) {
    server::Store s; server::CommandDispatcher d(s);
    CHECK_EQ(run(d, cmd({"LLEN", "k"})), std::string(":0\r\n"));
    run(d, cmd({"RPUSH", "k", "a", "b", "c"}));
    CHECK_EQ(run(d, cmd({"LLEN", "k"})), std::string(":3\r\n"));
}

TEST(cmd_lrange_basic) {
    server::Store s; server::CommandDispatcher d(s);
    run(d, cmd({"RPUSH", "k", "a", "b", "c", "d"}));

    auto out = run(d, cmd({"LRANGE", "k", "1", "2"}));
    // $1\r\nb\r\n$1\r\nc\r\n  внутри массива из 2 элементов
    CHECK(out.rfind("*2\r\n", 0) == 0);
    CHECK(out.find("b") != std::string::npos);
    CHECK(out.find("c") != std::string::npos);
    CHECK(out.find("a") == std::string::npos);
}

TEST(cmd_lpop_rpop) {
    server::Store s; server::CommandDispatcher d(s);
    run(d, cmd({"RPUSH", "k", "a", "b", "c"}));

    CHECK_EQ(run(d, cmd({"LPOP", "k"})), std::string("$1\r\na\r\n"));
    CHECK_EQ(run(d, cmd({"RPOP", "k"})), std::string("$1\r\nc\r\n"));
    CHECK_EQ(run(d, cmd({"LPOP", "k"})), std::string("$1\r\nb\r\n"));
    CHECK_EQ(run(d, cmd({"LPOP", "k"})), std::string("$-1\r\n"));  // пусто
}

TEST(cmd_lindex_cmd) {
    server::Store s; server::CommandDispatcher d(s);
    run(d, cmd({"RPUSH", "k", "a", "b", "c"}));

    CHECK_EQ(run(d, cmd({"LINDEX", "k", "0"})),  std::string("$1\r\na\r\n"));
    CHECK_EQ(run(d, cmd({"LINDEX", "k", "-1"})), std::string("$1\r\nc\r\n"));
    CHECK_EQ(run(d, cmd({"LINDEX", "k", "99"})), std::string("$-1\r\n"));
}

TEST(cmd_wrongtype_get_on_list) {
    server::Store s; server::CommandDispatcher d(s);
    run(d, cmd({"RPUSH", "k", "a"}));
    auto out = run(d, cmd({"GET", "k"}));
    CHECK(out.rfind("-WRONGTYPE", 0) == 0);
}

TEST(cmd_wrongtype_lpush_on_string) {
    server::Store s; server::CommandDispatcher d(s);
    run(d, cmd({"SET", "k", "hello"}));
    auto out = run(d, cmd({"LPUSH", "k", "a"}));
    CHECK(out.rfind("-WRONGTYPE", 0) == 0);
}

TEST(cmd_type_list) {
    server::Store s; server::CommandDispatcher d(s);
    run(d, cmd({"RPUSH", "k", "a"}));
    CHECK_EQ(run(d, cmd({"TYPE", "k"})), std::string("+list\r\n"));
}