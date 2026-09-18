#include "tests/mini_test.hpp"
#include "server/store.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace miniredis::server;

// ---------- Базовые операции ----------

TEST(store_set_get) {
    Store s;
    CHECK(s.set_string("k", "v"));
    auto v = s.get_string("k");
    CHECK(v.has_value());
    CHECK_EQ(*v, std::string("v"));
}

TEST(store_set_overwrites) {
    Store s;
    CHECK(s.set_string("k", "v1"));
    CHECK(!s.set_string("k", "v2"));

    auto v = s.get_string("k");
    CHECK(v.has_value());
    CHECK_EQ(*v, std::string("v2"));
}

TEST(store_get_missing) {
    Store s;
    CHECK(!s.get_string("nope").has_value());
}

TEST(store_del) {
    Store s;
    s.set_string("a", "1");
    s.set_string("b", "2");

    CHECK(s.del("a"));
    CHECK(!s.del("a"));
    CHECK(!s.exists("a"));
    CHECK(s.exists("b"));
    CHECK_EQ(s.size(), std::size_t{1});
}

TEST(store_exists_and_size) {
    Store s;
    CHECK(!s.exists("x"));
    s.set_string("x", "1");
    s.set_string("y", "2");
    CHECK(s.exists("x"));
    CHECK(s.exists("y"));
    CHECK_EQ(s.size(), std::size_t{2});
}

TEST(store_keys) {
    Store s;
    s.set_string("a", "1");
    s.set_string("b", "2");
    s.set_string("c", "3");

    auto keys = s.keys();
    CHECK_EQ(keys.size(), std::size_t{3});
    std::sort(keys.begin(), keys.end());
    CHECK_EQ(keys[0], std::string("a"));
    CHECK_EQ(keys[1], std::string("b"));
    CHECK_EQ(keys[2], std::string("c"));
}

TEST(store_clear) {
    Store s;
    s.set_string("a", "1");
    s.set_string("b", "2");
    s.clear();
    CHECK_EQ(s.size(), std::size_t{0});
}

TEST(store_concurrent_set_get) {
    Store s;
    constexpr int THREADS = 8;
    constexpr int OPS_PER_THREAD = 500;

    std::vector<std::thread> threads;
    threads.reserve(THREADS);

    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([&s, t]() {
            std::string prefix = "t" + std::to_string(t) + ":";
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                std::string key = prefix + std::to_string(i);
                s.set_string(key, std::to_string(i));
            }
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                std::string key = prefix + std::to_string(i);
                auto v = s.get_string(key);
                (void)v;
            }
        });
    }

    for (auto& th : threads) th.join();

    CHECK_EQ(s.size(), std::size_t{THREADS * OPS_PER_THREAD});
    auto v0 = s.get_string("t0:0");
    CHECK(v0.has_value());
    CHECK_EQ(*v0, std::string("0"));
    auto v7 = s.get_string("t7:499");
    CHECK(v7.has_value());
    CHECK_EQ(*v7, std::string("499"));
}

// ---------- TTL ----------

TEST(store_ttl_no_ttl) {
    Store s;
    s.set_string("k", "v");
    CHECK_EQ(s.ttl("k"), std::int64_t{-1});
}

TEST(store_ttl_missing_key) {
    Store s;
    CHECK_EQ(s.ttl("nope"), std::int64_t{-2});
}

TEST(store_ttl_with_set_ex) {
    Store s;
    s.set_string("k", "v", 100);
    auto t = s.ttl("k");
    CHECK(t >= 95 && t <= 100);
}

TEST(store_expire_command) {
    Store s;
    s.set_string("k", "v");
    CHECK(s.expire("k", 50));
    auto t = s.ttl("k");
    CHECK(t >= 45 && t <= 50);
}

TEST(store_expire_missing_key) {
    Store s;
    CHECK(!s.expire("nope", 10));
}

TEST(store_expire_zero_deletes) {
    Store s;
    s.set_string("k", "v");
    CHECK(s.expire("k", 0));
    CHECK(!s.exists("k"));
}

TEST(store_persist) {
    Store s;
    s.set_string("k", "v", 100);
    CHECK(s.persist("k"));
    CHECK_EQ(s.ttl("k"), std::int64_t{-1});
    CHECK(!s.persist("k"));
}

TEST(store_key_expires) {
    Store s;
    s.set_string("k", "v", 1);
    CHECK(s.exists("k"));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    CHECK(!s.exists("k"));
    CHECK(!s.get_string("k").has_value());
    CHECK_EQ(s.ttl("k"), std::int64_t{-2});
}

TEST(store_sweep) {
    Store s;
    s.set_string("a", "1", 1);
    s.set_string("b", "2", 1);
    s.set_string("c", "3");

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    auto removed = s.sweep_expired();
    CHECK_EQ(removed, std::size_t{2});
    CHECK_EQ(s.size(), std::size_t{1});
    CHECK(s.exists("c"));
}

// ---------- Lists ----------

TEST(store_list_push_left) {
    Store s;
    auto n = s.list_push_left("k", {"a", "b", "c"});
    CHECK(n.has_value());
    CHECK_EQ(*n, std::size_t{3});

    auto r = s.list_range("k", 0, -1);
    CHECK(r.has_value());
    CHECK_EQ(r->size(), std::size_t{3});
    CHECK_EQ((*r)[0], std::string("c"));
    CHECK_EQ((*r)[1], std::string("b"));
    CHECK_EQ((*r)[2], std::string("a"));
}

