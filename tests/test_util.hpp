#pragma once

// 轻量测试框架：无第三方依赖，仅用标准库。
// 用法：
//   #include "test_util.hpp"
//   TEST_CASE(my_case) {
//       EXPECT_TRUE(cond);
//       EXPECT_EQ(a, b);
//       EXPECT_CONTAINS(str, "sub");
//   }
//   TEST_MAIN("My Suite")
//
// 每个 TEST_CASE 自动注册；TEST_MAIN 生成 main()，顺序执行所有用例、
// 打印结果与汇总，失败时给出 文件:行号 定位，返回非零退出码。

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <sstream>

namespace tg_test {

struct Case {
    std::string name;
    std::function<void(bool&)> fn;  // fn(ok)：内部断言失败时置 ok=false
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void(bool&)> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline int run_all(const std::string& suite) {
    std::cout << "=== " << suite << " ===\n\n";
    int passed = 0;
    int failed = 0;
    for (auto& c : registry()) {
        bool ok = true;
        std::cout << "[ RUN  ] " << c.name << "\n";
        try {
            c.fn(ok);
        } catch (const std::exception& e) {
            ok = false;
            std::cout << "         exception: " << e.what() << "\n";
        } catch (...) {
            ok = false;
            std::cout << "         unknown exception\n";
        }
        if (ok) {
            std::cout << "[ PASS ] " << c.name << "\n";
            ++passed;
        } else {
            std::cout << "[ FAIL ] " << c.name << "\n";
            ++failed;
        }
    }
    std::cout << "\n--- Summary ---\n"
              << passed << "/" << (passed + failed) << " tests passed\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace tg_test

// ---- 用例定义与注册 ----
#define TEST_CASE(name)                                                        \
    static void name(bool& _tg_ok);                                            \
    static ::tg_test::Registrar _tg_reg_##name(#name, name);                   \
    static void name(bool& _tg_ok)

// ---- 断言宏（失败仅标记，不中断当前用例，便于一次看到多处问题）----
#define EXPECT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            _tg_ok = false;                                                    \
            std::cout << "         FAIL: EXPECT_TRUE(" #cond ") at "           \
                      << __FILE__ << ":" << __LINE__ << "\n";                  \
        }                                                                      \
    } while (0)

#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

#define EXPECT_EQ(a, b)                                                        \
    do {                                                                       \
        auto _tg_a = (a);                                                      \
        auto _tg_b = (b);                                                      \
        if (!(_tg_a == _tg_b)) {                                               \
            _tg_ok = false;                                                    \
            std::ostringstream _tg_ss;                                         \
            _tg_ss << _tg_a << " vs " << _tg_b;                                \
            std::cout << "         FAIL: EXPECT_EQ(" #a ", " #b ") ["          \
                      << _tg_ss.str() << "] at "                               \
                      << __FILE__ << ":" << __LINE__ << "\n";                  \
        }                                                                      \
    } while (0)

// 字符串包含子串（用于校验错误消息等）
#define EXPECT_CONTAINS(str, sub)                                             \
    do {                                                                       \
        std::string _tg_s = (str);                                             \
        std::string _tg_sub = (sub);                                           \
        if (_tg_s.find(_tg_sub) == std::string::npos) {                        \
            _tg_ok = false;                                                    \
            std::cout << "         FAIL: EXPECT_CONTAINS(\"" << _tg_s          \
                      << "\", \"" << _tg_sub << "\") at "                      \
                      << __FILE__ << ":" << __LINE__ << "\n";                  \
        }                                                                      \
    } while (0)

// ---- 生成 main() ----
#define TEST_MAIN(suite)                                                      \
    int main() {                                                               \
        return ::tg_test::run_all(suite);                                      \
    }
