#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace mini_test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

/// Реестр всех тестов. Заполняется автоматически через REGISTER_TEST.
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

/// Исключение для проваленных проверок.
struct AssertionFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// Регистратор — создаётся в глобальном scope для каждого TEST(...).
struct Registrar {
    Registrar(std::string name, std::function<void()> fn) {
        registry().push_back({std::move(name), std::move(fn)});
    }
};

/// Запускает все зарегистрированные тесты.
/// Возвращает 0 если все прошли, иначе — количество упавших.
inline int run_all() {
    int failed = 0;
    int passed = 0;

    for (auto& t : registry()) {
        try {
            t.fn();
            std::cout << "[ PASS ] " << t.name << "\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "[ FAIL ] " << t.name << "\n"
                      << "         " << e.what() << "\n";
            ++failed;
        } catch (...) {
            std::cout << "[ FAIL ] " << t.name << " (unknown exception)\n";
            ++failed;
        }
    }

    std::cout << "\n"
              << "Passed: " << passed << "\n"
              << "Failed: " << failed << "\n";
    return failed;
}

} // namespace mini_test

// ---- Макросы ----

#define TEST(name)                                                        \
    static void test_##name();                                            \
    static ::mini_test::Registrar registrar_##name(#name, test_##name);   \
    static void test_##name()

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::ostringstream _oss;                                      \
            _oss << __FILE__ << ":" << __LINE__                           \
                 << "  CHECK(" #cond ") failed";                          \
            throw ::mini_test::AssertionFailure(_oss.str());              \
        }                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        auto&& _a = (a);                                                  \
        auto&& _b = (b);                                                  \
        if (!(_a == _b)) {                                                \
            std::ostringstream _oss;                                      \
            _oss << __FILE__ << ":" << __LINE__                           \
                 << "  CHECK_EQ(" #a ", " #b ") failed";                  \
            throw ::mini_test::AssertionFailure(_oss.str());              \
        }                                                                 \
    } while (0)

/// Проверить, что optional содержит значение и оно равно ожидаемому.
#define CHECK_OPT_EQ(opt_expr, expected)                                  \
    do {                                                                  \
        auto&& _opt = (opt_expr);                                         \
        if (!_opt.has_value()) {                                          \
            std::ostringstream _oss;                                      \
            _oss << __FILE__ << ":" << __LINE__                           \
                 << "  CHECK_OPT_EQ(" #opt_expr ") has no value";         \
            throw ::mini_test::AssertionFailure(_oss.str());              \
        }                                                                 \
        if (!(*_opt == (expected))) {                                     \
            std::ostringstream _oss;                                      \
            _oss << __FILE__ << ":" << __LINE__                           \
                 << "  CHECK_OPT_EQ(" #opt_expr ", " #expected ") failed";\
            throw ::mini_test::AssertionFailure(_oss.str());              \
        }                                                                 \
    } while (0)