TEST(store_list_push_right) {
    Store s;
    auto n = s.list_push_right("k", {"a", "b", "c"});
    CHECK(n.has_value());
    CHECK_EQ(*n, std::size_t{3});

    auto r = s.list_range("k", 0, -1);
    CHECK(r.has_value());
    CHECK_EQ((*r)[0], std::string("a"));
    CHECK_EQ((*r)[1], std::string("b"));
    CHECK_EQ((*r)[2], std::string("c"));
}

TEST(store_list_length) {
    Store s;
    auto len0 = s.list_length("k");
    CHECK(len0.has_value());
    CHECK_EQ(*len0, std::size_t{0});

    s.list_push_right("k", {"a", "b"});

    auto len2 = s.list_length("k");
    CHECK(len2.has_value());
    CHECK_EQ(*len2, std::size_t{2});
}

TEST(store_list_pop) {
    Store s;
    s.list_push_right("k", {"a", "b", "c"});

    auto l = s.list_pop_left("k");
    CHECK(l.has_value());
    CHECK_EQ(*l, std::string("a"));

    auto r = s.list_pop_right("k");
    CHECK(r.has_value());
    CHECK_EQ(*r, std::string("c"));

    auto len = s.list_length("k");
    CHECK(len.has_value());
    CHECK_EQ(*len, std::size_t{1});
}

TEST(store_list_pop_until_empty) {
    Store s;
    s.list_push_right("k", {"a"});

    auto v = s.list_pop_left("k");
    CHECK(v.has_value());
    CHECK_EQ(*v, std::string("a"));

    CHECK(!s.exists("k"));
    CHECK(!s.list_pop_left("k").has_value());
}

TEST(store_list_range_negatives) {
    Store s;
    s.list_push_right("k", {"a", "b", "c", "d", "e"});

    auto r1 = s.list_range("k", 1, 3);
    CHECK(r1.has_value());
    CHECK_EQ(r1->size(), std::size_t{3});
    CHECK_EQ((*r1)[0], std::string("b"));
    CHECK_EQ((*r1)[2], std::string("d"));

    auto r2 = s.list_range("k", -2, -1);
    CHECK(r2.has_value());
    CHECK_EQ(r2->size(), std::size_t{2});
    CHECK_EQ((*r2)[0], std::string("d"));
    CHECK_EQ((*r2)[1], std::string("e"));

    auto r3 = s.list_range("k", 0, -1);
    CHECK_EQ(r3->size(), std::size_t{5});
}

TEST(store_list_index) {
    Store s;
    s.list_push_right("k", {"a", "b", "c"});

    auto v0 = s.list_index("k", 0);
    CHECK(v0.has_value());
    CHECK_EQ(*v0, std::string("a"));

    auto v_neg = s.list_index("k", -1);
    CHECK(v_neg.has_value());
    CHECK_EQ(*v_neg, std::string("c"));

    CHECK(!s.list_index("k", 100).has_value());
}

TEST(store_wrongtype_push_on_string) {
    Store s;
    s.set_string("k", "hello");
    auto n = s.list_push_left("k", {"a"});
    CHECK(!n.has_value());
}

// ---------- Hashes ----------

TEST(store_hash_set_get) {
    Store s;
    auto n = s.hash_set("h", {{"a", "1"}, {"b", "2"}});
    CHECK(n.has_value());
    CHECK_EQ(*n, std::size_t{2});

    auto va = s.hash_get("h", "a");
    CHECK(va.has_value());
    CHECK_EQ(*va, std::string("1"));

    auto miss = s.hash_get("h", "zzz");
    CHECK(!miss.has_value());
}

TEST(store_hash_set_update) {
    Store s;
    s.hash_set("h", {{"a", "1"}});

    auto n = s.hash_set("h", {{"a", "updated"}, {"b", "new"}});
    CHECK(n.has_value());
    CHECK_EQ(*n, std::size_t{1});

    auto va = s.hash_get("h", "a");
    CHECK(va.has_value());
    CHECK_EQ(*va, std::string("updated"));
}

TEST(store_hash_del) {
    Store s;
    s.hash_set("h", {{"a", "1"}, {"b", "2"}, {"c", "3"}});

    auto removed = s.hash_del("h", {"a", "c", "zzz"});
    CHECK(removed.has_value());
    CHECK_EQ(*removed, std::size_t{2});

    auto len = s.hash_length("h");
    CHECK(len.has_value());
    CHECK_EQ(*len, std::size_t{1});
}

TEST(store_hash_empty_deletes_key) {
    Store s;
    s.hash_set("h", {{"a", "1"}});
    s.hash_del("h", {"a"});
    CHECK(!s.exists("h"));
}

TEST(store_hash_exists) {
    Store s;
    s.hash_set("h", {{"a", "1"}});
    auto e1 = s.hash_exists("h", "a");
    auto e2 = s.hash_exists("h", "b");
    CHECK(e1.has_value() && *e1);
    CHECK(e2.has_value() && !*e2);
}

TEST(store_hash_length_missing) {
    Store s;
    auto len = s.hash_length("nope");
    CHECK(len.has_value());
    CHECK_EQ(*len, std::size_t{0});
}

TEST(store_hash_keys_values) {
    Store s;
    s.hash_set("h", {{"x", "1"}, {"y", "2"}, {"z", "3"}});

    auto keys = s.hash_keys("h");
    auto vals = s.hash_values("h");
    CHECK(keys.has_value());
    CHECK(vals.has_value());
    CHECK_EQ(keys->size(), std::size_t{3});
    CHECK_EQ(vals->size(), std::size_t{3});
}

TEST(store_hash_wrongtype) {
    Store s;
    s.set_string("k", "hello");
    auto n = s.hash_set("k", {{"a", "1"}});
    CHECK(!n.has_value());
}