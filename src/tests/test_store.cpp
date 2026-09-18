#include "tests/mini_test.hpp"
#include "server/store.hpp"

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

using namespace miniredis::server;

TEST(store_set_get) {
    Store s;
    CHECK(s.set("k", "v"));
    auto v = s.get("k");
    CHECK(v.has_value());
    CHECK_EQ(*v, std::string("v"));
}

TEST(store_set_overwrites) {
    Store s;
    CHECK(s.set("k", "v1"));
    CHECK(!s.set("k", "v2"));   // уже существовал → false

    auto v = s.get("k");
    CHECK(v.has_value());
    CHECK_EQ(*v, std::string("v2"));
}

TEST(store_get_missing) {
    Store s;
    CHECK(!s.get("nope").has_value());
}

TEST(store_del) {
    Store s;
    s.set("a", "1");
    s.set("b", "2");

    CHECK(s.del("a"));
    CHECK(!s.del("a"));         // второй раз нет
    CHECK(!s.exists("a"));
    CHECK(s.exists("b"));
    CHECK_EQ(s.size(), std::size_t{1});
}

TEST(store_exists_and_size) {
    Store s;
    CHECK(!s.exists("x"));
    s.set("x", "1");
    s.set("y", "2");
    CHECK(s.exists("x"));
    CHECK(s.exists("y"));
    CHECK_EQ(s.size(), std::size_t{2});
}

TEST(store_keys) {
    Store s;
    s.set("a", "1");
    s.set("b", "2");
    s.set("c", "3");

    auto keys = s.keys();
    CHECK_EQ(keys.size(), std::size_t{3});

    // порядок не гарантирован — сортируем и проверяем множество
    std::sort(keys.begin(), keys.end());
    CHECK_EQ(keys[0], std::string("a"));
    CHECK_EQ(keys[1], std::string("b"));
    CHECK_EQ(keys[2], std::string("c"));
}

TEST(store_clear) {
    Store s;
    s.set("a", "1");
    s.set("b", "2");
    s.clear();
    CHECK_EQ(s.size(), std::size_t{0});
}

// ---------- Многопоточный стресс-тест ----------

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
                s.set(key, std::to_string(i));
            }
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                std::string key = prefix + std::to_string(i);
                auto v = s.get(key);
                (void)v;   // просто читаем, main assert ниже
            }
        });
    }

    for (auto& th : threads) th.join();

    CHECK_EQ(s.size(), std::size_t{THREADS * OPS_PER_THREAD});

    // Проверим несколько ключей на корректность
    auto v0 = s.get("t0:0");
    CHECK(v0.has_value());
    CHECK_EQ(*v0, std::string("0"));

    auto v7 = s.get("t7:499");
    CHECK(v7.has_value());
    CHECK_EQ(*v7, std::string("499"));
}



// ---------- TTL тесты ----------

#include <chrono>
#include <thread>

TEST(store_ttl_no_ttl) {
    Store s;
    s.set("k", "v");
    CHECK_EQ(s.ttl("k"), std::int64_t{-1});
}

TEST(store_ttl_missing_key) {
    Store s;
    CHECK_EQ(s.ttl("nope"), std::int64_t{-2});
}

TEST(store_ttl_with_set_ex) {
    Store s;
    s.set("k", "v", 100);
    auto t = s.ttl("k");
    CHECK(t >= 95 && t <= 100);
}

TEST(store_expire_command) {
    Store s;
    s.set("k", "v");
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
    s.set("k", "v");
    CHECK(s.expire("k", 0));
    CHECK(!s.exists("k"));
}

TEST(store_persist) {
    Store s;
    s.set("k", "v", 100);
    CHECK(s.persist("k"));
    CHECK_EQ(s.ttl("k"), std::int64_t{-1});
    CHECK(!s.persist("k"));
}

TEST(store_key_expires) {
    Store s;
    s.set("k", "v", 1);
    CHECK(s.exists("k"));
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    CHECK(!s.exists("k"));
    CHECK(!s.get("k").has_value());
    CHECK_EQ(s.ttl("k"), std::int64_t{-2});
}

TEST(store_sweep) {
    Store s;
    s.set("a", "1", 1);
    s.set("b", "2", 1);
    s.set("c", "3");

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    auto removed = s.sweep_expired();
    CHECK_EQ(removed, std::size_t{2});
    CHECK_EQ(s.size(), std::size_t{1});
    CHECK(s.exists("c"));
}