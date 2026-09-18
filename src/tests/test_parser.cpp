#include "tests/mini_test.hpp"
#include "resp/parser.hpp"
#include "resp/serializer.hpp"

#include <string>

using namespace miniredis::resp;


// ---------- Простые случаи ----------

TEST(parse_simple_string) {
    RespParser p;
    p.feed("+OK\r\n");
    auto v = p.try_parse();
    CHECK(v.has_value());
    CHECK_EQ(serialize(*v), std::string("+OK\r\n"));
}

TEST(parse_error) {
    RespParser p;
    p.feed("-ERR unknown command\r\n");
    auto v = p.try_parse();
    CHECK(v.has_value());
    CHECK_EQ(serialize(*v), std::string("-ERR unknown command\r\n"));
}

TEST(parse_integer) {
    RespParser p;
    p.feed(":12345\r\n");
    auto v = p.try_parse();
    CHECK(v.has_value());
    CHECK_EQ(serialize(*v), std::string(":12345\r\n"));
}

TEST(parse_negative_integer) {
    RespParser p;
    p.feed(":-99\r\n");
    auto v = p.try_parse();
    CHECK(v.has_value());
    CHECK_EQ(serialize(*v), std::string(":-99\r\n"));
}

TEST(parse_bulk_string) {
    RespParser p;
    p.feed("$5\r\nhello\r\n");
    auto v = p.try_parse();
    CHECK(v.has_value());
    CHECK_EQ(serialize(*v), std::string("$5\r\nhello\r\n"));
}

TEST(parse_empty_bulk_string) {
    RespParser p;
    p.feed("$0\r\n\r\n");
    auto v = p.try_parse();
    CHECK(v.has_value());
    CHECK_EQ(serialize(*v), std::string("$0\r\n\r\n"));
}

TEST(parse_null_bulk) {
    RespParser p;
    p.feed("$-1\r\n");
    auto v = p.try_parse();
    CHECK(v.has_value());
    CHECK_EQ(serialize(*v), std::string("$-1\r\n"));
}

TEST(parse_command_set_key_value) {
    // Эквивалент: SET key value
    std::string raw =
        "*3\r\n"
        "$3\r\nSET\r\n"
        "$3\r\nkey\r\n"
        "$5\r\nvalue\r\n";

    RespParser p;
    p.feed(raw);

    auto v = p.try_parse();
    CHECK(v.has_value());
    CHECK_EQ(serialize(*v), raw);

    // буфер должен опустеть
    CHECK_EQ(p.buffered_size(), std::size_t{0});
}

// ---------- Partial read (куски) ----------

TEST(parse_partial_simple_string) {
    RespParser p;

    p.feed("+OK");
    CHECK(!p.try_parse().has_value());      // не хватает CRLF

    p.feed("\r\n");
    auto v = p.try_parse();
    CHECK(v.has_value());
    CHECK_EQ(serialize(*v), std::string("+OK\r\n"));
}

TEST(parse_partial_bulk_string) {
    RespParser p;

    p.feed("$5\r\nhel");
    CHECK(!p.try_parse().has_value());

    p.feed("lo");
    CHECK(!p.try_parse().has_value());      // не хватает завершающего CRLF

    p.feed("\r\n");
    auto v = p.try_parse();
    CHECK(v.has_value());
    CHECK_EQ(serialize(*v), std::string("$5\r\nhello\r\n"));
}

TEST(parse_two_messages_in_one_chunk) {
    RespParser p;
    p.feed("+OK\r\n:42\r\n");

    auto v1 = p.try_parse();
    CHECK(v1.has_value());
    CHECK_EQ(serialize(*v1), std::string("+OK\r\n"));

    auto v2 = p.try_parse();
    CHECK(v2.has_value());
    CHECK_EQ(serialize(*v2), std::string(":42\r\n"));

    CHECK(!p.try_parse().has_value());
    CHECK_EQ(p.buffered_size(), std::size_t{0});
}

TEST(parse_array_one_byte_at_a_time) {
    std::string raw =
        "*2\r\n"
        "$3\r\nfoo\r\n"
        "$3\r\nbar\r\n";

    RespParser p;
    std::optional<RespValue> v;
    for (char c : raw) {
        p.feed(std::string_view(&c, 1));
        v = p.try_parse();
        if (v) break;
    }
    CHECK(v.has_value());
    CHECK_EQ(serialize(*v), raw);
}

// ---------- Ошибки ----------

TEST(parse_unknown_tag_throws) {
    RespParser p;
    p.feed("?what\r\n");

    bool thrown = false;
    try {
        p.try_parse();
    } catch (const ProtocolError&) {
        thrown = true;
    }
    CHECK(thrown);
}

TEST(parse_invalid_integer_throws) {
    RespParser p;
    p.feed(":12x4\r\n");

    bool thrown = false;
    try {
        p.try_parse();
    } catch (const ProtocolError&) {
        thrown = true;
    }
    CHECK(thrown);
}