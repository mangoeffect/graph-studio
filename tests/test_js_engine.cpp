// Test: JsEngine 引擎契约（核心库公共 API include/task_graph/js/）。
// 供 render 等子模块消费者引用的最小面：eval/callFunction/异常消息/
// 多实例并行（QuickJS runtime 非线程安全，但每实例一个 runtime 互不干扰）。
#include <task_graph/js/js_engine.hpp>
#include <task_graph/js/js_bindings.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

using namespace task_graph;

TEST(JsEngine, EvalReturnsValue) {
    JsEngine engine;
    JsValue v = engine.eval("1 + 2 * 3");
    ASSERT_TRUE(v);
    double d = 0;
    JS_ToFloat64(engine.context(), &d, v.get());
    EXPECT_DOUBLE_EQ(d, 7.0);
}

TEST(JsEngine, CallGlobalFunction) {
    JsEngine engine;
    engine.eval("function add(a, b) { return a + b; }");
    ASSERT_TRUE(engine.hasFunction("add"));
    JSValueConst argv[2] = {JS_NewInt32(engine.context(), 20),
                            JS_NewInt32(engine.context(), 22)};
    JsValue r = engine.callFunction("add", 2, argv);
    JS_FreeValue(engine.context(), argv[0]);
    JS_FreeValue(engine.context(), argv[1]);
    ASSERT_TRUE(r);
    double d = 0;
    JS_ToFloat64(engine.context(), &d, r.get());
    EXPECT_DOUBLE_EQ(d, 42.0);
}

TEST(JsEngine, ExceptionMessageReadable) {
    JsEngine engine;
    ASSERT_THROW(engine.eval("throw new Error('boom')"), JsException);
    try {
        engine.eval("throw new Error('boom')");
        FAIL() << "expected JsException";
    } catch (const JsException& e) {
        // message-first 语义（QuickJS 的 stack 只含回溯不含消息）
        EXPECT_NE(std::string(e.what()).find("boom"), std::string::npos)
            << e.what();
    }
}

TEST(JsEngine, MultipleEnginesIndependent) {
    // JS_SetClassProto 时代的坑：每 runtime 各自注册类。两个引擎并行 eval
    // 各自的脚本互不干扰（DAG 并行 = 多任务实例 = 多引擎的契约基础）。
    std::atomic<int> ok_count{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&, t] {
            JsEngine engine;
            const int a = t * 10, b = t * 10 + 1;
            JsValue v = engine.eval(
                "function f(a,b){return a+b;} f(" + std::to_string(a) + "," +
                std::to_string(b) + ")");
            if (!v) return;
            double d = 0;
            JS_ToFloat64(engine.context(), &d, v.get());
            if (d == static_cast<double>(a + b)) ok_count++;
        });
    }
    for (auto& th : threads) th.join();
    EXPECT_EQ(ok_count.load(), 4);
}

TEST(JsEngine, ConsoleBindings) {
    JsEngine engine;
    std::string logged;
    registerConsoleBindings(engine.context(),
                            [&](const std::string& msg) { logged += msg + "|"; });
    // 回调逐参数触发（无空格拼接），两参数 -> "hello|js|"
    engine.eval("console.log('hello', 'js')");
    EXPECT_EQ(logged, "hello|js|") << logged;
}

TEST(JsEngine, MathBindings) {
    JsEngine engine;
    registerMathBindings(engine.context());
    JsValue v = engine.eval("TG.Math.clamp(15, 0, 10)");
    ASSERT_TRUE(v);
    double d = 0;
    JS_ToFloat64(engine.context(), &d, v.get());
    EXPECT_DOUBLE_EQ(d, 10.0);
}
