#include "tests/mini_test.hpp"
#include "resp/serializer.hpp"
#include "resp/value.hpp"

using namespace miniredis::resp;

TEST(serialize_simple_string) {
    auto v = make_simple_string("OK");
    CHECK_EQ(serialize(v), std::string("+OK\r\n"));
}

TEST(serialize_error) {
    auto v = make_error("ERR unknown command");
    CHECK_EQ(serialize(v), std::string("-ERR unknown command\r\n"));
}

TEST(serialize_integer) {
    CHECK_EQ(serialize(make_integer(0)),   std::string(":0\r\n"));
    CHECK_EQ(serialize(make_integer(42)),  std::string(":42\r\n"));
    CHECK_EQ(serialize(make_integer(-7)),  std::string(":-7\r\n"));
}

TEST(serialize_bulk_string) {
    CHECK_EQ(serialize(make_bulk_string("hello")), std::string("$5\r\nhello\r\n"));
    CHECK_EQ(serialize(make_bulk_string("")),      std::string("$0\r\n\r\n"));
}

TEST(serialize_null_bulk) {
    CHECK_EQ(serialize(make_null_bulk()), std::string("$-1\r\n"));
}

TEST(serialize_empty_array) {
    auto v = make_array({});
    CHECK_EQ(serialize(v), std::string("*0\r\n"));
}

TEST(serialize_array_of_bulk_strings) {
    // Эквивалент команды: SET key value
    std::vector<RespValue> arr;
    arr.push_back(make_bulk_string("SET"));
    arr.push_back(make_bulk_string("key"));
    arr.push_back(make_bulk_string("value"));

    auto expected =
        std::string("*3\r\n")
      + "$3\r\nSET\r\n"
      + "$3\r\nkey\r\n"
      + "$5\r\nvalue\r\n";

    CHECK_EQ(serialize(make_array(std::move(arr))), expected);
}

TEST(serialize_nested_array) {
    // Массив из двух элементов: [ "a", [ "b", "c" ] ]
    std::vector<RespValue> inner;
    inner.push_back(make_bulk_string("b"));
    inner.push_back(make_bulk_string("c"));

    std::vector<RespValue> outer;
    outer.push_back(make_bulk_string("a"));
    outer.push_back(make_array(std::move(inner)));

    auto expected =
        std::string("*2\r\n")
      + "$1\r\na\r\n"
      + "*2\r\n"
      + "$1\r\nb\r\n"
      + "$1\r\nc\r\n";

    CHECK_EQ(serialize(make_array(std::move(outer))), expected);
}